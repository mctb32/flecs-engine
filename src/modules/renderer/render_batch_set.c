#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static WGPURenderPassEncoder flecsEngine_renderBatch_beginPass(
    const FlecsEngineImpl *impl,
    const FlecsRenderView *view,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op,
    WGPULoadOp depth_load_op)
{
    WGPUColor sky_color = {
        .r = (double)flecsEngine_colorChannelToFloat(view->background.sky_color.r),
        .g = (double)flecsEngine_colorChannelToFloat(view->background.sky_color.g),
        .b = (double)flecsEngine_colorChannelToFloat(view->background.sky_color.b),
        .a = (double)flecsEngine_colorChannelToFloat(view->background.sky_color.a)
    };

    bool msaa = impl->sample_count > 1 && impl->depth.msaa_color_texture_view;

    WGPURenderPassColorAttachment color_attachment = {
        .view = msaa ? impl->depth.msaa_color_texture_view : color_view,
        .resolveTarget = msaa ? color_view : NULL,
        WGPU_DEPTH_SLICE
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = sky_color
    };

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = msaa ? impl->depth.msaa_depth_texture_view : impl->depth.depth_texture_view,
        .depthLoadOp = depth_load_op,
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

typedef void (*flecsEngine_batchSetVisitor_t)(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx);

static void flecsEngine_renderBatch_visitSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set,
    flecsEngine_batchSetVisitor_t visitor,
    void *ctx)
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
            flecsEngine_renderBatch_visitSet(
                world, engine, nested_batch_set, visitor, ctx);
            continue;
        }

        visitor(world, engine, batch_entity, ctx);
    }
}

typedef struct {
    const WGPURenderPassEncoder pass;
    const FlecsRenderView *view;
    int phase; /* 0 = all, 1 = opaque only, 2 = post-snapshot only */
} flecsEngine_renderVisitorCtx_t;

static void flecsEngine_renderBatch_transmissionCheckVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    (void)engine;
    bool *has_transmission = ctx;
    if (*has_transmission) {
        return;
    }

    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (batch && batch->needs_transmission && ecs_query_is_true(batch->query)) {
        *has_transmission = true;
    }
}

static void flecsEngine_renderBatch_renderVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    flecsEngine_renderVisitorCtx_t *render_ctx = ctx;

    /* Phase filtering: skip batches that don't belong to this phase */
    if (render_ctx->phase != 0) {
        const FlecsRenderBatch *batch = ecs_get(
            world, batch_entity, FlecsRenderBatch);
        if (batch) {
            bool after = batch->render_after_snapshot;
            if (render_ctx->phase == 1 && after) return;  /* skip post-snapshot */
            if (render_ctx->phase == 2 && !after) return; /* skip opaques */
        }
    }

    flecsEngine_renderBatch_render(
        world, engine, render_ctx->pass, render_ctx->view, batch_entity);
}

static void flecsEngine_renderBatch_extractVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    (void)ctx;
    flecsEngine_renderBatch_extract(world, engine, batch_entity);
}

static void flecsEngine_renderBatch_shadowExtractVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    (void)ctx;
    flecsEngine_renderBatch_extractShadow(world, engine, batch_entity);
}

static void flecsEngine_renderBatch_shadowVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    flecsEngine_renderVisitorCtx_t *render_ctx = ctx;
    flecsEngine_renderBatch_renderShadow(
        world, engine, render_ctx->pass, batch_entity);
}

void flecsEngine_renderView_extractBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractBatches");
    (void)view;

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatch_visitSet(
        world, engine, batch_set, flecsEngine_renderBatch_extractVisitor, NULL);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_extractShadowBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractShadowBatches");
    (void)view;

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatch_visitSet(
        world, engine, batch_set,
        flecsEngine_renderBatch_shadowExtractVisitor, NULL);
    FLECS_TRACY_ZONE_END;
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

    /* Cascade light VP matrices and frustum planes were already computed
     * during the extract phase. Upload VP matrices to GPU buffers before
     * encoding any render passes. */
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

        flecsEngine_renderVisitorCtx_t shadow_ctx = {
            .pass = shadow_pass
        };

        flecsEngine_renderBatch_visitSet(
            world, engine, batch_set,
            flecsEngine_renderBatch_shadowVisitor, &shadow_ctx);

        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }

    engine->shadow.in_pass = false;
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *viewImpl,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("RenderBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    WGPUTextureView batch_target = viewImpl->effect_target_views[0];

    /* Check if any transmission batch has matching entities. If not, we can
     * skip the expensive opaque snapshot + prefilter and render everything
     * in a single pass. */
    bool has_transmission = false;
    flecsEngine_renderBatch_visitSet(
        world, engine, batch_set,
        flecsEngine_renderBatch_transmissionCheckVisitor, &has_transmission);

    if (has_transmission) {
        /* Pass 1: render opaque batches (render_after_snapshot = false) */
        {
            WGPURenderPassEncoder pass = flecsEngine_renderBatch_beginPass(
                engine, view, encoder, batch_target,
                WGPULoadOp_Clear, WGPULoadOp_Clear);
            engine->last_pipeline = NULL;

            flecsEngine_renderVisitorCtx_t ctx = {
                .pass = pass,
                .view = view,
                .phase = 1
            };
            flecsEngine_renderBatch_visitSet(
                world, engine, batch_set,
                flecsEngine_renderBatch_renderVisitor, &ctx);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        /* Snapshot the opaque result for transmission sampling */
        {
            WGPUTexture src_tex = viewImpl->effect_target_textures[0];
            if (src_tex) {
                flecsEngine_transmission_updateSnapshot(
                    engine, encoder, src_tex,
                    viewImpl->effect_target_width,
                    viewImpl->effect_target_height);
            }
        }

        /* Pass 2: render post-snapshot batches (transmission + transparent).
         * LoadOp_Load preserves both color and depth from pass 1. */
        {
            WGPURenderPassEncoder pass = flecsEngine_renderBatch_beginPass(
                engine, view, encoder, batch_target,
                WGPULoadOp_Load, WGPULoadOp_Load);
            engine->last_pipeline = NULL;

            flecsEngine_renderVisitorCtx_t ctx = {
                .pass = pass,
                .view = view,
                .phase = 2
            };
            flecsEngine_renderBatch_visitSet(
                world, engine, batch_set,
                flecsEngine_renderBatch_renderVisitor, &ctx);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    } else {
        /* No transmissive materials — single pass, no snapshot needed */
        WGPURenderPassEncoder pass = flecsEngine_renderBatch_beginPass(
            engine, view, encoder, batch_target,
            WGPULoadOp_Clear, WGPULoadOp_Clear);
        engine->last_pipeline = NULL;

        flecsEngine_renderVisitorCtx_t ctx = {
            .pass = pass,
            .view = view,
            .phase = 0
        };
        flecsEngine_renderBatch_visitSet(
            world, engine, batch_set,
            flecsEngine_renderBatch_renderVisitor, &ctx);

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    FLECS_TRACY_ZONE_END;
}
