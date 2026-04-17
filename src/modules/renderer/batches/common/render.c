#include "common.h"

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    WGPUBindGroup group = (buf->flags & FLECS_BATCH_OWNS_MATERIAL)
        ? buf->buffers.gpu_material_bind_group
        : flecsEngine_materialBind_ensureScene(engine);

    if (group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
    }
}

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;

    if (!ctx->view.count) {
        return;
    }

    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf) {
        return;
    }

    if (!ctx->mesh.vertex_uv_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->view.offset * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)ctx->view.count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_uv_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->buffers.gpu_transforms, transform_offset, transform_size);

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        /* Identity buffer provides per-instance material_id = global buffer
         * index, so each group's instances index into their own slice of
         * the shared cpu_materials array. */
        int32_t identity_end = ctx->view.offset + ctx->view.count;
        WGPUBuffer identity =
            flecsEngine_defaultAttrCache_getMaterialIdIdentityBuffer(
                (FlecsEngineImpl*)engine, identity_end);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2, identity,
            (uint64_t)ctx->view.offset * sizeof(FlecsMaterialId),
            (uint64_t)ctx->view.count * sizeof(FlecsMaterialId));
    } else {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
            buf->buffers.gpu_material_ids,
            (uint64_t)ctx->view.offset * sizeof(FlecsMaterialId),
            (uint64_t)ctx->view.count * sizeof(FlecsMaterialId));
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->view.count, 0, 0, 0);
}

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    int cascade = view_impl->shadow.current_cascade;
    int32_t count = ctx->view.shadow_count[cascade];
    if (!count) {
        return;
    }

    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || !buf->buffers.gpu_shadow_transforms[cascade]) {
        return;
    }

    if (!ctx->mesh.vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->view.shadow_offset[cascade] * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->buffers.gpu_shadow_transforms[cascade],
        transform_offset, transform_size);

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        WGPUBuffer identity =
            flecsEngine_defaultAttrCache_getMaterialIdIdentityBuffer(
                (FlecsEngineImpl*)engine, count);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2, identity, 0,
            (uint64_t)count * sizeof(FlecsMaterialId));
    } else if (buf->buffers.gpu_material_ids) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
            buf->buffers.gpu_material_ids, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, count, 0, 0, 0);
}
