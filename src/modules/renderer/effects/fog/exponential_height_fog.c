#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsDistanceFog);
ECS_COMPONENT_DECLARE(FlecsHeightFog);
ECS_COMPONENT_DECLARE(FlecsHeightFogImpl);

typedef struct FlecsHeightFogUniform {
    mat4 inv_vp;
    float camera_pos[4];
    float fog_color_density[4];
    float fog_params[4];
    float distance_fog_color_density[4];
} FlecsHeightFogUniform;

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct FogUniforms {\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  camera_pos : vec4<f32>,\n"
    "  fog_color_density : vec4<f32>,\n"
    "  fog_params : vec4<f32>,\n"
    "  distance_fog_color_density : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : FogUniforms;\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(\n"
    "    uv.x * 2.0 - 1.0,\n"
    "    (1.0 - uv.y) * 2.0 - 1.0,\n"
    "    depth,\n"
    "    1.0);\n"
    "  let world_h = uniforms.inv_vp * ndc;\n"
    "  if (abs(world_h.w) > 1e-6) {\n"
    "    return world_h.xyz / world_h.w;\n"
    "  }\n"
    "  return world_h.xyz;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  let is_sky = depth >= 0.9999;\n"
    "  let world_pos = reconstruct_world_pos(in.uv, depth);\n"
    "  let ray = world_pos - uniforms.camera_pos.xyz;\n"
    "  let distance = length(ray);\n"
    "  if (distance <= 1e-6 && !is_sky) {\n"
    "    return src;\n"
    "  }\n"
    "  var fogged = src.rgb;\n"
    /* Distance fog: plain exponential */
    "  let dist_density = max(uniforms.distance_fog_color_density.w, 0.0);\n"
    "  if (dist_density > 0.0 && !is_sky) {\n"
    "    let dist_factor = 1.0 - exp(clamp(-dist_density * distance, -80.0, 0.0));\n"
    "    fogged = mix(fogged, uniforms.distance_fog_color_density.rgb, dist_factor);\n"
    "  }\n"
    /* Height fog: skip for sky pixels (no meaningful world position) */
    "  let density = max(uniforms.fog_color_density.w, 0.0);\n"
    "  if (density > 0.0 && !is_sky) {\n"
    "    let falloff = max(uniforms.fog_params.x, 1e-6);\n"
    "    let base_height = uniforms.fog_params.y;\n"
    "    let max_opacity = clamp(uniforms.fog_params.z, 0.0, 1.0);\n"
    "    let camera_height = uniforms.camera_pos.y - base_height;\n"
    "    let world_height = world_pos.y - base_height;\n"
    "    let e0 = exp(clamp(-falloff * camera_height, -80.0, 80.0));\n"
    "    let e1 = exp(clamp(-falloff * world_height, -80.0, 80.0));\n"
    "    let dy = world_pos.y - uniforms.camera_pos.y;\n"
    "    var integral = distance * sqrt(max(e0 * e1, 0.0));\n"
    "    if (abs(dy) > 1e-5) {\n"
    "      integral = distance * (e0 - e1) / (falloff * dy);\n"
    "    }\n"
    "    let optical_depth = max(0.0, density * max(integral, 0.0));\n"
    "    let fog_factor = min(max_opacity, 1.0 - exp(clamp(-optical_depth, -80.0, 0.0)));\n"
    "    fogged = mix(fogged, uniforms.fog_color_density.rgb, fog_factor);\n"
    "  }\n"
    "  return vec4<f32>(fogged, src.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_heightFog_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "HeightFogShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_heightFog_releaseResources(
    FlecsHeightFogImpl *impl)
{
    if (impl->uniform_buffer) {
        wgpuBufferRelease(impl->uniform_buffer);
        impl->uniform_buffer = NULL;
    }
}

ECS_DTOR(FlecsHeightFogImpl, ptr, {
    flecsEngine_heightFog_releaseResources(ptr);
})

ECS_MOVE(FlecsHeightFogImpl, dst, src, {
    flecsEngine_heightFog_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_heightFog_fillUniform(
    const ecs_world_t *world,
    ecs_entity_t effect_entity,
    const FlecsHeightFog *fog,
    const FlecsDistanceFog *dist_fog,
    FlecsHeightFogUniform *uniform)
{
    glm_mat4_identity(uniform->inv_vp);

    uniform->camera_pos[0] = 0.0f;
    uniform->camera_pos[1] = 0.0f;
    uniform->camera_pos[2] = 0.0f;
    uniform->camera_pos[3] = 1.0f;

    if (fog) {
        uniform->fog_color_density[0] = flecsEngine_colorChannelToFloat(fog->color.r);
        uniform->fog_color_density[1] = flecsEngine_colorChannelToFloat(fog->color.g);
        uniform->fog_color_density[2] = flecsEngine_colorChannelToFloat(fog->color.b);
        uniform->fog_color_density[3] = fog->density;
        uniform->fog_params[0] = fog->falloff;
        uniform->fog_params[1] = fog->base_height;
        uniform->fog_params[2] = fog->max_opacity;
        uniform->fog_params[3] = 0.0f;
    }

    if (dist_fog) {
        uniform->distance_fog_color_density[0] = flecsEngine_colorChannelToFloat(dist_fog->color.r);
        uniform->distance_fog_color_density[1] = flecsEngine_colorChannelToFloat(dist_fog->color.g);
        uniform->distance_fog_color_density[2] = flecsEngine_colorChannelToFloat(dist_fog->color.b);
        uniform->distance_fog_color_density[3] = dist_fog->density;
    }

    ecs_entity_t view_entity = ecs_get_target(world, effect_entity, EcsChildOf, 0);
    if (!view_entity) {
        return;
    }

    const FlecsRenderView *view = ecs_get(world, view_entity, FlecsRenderView);
    if (!view || !view->camera) {
        return;
    }

    const FlecsCameraImpl *camera = ecs_get(world, view->camera, FlecsCameraImpl);
    if (!camera) {
        return;
    }

    mat4 mvp;
    glm_mat4_copy((vec4*)camera->mvp, mvp);
    glm_mat4_inv(mvp, uniform->inv_vp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, view->camera, FlecsWorldTransform3);
    if (camera_transform) {
        uniform->camera_pos[0] = camera_transform->m[3][0];
        uniform->camera_pos[1] = camera_transform->m[3][1];
        uniform->camera_pos[2] = camera_transform->m[3][2];
    }
}

static bool flecsEngine_heightFog_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)effect_entity;
    (void)effect;
    (void)effect_impl;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsHeightFogImpl fog_impl = {0};

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsHeightFogUniform)
    };

    fog_impl.uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!fog_impl.uniform_buffer) {
        return false;
    }

    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsHeightFogUniform)
        }
    };

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsHeightFogImpl, &fog_impl);

    *entry_count = 4;
    return true;
}

static bool flecsEngine_heightFog_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)effect;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    if (!engine->depth.depth_texture_view) {
        return false;
    }

    const FlecsHeightFogImpl *fog_impl = ecs_get(
        world, effect_entity, FlecsHeightFogImpl);
    if (!fog_impl || !fog_impl->uniform_buffer) {
        return false;
    }

    const FlecsHeightFog *fog = ecs_get(
        world, effect_entity, FlecsHeightFog);
    const FlecsDistanceFog *dist_fog = ecs_get(
        world, effect_entity, FlecsDistanceFog);

    FlecsHeightFogUniform uniform = {0};
    flecsEngine_heightFog_fillUniform(
        world, effect_entity, fog, dist_fog, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        fog_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = engine->depth.depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = fog_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsHeightFogUniform)
    };

    *entry_count = 4;
    return true;
}

FlecsHeightFog flecsEngine_heightFogSettingsDefault(void)
{
    return (FlecsHeightFog){
        .density = 0.1f,
        .falloff = 0.3f,
        .base_height = 0.0f,
        .max_opacity = 1.0f,
        .color = {191, 158, 140, 255}
    };
}

ecs_entity_t flecsEngine_createEffect_heightFog(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsHeightFog *settings)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });

    FlecsHeightFog fog = settings
        ? *settings
        : flecsEngine_heightFogSettingsDefault();

    ecs_set_ptr(world, effect, FlecsHeightFog, &fog);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsEngine_heightFog_shader(world),
        .input = input,
        .setup_callback = flecsEngine_heightFog_setup,
        .bind_callback = flecsEngine_heightFog_bind
    });

    return effect;
}

ecs_entity_t flecsEngine_createEffect_distanceFog(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsDistanceFog *settings)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });

    FlecsDistanceFog fog = settings
        ? *settings
        : (FlecsDistanceFog){ .density = 0.01f, .color = {180, 190, 200, 255} };

    ecs_set_ptr(world, effect, FlecsDistanceFog, &fog);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsEngine_heightFog_shader(world),
        .input = input,
        .setup_callback = flecsEngine_heightFog_setup,
        .bind_callback = flecsEngine_heightFog_bind
    });

    return effect;
}

void flecsEngine_heightFog_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsDistanceFog);
    ECS_COMPONENT_DEFINE(world, FlecsHeightFog);
    ECS_COMPONENT_DEFINE(world, FlecsHeightFogImpl);

    ecs_set_hooks(world, FlecsHeightFogImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsHeightFogImpl),
        .dtor = ecs_dtor(FlecsHeightFogImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsDistanceFog),
        .members = {
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHeightFog),
        .members = {
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "falloff", .type = ecs_id(ecs_f32_t) },
            { .name = "base_height", .type = ecs_id(ecs_f32_t) },
            { .name = "max_opacity", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(flecs_rgba_t) }
        }
    });
}
