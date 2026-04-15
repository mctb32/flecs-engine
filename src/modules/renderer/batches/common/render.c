#include "common.h"

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_buffers_t *buf)
{
    if (!buf->use_material_storage) {
        return;
    }

    WGPUBindGroup group;
    if (buf->owns_material_data) {
        group = buf->material_bind_group;
    } else {
        group = flecsEngine_materialBind_ensureScene(engine);
    }

    if (group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
    }
}

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

    if (buf->owns_material_data && buf->use_material_storage) {
        WGPUBuffer identity =
            flecsEngine_defaultAttrCache_getMaterialIdIdentityBuffer(
                (FlecsEngineImpl*)engine, ctx->count);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2, identity, 0,
            (uint64_t)ctx->count * sizeof(FlecsMaterialId));
    } else if (!buf->owns_material_data) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
            buf->instance_material_id,
            (uint64_t)ctx->offset * sizeof(FlecsMaterialId),
            (uint64_t)ctx->count * sizeof(FlecsMaterialId));
    } else {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
            buf->instance_color,
            (uint64_t)ctx->offset * sizeof(FlecsRgba),
            (uint64_t)ctx->count * sizeof(FlecsRgba));
        wgpuRenderPassEncoderSetVertexBuffer(pass, 3,
            buf->instance_pbr,
            (uint64_t)ctx->offset * sizeof(FlecsPbrMaterial),
            (uint64_t)ctx->count * sizeof(FlecsPbrMaterial));
        wgpuRenderPassEncoderSetVertexBuffer(pass, 4,
            buf->instance_emissive,
            (uint64_t)ctx->offset * sizeof(FlecsEmissive),
            (uint64_t)ctx->count * sizeof(FlecsEmissive));

        if (buf->owns_transmission_data && buf->instance_transmission) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 5,
                buf->instance_transmission,
                (uint64_t)ctx->offset * sizeof(FlecsTransmission),
                (uint64_t)ctx->count * sizeof(FlecsTransmission));
        }
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

    if (buf->owns_material_data && buf->use_material_storage) {
        WGPUBuffer identity =
            flecsEngine_defaultAttrCache_getMaterialIdIdentityBuffer(
                (FlecsEngineImpl*)engine, count);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2, identity, 0,
            (uint64_t)count * sizeof(FlecsMaterialId));
    } else if (!buf->owns_material_data && buf->instance_material_id) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
            buf->instance_material_id, 0, WGPU_WHOLE_SIZE);
    } else {
        if (buf->instance_color) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
                buf->instance_color, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_pbr) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 3,
                buf->instance_pbr, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_emissive) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 4,
                buf->instance_emissive, 0, WGPU_WHOLE_SIZE);
        }
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, count, 0, 0, 0);
}
