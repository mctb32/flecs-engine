#include "render_batch.h"

void flecsEngine_batch_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx)
{
    (void)engine;

    if (!ctx->count) {
        return;
    }

    const flecsEngine_batch_buffers_t *buf = ctx->buffers;
    if (!buf) {
        return;
    }

    WGPUBuffer vertex_buffer = ctx->vertex_buffer;
    if (!vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->offset * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)ctx->count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->instance_transform, transform_offset, transform_size);

    if (buf->owns_material_data) {
        uint64_t color_offset =
            (uint64_t)ctx->offset * sizeof(FlecsRgba);
        uint64_t color_size =
            (uint64_t)ctx->count * sizeof(FlecsRgba);
        uint64_t pbr_offset =
            (uint64_t)ctx->offset * sizeof(FlecsPbrMaterial);
        uint64_t pbr_size =
            (uint64_t)ctx->count * sizeof(FlecsPbrMaterial);
        uint64_t emissive_offset =
            (uint64_t)ctx->offset * sizeof(FlecsEmissive);
        uint64_t emissive_size =
            (uint64_t)ctx->count * sizeof(FlecsEmissive);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_color, color_offset, color_size);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 3, buf->instance_pbr, pbr_offset, pbr_size);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 4, buf->instance_emissive, emissive_offset, emissive_size);

        if (buf->owns_transmission_data && buf->instance_transmission) {
            uint64_t trans_offset =
                (uint64_t)ctx->offset * sizeof(FlecsTransmission);
            uint64_t trans_size =
                (uint64_t)ctx->count * sizeof(FlecsTransmission);
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 5, buf->instance_transmission,
                trans_offset, trans_size);
        }
    } else {
        uint64_t matid_offset =
            (uint64_t)ctx->offset * sizeof(FlecsMaterialId);
        uint64_t matid_size =
            (uint64_t)ctx->count * sizeof(FlecsMaterialId);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_material_id, matid_offset, matid_size);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batch_drawShadow(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx)
{
    int cascade = engine->shadow.current_cascade;
    int32_t count = ctx->shadow_count[cascade];
    if (!count) {
        return;
    }

    const flecsEngine_batch_buffers_t *buf = ctx->buffers;
    if (!buf || !buf->shadow_transforms[cascade]) {
        return;
    }

    WGPUBuffer vertex_buffer = ctx->vertex_buffer;
    if (!vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->shadow_offset[cascade] * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->shadow_transforms[cascade],
        transform_offset, transform_size);

    if (buf->owns_material_data) {
        if (buf->instance_color) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 2, buf->instance_color, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_pbr) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 3, buf->instance_pbr, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_emissive) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 4, buf->instance_emissive, 0, WGPU_WHOLE_SIZE);
        }
    } else if (buf->instance_material_id) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_material_id, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, count, 0, 0, 0);
}
