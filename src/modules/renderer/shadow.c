#include "renderer.h"
#include "flecs_engine.h"
#include <cglm/clipspace/ortho_rh_zo.h>

#define FLECS_ENGINE_SHADOW_MAP_SIZE 4096
#define FLECS_ENGINE_SHADOW_MAP_FORMAT WGPUTextureFormat_Depth32Float

static const char *kShadowDepthShaderSource =
    "struct ShadowUniforms {\n"
    "  light_vp : mat4x4<f32>\n"
    "}\n"
    "@group(0) @binding(0) var<uniform> shadow_uniforms : ShadowUniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>\n"
    "}\n"
    "@vertex fn vs_main(input : VertexInput) -> @builtin(position) vec4<f32> {\n"
    "  let model = mat4x4<f32>(\n"
    "    vec4<f32>(input.m0, 0.0),\n"
    "    vec4<f32>(input.m1, 0.0),\n"
    "    vec4<f32>(input.m2, 0.0),\n"
    "    vec4<f32>(input.m3, 1.0)\n"
    "  );\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  return shadow_uniforms.light_vp * world_pos;\n"
    "}\n";

int flecsEngine_shadow_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    (void)world;
    impl->shadow_map_size = FLECS_ENGINE_SHADOW_MAP_SIZE;

    /* Compile shadow depth shader directly (bypasses ECS shader system
     * to avoid deferred context issues during batch setup) */
    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = (WGPUStringView){
            .data = kShadowDepthShaderSource,
            .length = WGPU_STRLEN
        }
    };

    impl->shadow_shader_module = wgpuDeviceCreateShaderModule(
        impl->device, &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&wgsl_desc
        });
    if (!impl->shadow_shader_module) {
        ecs_err("failed to compile shadow depth shader");
        return -1;
    }

    /* Create shadow depth texture */
    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = impl->shadow_map_size,
            .height = impl->shadow_map_size,
            .depthOrArrayLayers = 1
        },
        .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    impl->shadow_texture = wgpuDeviceCreateTexture(impl->device, &tex_desc);
    if (!impl->shadow_texture) {
        ecs_err("failed to create shadow map texture");
        return -1;
    }

    impl->shadow_texture_view = wgpuTextureCreateView(
        impl->shadow_texture, NULL);
    if (!impl->shadow_texture_view) {
        ecs_err("failed to create shadow map texture view");
        return -1;
    }

    /* Create comparison sampler for shadow sampling */
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .compare = WGPUCompareFunction_Less,
        .maxAnisotropy = 1
    };

    impl->shadow_sampler = wgpuDeviceCreateSampler(impl->device, &sampler_desc);
    if (!impl->shadow_sampler) {
        ecs_err("failed to create shadow sampler");
        return -1;
    }

    /* Create light VP uniform buffer (mat4, 64 bytes) */
    WGPUBufferDescriptor buf_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(mat4)
    };

    impl->shadow_vp_buffer = wgpuDeviceCreateBuffer(impl->device, &buf_desc);
    if (!impl->shadow_vp_buffer) {
        ecs_err("failed to create shadow VP buffer");
        return -1;
    }

    /* Shadow pass bind group layout: binding 0 = light VP uniform */
    WGPUBindGroupLayoutEntry pass_layout_entries[1] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = (WGPUBufferBindingLayout){
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(mat4)
        }
    }};

    WGPUBindGroupLayoutDescriptor pass_layout_desc = {
        .entryCount = 1,
        .entries = pass_layout_entries
    };

    impl->shadow_pass_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &pass_layout_desc);
    if (!impl->shadow_pass_bind_layout) {
        ecs_err("failed to create shadow pass bind group layout");
        return -1;
    }

    /* Shadow pass bind group */
    WGPUBindGroupEntry pass_entries[1] = {{
        .binding = 0,
        .buffer = impl->shadow_vp_buffer,
        .size = sizeof(mat4)
    }};

    WGPUBindGroupDescriptor pass_group_desc = {
        .layout = impl->shadow_pass_bind_layout,
        .entryCount = 1,
        .entries = pass_entries
    };

    impl->shadow_pass_bind_group = wgpuDeviceCreateBindGroup(
        impl->device, &pass_group_desc);
    if (!impl->shadow_pass_bind_group) {
        ecs_err("failed to create shadow pass bind group");
        return -1;
    }

    /* Shadow sample bind group layout: depth texture + comparison sampler */
    WGPUBindGroupLayoutEntry sample_layout_entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = (WGPUTextureBindingLayout){
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = (WGPUSamplerBindingLayout){
                .type = WGPUSamplerBindingType_Comparison
            }
        }
    };

    WGPUBindGroupLayoutDescriptor sample_layout_desc = {
        .entryCount = 2,
        .entries = sample_layout_entries
    };

    impl->shadow_sample_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &sample_layout_desc);
    if (!impl->shadow_sample_bind_layout) {
        ecs_err("failed to create shadow sample bind group layout");
        return -1;
    }

    /* Shadow sample bind group */
    WGPUBindGroupEntry sample_entries[2] = {
        {
            .binding = 0,
            .textureView = impl->shadow_texture_view
        },
        {
            .binding = 1,
            .sampler = impl->shadow_sampler
        }
    };

    WGPUBindGroupDescriptor sample_group_desc = {
        .layout = impl->shadow_sample_bind_layout,
        .entryCount = 2,
        .entries = sample_entries
    };

    impl->shadow_sample_bind_group = wgpuDeviceCreateBindGroup(
        impl->device, &sample_group_desc);
    if (!impl->shadow_sample_bind_group) {
        ecs_err("failed to create shadow sample bind group");
        return -1;
    }

    return 0;
}

void flecsEngine_shadow_cleanup(
    FlecsEngineImpl *impl)
{
    if (impl->shadow_sample_bind_group) {
        wgpuBindGroupRelease(impl->shadow_sample_bind_group);
        impl->shadow_sample_bind_group = NULL;
    }
    if (impl->shadow_sample_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->shadow_sample_bind_layout);
        impl->shadow_sample_bind_layout = NULL;
    }
    if (impl->shadow_pass_bind_group) {
        wgpuBindGroupRelease(impl->shadow_pass_bind_group);
        impl->shadow_pass_bind_group = NULL;
    }
    if (impl->shadow_pass_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->shadow_pass_bind_layout);
        impl->shadow_pass_bind_layout = NULL;
    }
    if (impl->shadow_vp_buffer) {
        wgpuBufferRelease(impl->shadow_vp_buffer);
        impl->shadow_vp_buffer = NULL;
    }
    if (impl->shadow_sampler) {
        wgpuSamplerRelease(impl->shadow_sampler);
        impl->shadow_sampler = NULL;
    }
    if (impl->shadow_texture_view) {
        wgpuTextureViewRelease(impl->shadow_texture_view);
        impl->shadow_texture_view = NULL;
    }
    if (impl->shadow_texture) {
        wgpuTextureRelease(impl->shadow_texture);
        impl->shadow_texture = NULL;
    }
    if (impl->shadow_shader_module) {
        wgpuShaderModuleRelease(impl->shadow_shader_module);
        impl->shadow_shader_module = NULL;
    }
}

void flecsEngine_shadow_computeLightVP(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    mat4 out_light_vp)
{
    glm_mat4_identity(out_light_vp);

    if (!view->light || !view->camera) {
        return;
    }

    const FlecsRotation3 *rotation = ecs_get(
        world, view->light, FlecsRotation3);
    if (!rotation) {
        return;
    }

    float pitch = rotation->x;
    float yaw = rotation->y;
    vec3 ray_dir = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };

    float ray_len = glm_vec3_norm(ray_dir);
    if (ray_len <= 1e-6f) {
        return;
    }

    glm_vec3_scale(ray_dir, 1.0f / ray_len, ray_dir);

    /* Get camera position for centering the shadow frustum */
    vec3 cam_pos = {0.0f, 0.0f, 0.0f};
    const FlecsWorldTransform3 *cam_wt = ecs_get(
        world, view->camera, FlecsWorldTransform3);
    if (cam_wt) {
        cam_pos[0] = cam_wt->m[3][0];
        cam_pos[1] = cam_wt->m[3][1];
        cam_pos[2] = cam_wt->m[3][2];
    }

    /* Light position: offset from camera against ray direction */
    float shadow_distance = 200.0f;
    vec3 light_pos;
    glm_vec3_scale(ray_dir, -shadow_distance, light_pos);
    glm_vec3_add(light_pos, cam_pos, light_pos);

    /* Choose an up vector that isn't parallel to the light direction */
    vec3 up = {0.0f, 1.0f, 0.0f};
    if (fabsf(ray_dir[1]) > 0.99f) {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    }

    /* Compute light view matrix */
    mat4 light_view;
    glm_lookat(light_pos, cam_pos, up, light_view);

    /* Orthographic projection covering the shadow area.
     * Use _zo (zero-to-one) variant for WebGPU depth range [0, 1]. */
    float extent = 200.0f;
    float near_plane = 0.1f;
    float far_plane = shadow_distance * 2.0f;

    /* Snap the light-space origin to shadow map texel boundaries to
     * eliminate sub-texel shifts that cause shadow edge shimmer. */
    float texel_size = (2.0f * extent) / (float)FLECS_ENGINE_SHADOW_MAP_SIZE;
    vec4 origin = {0.0f, 0.0f, 0.0f, 1.0f};
    glm_mat4_mulv(light_view, origin, origin);
    origin[0] = floorf(origin[0] / texel_size) * texel_size;
    origin[1] = floorf(origin[1] / texel_size) * texel_size;

    /* Apply the rounding offset back into the light view matrix */
    vec4 snapped;
    glm_mat4_mulv(light_view, (vec4){0.0f, 0.0f, 0.0f, 1.0f}, snapped);
    light_view[3][0] += origin[0] - snapped[0];
    light_view[3][1] += origin[1] - snapped[1];

    mat4 light_proj;
    glm_ortho_rh_zo(
        -extent, extent,
        -extent, extent,
        near_plane, far_plane,
        light_proj);

    glm_mat4_mul(light_proj, light_view, out_light_vp);
}
