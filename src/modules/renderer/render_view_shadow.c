#include "renderer.h"
#include "frustum_cull.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static void flecsEngine_renderView_extractShadowBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    (void)view;
    FLECS_TRACY_ZONE_BEGIN("ExtractShadowBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatchSet_extractShadow(world, engine, batch_set);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_uploadShadowBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    (void)view;
    FLECS_TRACY_ZONE_BEGIN("UploadShadowBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatchSet_uploadShadow(world, engine, batch_set);
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

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("ShadowPass");
    if (!engine->shadow.texture_view || !view->light) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        wgpuQueueWriteBuffer(
            engine->queue,
            engine->shadow.vp_buffers[c],
            0,
            engine->shadow.current_light_vp[c],
            sizeof(mat4));
    }

    engine->shadow.in_pass = true;

    /* Render each cascade into its own texture array layer */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (!engine->shadow.layer_views[c]) {
            continue;
        }

        engine->shadow.current_cascade = c;

        /* Begin shadow depth-only render pass for this cascade layer */
        WGPURenderPassDepthStencilAttachment depth_attachment = {
            .view = engine->shadow.layer_views[c],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 1.0f,
            .depthReadOnly = false,
            .stencilLoadOp = WGPULoadOp_Undefined,
            .stencilStoreOp = WGPUStoreOp_Undefined,
            .stencilClearValue = 0,
            .stencilReadOnly = true
        };

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 0,
            .colorAttachments = NULL,
            .depthStencilAttachment = &depth_attachment
        };

        WGPURenderPassEncoder shadow_pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);

        wgpuRenderPassEncoderSetViewport(
            shadow_pass,
            0.0f, 0.0f,
            (float)engine->shadow.map_size,
            (float)engine->shadow.map_size,
            0.0f, 1.0f);

        engine->last_pipeline = NULL;

        flecsEngine_renderBatchSet_renderShadow(
            world, engine, batch_set, shadow_pass);

        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }

    engine->shadow.in_pass = false;
    FLECS_TRACY_ZONE_END;
}
