#include "renderer.h"
#include "flecs_engine.h"

static WGPURenderPassEncoder flecsEngine_renderBatch_beginPass(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPUColor clear_color = flecsEngine_getClearColor(impl);

    WGPURenderPassColorAttachment color_attachment = {
        .view = color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clear_color
    };

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = impl->depth_texture_view,
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
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

static void flecsEngine_renderBatch_extractSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngine_renderBatch_extractSet(
                world,
                engine,
                nested_batch_set);
            continue;
        }

        flecsEngine_renderBatch_extract(world, engine, batch_entity);
    }
}

static void flecsEngine_renderBatch_renderSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngine_renderBatch_renderSet(
                world,
                engine,
                pass,
                view,
                nested_batch_set);
            continue;
        }

        flecsEngine_renderBatch_render(world, engine, pass, view, batch_entity);
    }
}

void flecsEngine_renderView_extractBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    (void)view;

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatch_extractSet(world, engine, batch_set);
}

static void flecsEngine_renderBatch_renderSetShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngine_renderBatch_renderSetShadow(
                world,
                engine,
                pass,
                nested_batch_set);
            continue;
        }

        flecsEngine_renderBatch_renderShadow(
            world, engine, pass, batch_entity);
    }
}

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    WGPUCommandEncoder encoder)
{
    if (!engine->shadow_texture_view || !view->light) {
        return;
    }

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    /* Compute all cascade light VP matrices and split distances */
    flecsEngine_shadow_computeCascades(
        world, view, engine->shadow_map_size,
        engine->current_light_vp, engine->cascade_splits);

    /* Upload all cascade VP matrices to their own buffers upfront.
     * This must happen before encoding any render passes because
     * wgpuQueueWriteBuffer calls resolve before command buffer execution. */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        wgpuQueueWriteBuffer(
            engine->queue,
            engine->shadow_vp_buffers[c],
            0,
            engine->current_light_vp[c],
            sizeof(mat4));
    }

    /* Render each cascade into its own texture array layer */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (!engine->shadow_layer_views[c]) {
            continue;
        }

        engine->current_shadow_cascade = c;

        /* Begin shadow depth-only render pass for this cascade layer */
        WGPURenderPassDepthStencilAttachment depth_attachment = {
            .view = engine->shadow_layer_views[c],
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

        engine->last_pipeline = NULL;

        flecsEngine_renderBatch_renderSetShadow(
            world, engine, shadow_pass, batch_set);

        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }
}

void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    WGPURenderPassEncoder batch_pass = flecsEngine_renderBatch_beginPass(
        engine,
        encoder,
        viewImpl->effect_target_views[0],
        WGPULoadOp_Clear);

    /* Always set pipeline/uniforms for first batch in view */
    engine->last_pipeline = NULL;

    flecsEngine_renderBatch_renderSet(
        world,
        engine,
        batch_pass,
        view,
        batch_set);

    wgpuRenderPassEncoderEnd(batch_pass);
    wgpuRenderPassEncoderRelease(batch_pass);
}
