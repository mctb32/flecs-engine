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

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;

    int32_t visible = ctx->view.visible_count;
    if (!visible) {
        return;
    }

    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || !buf->buffers.gpu_visible_slots) {
        return;
    }

    if (!ctx->mesh.vertex_uv_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t slot_offset =
        (uint64_t)ctx->view.visible_offset * sizeof(uint32_t);
    uint64_t slot_size =
        (uint64_t)visible * sizeof(uint32_t);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_uv_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->buffers.gpu_visible_slots, slot_offset, slot_size);

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, visible, 0, 0, 0);
}

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;

    int cascade = view_impl->shadow.current_cascade;
    int32_t visible = ctx->view.shadow_visible_count[cascade];
    if (!visible) {
        return;
    }

    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || !buf->buffers.gpu_shadow_visible_slots[cascade]) {
        return;
    }

    if (!ctx->mesh.vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t slot_offset =
        (uint64_t)ctx->view.shadow_visible_offset[cascade] * sizeof(uint32_t);
    uint64_t slot_size =
        (uint64_t)visible * sizeof(uint32_t);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->buffers.gpu_shadow_visible_slots[cascade],
        slot_offset, slot_size);

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, visible, 0, 0, 0);
}
