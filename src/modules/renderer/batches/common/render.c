#include "common.h"

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    WGPUBindGroup group = (buf->flags & FLECS_BATCH_OWNS_MATERIAL)
        ? buf->buffers.gpu_material_bind_group
        : flecsEngine_materialBind_ensure(engine);

    if (group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
    }
}

void flecsEngine_batch_bindInstanceGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    if (buf->buffers.gpu_instance_bind_group) {
        wgpuRenderPassEncoderSetBindGroup(
            pass, 3, buf->buffers.gpu_instance_bind_group, 0, NULL);
    }
}

void flecsEngine_batch_bindInstanceGroupShadow(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    if (buf->buffers.gpu_instance_bind_group) {
        wgpuRenderPassEncoderSetBindGroup(
            pass, 1, buf->buffers.gpu_instance_bind_group, 0, NULL);
    }
}

static void flecsEngine_batch_group_drawView(
    const flecsEngine_batch_group_t *ctx,
    const WGPURenderPassEncoder pass,
    int view_idx,
    WGPUBuffer vertex_buffer)
{
    int32_t src_count = ctx->view.count;
    if (!src_count || ctx->view.group_idx < 0) {
        return;
    }

    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || !buf->buffers.gpu_visible_slots ||
        !buf->buffers.gpu_indirect_args)
    {
        return;
    }

    if (!vertex_buffer || !ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        return;
    }

    int32_t capacity = buf->buffers.capacity;
    int32_t group_count = buf->buffers.group_count;

    uint64_t slot_offset =
        ((uint64_t)view_idx * (uint64_t)capacity
            + (uint64_t)ctx->view.offset) * sizeof(uint32_t);
    uint64_t slot_size = (uint64_t)src_count * sizeof(uint32_t);

    uint64_t args_offset =
        ((uint64_t)view_idx * (uint64_t)group_count
            + (uint64_t)ctx->view.group_idx)
        * sizeof(flecsEngine_gpuDrawArgs_t);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->buffers.gpu_visible_slots, slot_offset, slot_size);

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexedIndirect(
        pass, buf->buffers.gpu_indirect_args, args_offset);
}

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    flecsEngine_batch_group_drawView(
        ctx, pass, 0, ctx->mesh.vertex_uv_buffer);
}

void flecsEngine_batch_group_drawDepthPrepass(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    flecsEngine_batch_group_drawView(
        ctx, pass, 0, ctx->mesh.vertex_buffer);
}

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    int cascade = view_impl->shadow.current_cascade;
    flecsEngine_batch_group_drawView(
        ctx, pass, 1 + cascade, ctx->mesh.vertex_buffer);
}

flecsEngine_batch_t* flecsEngine_batch_getCullBuf(
    const FlecsRenderBatch *batch)
{
    /* Polymorphic over batch ctx type. Mesh batches store the
     * flecsEngine_batch_t directly as ctx; primitive batches embed it. */
    if (!batch || !batch->ctx) {
        return NULL;
    }
    if (batch->get_cull_buf) {
        return batch->get_cull_buf(batch);
    }
    return NULL;
}
