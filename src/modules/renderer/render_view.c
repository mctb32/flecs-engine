#include <string.h>

#include "renderer.h"
#include "frustum_cull.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(flecs_engine_background_t);
ECS_COMPONENT_DECLARE(flecs_engine_shadow_params_t);
ECS_COMPONENT_DECLARE(flecs_render_view_effect_t);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->effects, flecs_render_view_effect_t, 0);
    ptr->camera = 0;
    ptr->light = 0;
    ptr->hdri = 0;
    ptr->background = (flecs_engine_background_t){
        .sky_color = {0},
        .ground_color = {0},
        .haze_color = {255, 255, 255, 255},
        .horizon_color = {255, 255, 255, 255},
        .ambient_intensity = 1.0
    };
    ptr->shadow.enabled = true;
    ptr->shadow.map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    ptr->shadow.bias = 0.0005f;
    ptr->shadow.max_range = 100.0f;
    ptr->screen_size_threshold = 0.0f;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    dst->camera = src->camera;
    dst->light = src->light;
    dst->hdri = src->hdri;
    dst->background = src->background;
    dst->shadow = src->shadow;
    dst->screen_size_threshold = src->screen_size_threshold;
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, flecs_render_view_effect_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->effects, flecs_render_view_effect_t);
})

static void flecsEngine_renderView_releaseTargets(
    FlecsRenderViewImpl *impl)
{
    if (!impl) {
        return;
    }

    for (int32_t i = 0; i < impl->effect_target_count; i ++) {
        if (impl->effect_target_views && impl->effect_target_views[i]) {
            wgpuTextureViewRelease(impl->effect_target_views[i]);
            impl->effect_target_views[i] = NULL;
        }
        if (impl->effect_target_textures && impl->effect_target_textures[i]) {
            wgpuTextureRelease(impl->effect_target_textures[i]);
            impl->effect_target_textures[i] = NULL;
        }
    }

    if (impl->effect_target_views) {
        ecs_os_free(impl->effect_target_views);
        impl->effect_target_views = NULL;
    }
    if (impl->effect_target_textures) {
        ecs_os_free(impl->effect_target_textures);
        impl->effect_target_textures = NULL;
    }

    if (impl->passthrough_bind_group) {
        wgpuBindGroupRelease(impl->passthrough_bind_group);
        impl->passthrough_bind_group = NULL;
    }

    impl->effect_target_count = 0;
    impl->effect_target_width = 0;
    impl->effect_target_height = 0;
    impl->effect_target_format = WGPUTextureFormat_Undefined;
}

ECS_DTOR(FlecsRenderViewImpl, ptr, {
    flecsEngine_renderView_releaseTargets(ptr);
})

ECS_MOVE(FlecsRenderViewImpl, dst, src, {
    flecsEngine_renderView_releaseTargets(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_renderView_logErr(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *fmt)
{
    char *name = ecs_get_path(world, entity);
    ecs_err(fmt, name);
    ecs_os_free(name);
}

static void flecsEngine_renderView_writeCamera(
    const ecs_world_t *world,
    FlecsUniform *uniforms,
    ecs_entity_t entity)
{
    uniforms->camera_pos[0] = 0.0f;
    uniforms->camera_pos[1] = 0.0f;
    uniforms->camera_pos[2] = 0.0f;
    uniforms->camera_pos[3] = 1.0f;

    glm_mat4_identity(uniforms->mvp);
    glm_mat4_identity(uniforms->inv_vp);

    const FlecsCameraImpl *camera = ecs_get(
        world, entity, FlecsCameraImpl);
    if (!camera) {
        flecsEngine_renderView_logErr(world, entity,
            "invalid camera '%s' in view");
        return;
    }

    glm_mat4_copy((vec4*)camera->mvp, uniforms->mvp);
    glm_mat4_inv(uniforms->mvp, uniforms->inv_vp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, entity, FlecsWorldTransform3);
    if (camera_transform) {
        uniforms->camera_pos[0] = camera_transform->m[3][0];
        uniforms->camera_pos[1] = camera_transform->m[3][1];
        uniforms->camera_pos[2] = camera_transform->m[3][2];
    }
}

static void flecsEngine_renderView_writeLight(
    const ecs_world_t *world,
    FlecsUniform *uniforms,
    ecs_entity_t entity)
{
    uniforms->light_color[0] = 1.0f;
    uniforms->light_color[1] = 1.0f;
    uniforms->light_color[2] = 1.0f;
    uniforms->light_color[3] = 1.0f;

    const FlecsDirectionalLight *light = ecs_get(
        world, entity, FlecsDirectionalLight);
    if (!light) {
        flecsEngine_renderView_logErr(world, entity,
            "invalid directional light '%s'");
        return;
    }

    const FlecsRotation3 *rotation = ecs_get(world, entity, FlecsRotation3);
    if (!rotation) {
        flecsEngine_renderView_logErr(world, entity,
            "directional light '%s' is missing Rotation3");
        return;
    }

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rotation, ray_dir)) {
        flecsEngine_renderView_logErr(world, entity,
            "directional light '%s' has invalid Rotation3");
        return;
    }

    uniforms->light_ray_dir[0] = ray_dir[0];
    uniforms->light_ray_dir[1] = ray_dir[1];
    uniforms->light_ray_dir[2] = ray_dir[2];

    FlecsRgba rgb = {255, 255, 255, 255};
    const FlecsRgba *light_rgb = ecs_get(world, entity, FlecsRgba);
    if (light_rgb) {
        rgb = *light_rgb;
    }

    uniforms->light_color[0] =
        flecsEngine_colorChannelToFloat(rgb.r) * light->intensity;
    uniforms->light_color[1] =
        flecsEngine_colorChannelToFloat(rgb.g) * light->intensity;
    uniforms->light_color[2] =
        flecsEngine_colorChannelToFloat(rgb.b) * light->intensity;
}

static void flecsEngine_renderView_writeFrameUniforms(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    if (!engine->frame_uniform_buffer) {
        return;
    }

    FlecsUniform uniforms = {0};
    uniforms.camera_pos[3] = 1.0f;

    if (view->camera) {
        flecsEngine_renderView_writeCamera(world, &uniforms, view->camera);
    }

    if (view->light) {
        flecsEngine_renderView_writeLight(world, &uniforms, view->light);
    }

    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        glm_mat4_copy((vec4*)engine->shadow.current_light_vp[i],
            uniforms.light_vp[i]);
    }

    memcpy(uniforms.cascade_splits, engine->shadow.cascade_splits,
        sizeof(float) * FLECS_ENGINE_SHADOW_CASCADE_COUNT);

    float bias = view->shadow.bias;
    if (bias <= 0) { bias = 0.0005f; }
    uniforms.shadow_info[1] = bias;

    uniforms.ambient_light[3] = view->background.ambient_intensity;

    uniforms.sky_color[0] = flecsEngine_colorChannelToFloat(view->background.sky_color.r);
    uniforms.sky_color[1] = flecsEngine_colorChannelToFloat(view->background.sky_color.g);
    uniforms.sky_color[2] = flecsEngine_colorChannelToFloat(view->background.sky_color.b);
    uniforms.sky_color[3] = flecsEngine_colorChannelToFloat(view->background.sky_color.a);

    engine->camera_pos[0] = uniforms.camera_pos[0];
    engine->camera_pos[1] = uniforms.camera_pos[1];
    engine->camera_pos[2] = uniforms.camera_pos[2];

    wgpuQueueWriteBuffer(
        engine->queue,
        engine->frame_uniform_buffer,
        0,
        &uniforms,
        sizeof(FlecsUniform));
}

static bool flecsEngine_renderView_createTargets(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count,
    WGPUTextureFormat format)
{
    uint32_t width = (uint32_t)engine->actual_width;
    uint32_t height = (uint32_t)engine->actual_height;

    impl->effect_target_textures = ecs_os_calloc_n(WGPUTexture, effect_count);
    impl->effect_target_views = ecs_os_calloc_n(WGPUTextureView, effect_count);
    if (!impl->effect_target_textures || !impl->effect_target_views) {
        goto error;
    }

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    for (int32_t i = 0; i < effect_count; i ++) {
        impl->effect_target_textures[i] = wgpuDeviceCreateTexture(
            engine->device, &color_desc);
        if (!impl->effect_target_textures[i]) {
            goto error;
        }

        impl->effect_target_views[i] = wgpuTextureCreateView(
            impl->effect_target_textures[i], NULL);
        if (!impl->effect_target_views[i]) {
            goto error;
        }
    }

    impl->effect_target_count = effect_count;
    impl->effect_target_width = width;
    impl->effect_target_height = height;
    impl->effect_target_format = format;

    return true;
error:
    flecsEngine_renderView_releaseTargets(impl);
    return false;
}

static int flecsEngine_renderView_ensureTargets(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count)
{
    if (effect_count <= 0) {
        return 0;
    }

    uint32_t width = (uint32_t)engine->actual_width;
    uint32_t height = (uint32_t)engine->actual_height;
    WGPUTextureFormat surface_format = engine->surface_config.format;
    WGPUTextureFormat desired_format = flecsEngine_getHdrFormat(engine);

    if (impl->effect_target_count >= effect_count) {
        if (impl->effect_target_textures && impl->effect_target_views) {
            if (impl->effect_target_width == width &&
                impl->effect_target_height == height &&
                impl->effect_target_format == desired_format)
            {
                return 0;
            }
        }
    }

    flecsEngine_renderView_releaseTargets(impl);

    if (flecsEngine_renderView_createTargets(
        engine, impl, effect_count, desired_format))
    {
        return 0;
    }

    if (desired_format != surface_format &&
        flecsEngine_renderView_createTargets(
            engine, impl, effect_count, surface_format))
    {
        engine->hdr_color_format = surface_format;
        ecs_warn("falling back to LDR targets: HDR format unavailable");
        return 0;
    }

    return -1;
}

static void flecsEngine_renderView_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView view_texture)
{
    FLECS_TRACY_ZONE_BEGIN("RenderView");
    engine->last_pipeline = NULL;

    int32_t effect_count = ecs_vec_count(&view->effects);
    int32_t target_count = effect_count > 0 ? effect_count : 1;
    if (effect_count > 0 && engine->resolution_scale > 1) {
        target_count = effect_count + 1;
    }
    if (flecsEngine_renderView_ensureTargets(engine, impl, target_count))
    {
        ecs_err("failed to allocate effect render targets");
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (view->shadow.enabled) {
        if (flecsEngine_shadow_ensureSize(
            world, engine, (uint32_t)view->shadow.map_size))
        {
            ecs_err("failed to resize shadow maps");
        }

        flecsEngine_renderView_renderShadow(
            world, view_entity, engine, view, encoder);
    } else {
        for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
            memset(engine->shadow.current_light_vp[i], 0, sizeof(mat4));
            engine->shadow.cascade_splits[i] = 0.0f;
        }
    }

    flecsEngine_setupLights(world, engine);
    flecsEngine_cluster_build(world, engine, view);

    /* Populate the engine-global frame uniform buffer for this view.
     * Written once here instead of once per pipeline change per batch. */
    flecsEngine_renderView_writeFrameUniforms(world, engine, view);

    flecsEngine_renderView_renderBatches(
        world, view_entity, engine, view, impl, encoder);

    if (engine->sample_count > 1) {
        flecsEngine_depthResolve(engine, encoder);
    }

    flecsEngine_renderView_renderEffects(
        world, view_entity, engine, view, impl, view_texture, encoder);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractView");
    (void)impl;

    /* Rebuild the sky background HDRI if the view's background colors
     * changed since the last frame. */
    if (!view->hdri) {
        flecsEngine_ibl_ensureSkyBackground(
            world, engine, &view->background);
    }

    /* Extract frustum planes from camera view-projection matrix */
    engine->frustum_valid = false;
    engine->shadow_frustum_valid = false;

    if (view->camera) {
        const FlecsCameraImpl *camera = ecs_get(
            world, view->camera, FlecsCameraImpl);
        if (camera) {
            flecsEngine_frustum_extractPlanes(
                camera->mvp,
                engine->frustum_planes);
            engine->frustum_valid = true;

            if (view->shadow.enabled && view->shadow.max_range > 0.0f) {
                const FlecsCamera *cam = ecs_get(
                    world, view->camera, FlecsCamera);
                if (cam) {
                    mat4 shadow_proj;
                    glm_perspective(
                        cam->fov,
                        cam->aspect_ratio > 0.0f ? cam->aspect_ratio : 1.0f,
                        cam->near_,
                        view->shadow.max_range,
                        shadow_proj);

                    mat4 shadow_vp;
                    glm_mat4_mul(shadow_proj, (vec4*)camera->view, shadow_vp);

                    flecsEngine_frustum_extractPlanes(
                        shadow_vp,
                        engine->shadow_frustum_planes);
                    engine->shadow_frustum_valid = true;
                }
            }
        }
    }

    /* Compute screen-size culling factor from camera FOV and viewport */
    engine->screen_cull_valid = false;
    if (view->screen_size_threshold > 0.0f && view->camera) {
        const FlecsCamera *cam = ecs_get(
            world, view->camera, FlecsCamera);
        if (cam && !cam->orthographic && cam->fov > 0.0f) {
            float half_tan = tanf(cam->fov * 0.5f);
            if (half_tan > 1e-6f) {
                float vh = (float)engine->actual_height;
                float f = vh / half_tan;
                engine->screen_cull_factor = f * f;
                engine->screen_cull_threshold = view->screen_size_threshold;
                engine->screen_cull_valid = true;
            }
        }
    }

    flecsEngine_renderView_extractBatches(world, view_entity, engine, view);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_extractShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractShadowView");

    /* Compute cascade light VP matrices and per-cascade frustum planes. */
    engine->cascade_frustum_valid = false;
    if (view->shadow.enabled && view->light) {
        flecsEngine_shadow_computeCascades(
            world, view, engine->shadow.map_size,
            view->shadow.max_range,
            engine->shadow.current_light_vp,
            engine->shadow.cascade_splits);

        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            flecsEngine_frustum_extractPlanes(
                engine->shadow.current_light_vp[c],
                engine->cascade_frustum_planes[c]);
        }
        engine->cascade_frustum_valid = true;
    }

    flecsEngine_renderView_extractShadowBatches(
        world, view_entity, engine, view);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_extractAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_extract(
                world,
                engine,
                it.entities[i],
                &views[i],
                &viewImpls[i]);
        }
    }
}

void flecsEngine_renderView_extractShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_extractShadow(
                world,
                engine,
                it.entities[i],
                &views[i]);
        }
    }
}

void flecsEngine_renderView_uploadAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_uploadBatches(
                world, it.entities[i], engine, &views[i]);
        }
    }
}

void flecsEngine_renderView_uploadShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_uploadShadowBatches(
                world, it.entities[i], engine, &views[i]);
        }
    }
}

void flecsEngine_renderView_renderAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_render(world, engine, it.entities[i], 
                &views[i], &viewImpls[i],
                encoder, view_texture);
        }
    }
}

void flecsEngine_renderView_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, flecs_engine_background_t);
    ECS_COMPONENT_DEFINE(world, flecs_engine_shadow_params_t);
    ECS_COMPONENT_DEFINE(world, flecs_render_view_effect_t);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsRenderViewImpl);

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_set_hooks(world, FlecsRenderViewImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderViewImpl),
        .dtor = ecs_dtor(FlecsRenderViewImpl)
    });

    ecs_add_pair(world, ecs_id(FlecsRenderView), EcsWith, ecs_id(FlecsRenderViewImpl));

    ecs_struct(world, {
        .entity = ecs_id(flecs_engine_background_t),
        .members = {
            { .name = "sky_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "ground_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "haze_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "horizon_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "ambient_intensity", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_engine_shadow_params_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "map_size", .type = ecs_id(ecs_i32_t) },
            { .name = "bias", .type = ecs_id(ecs_f32_t) },
            { .name = "max_range", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_render_view_effect_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "effect", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_entity_t vec_view_effect = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecViewEffect",
        }),
        .type = ecs_id(flecs_render_view_effect_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderView),
        .members = {
            { .name = "camera", .type = ecs_id(ecs_entity_t) },
            { .name = "light", .type = ecs_id(ecs_entity_t) },
            { .name = "hdri", .type = ecs_id(ecs_entity_t) },
            { .name = "screen_size_threshold", .type = ecs_id(ecs_f32_t) },
            { .name = "background", .type = ecs_id(flecs_engine_background_t) },
            { .name = "shadow", .type = ecs_id(flecs_engine_shadow_params_t) },
            { .name = "effects", .type = vec_view_effect }
        }
    });
}
