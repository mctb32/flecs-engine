#include "renderer.h"
#include "flecs_engine.h"

void flecsEngine_transmission_updateSnapshot(
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    WGPUTexture src_texture,
    uint32_t width,
    uint32_t height)
{
    if (!width || !height) {
        return;
    }

    /* (Re)create snapshot texture if dimensions changed */
    if (engine->opaque_snapshot_width != width ||
        engine->opaque_snapshot_height != height)
    {
        if (engine->opaque_snapshot_view) {
            wgpuTextureViewRelease(engine->opaque_snapshot_view);
            engine->opaque_snapshot_view = NULL;
        }
        if (engine->opaque_snapshot) {
            wgpuTextureRelease(engine->opaque_snapshot);
            engine->opaque_snapshot = NULL;
        }

        engine->opaque_snapshot = wgpuDeviceCreateTexture(
            engine->device, &(WGPUTextureDescriptor){
                .usage = WGPUTextureUsage_TextureBinding
                       | WGPUTextureUsage_CopyDst,
                .dimension = WGPUTextureDimension_2D,
                .size = { width, height, 1 },
                .format = engine->hdr_color_format,
                .mipLevelCount = 1,
                .sampleCount = 1
            });
        if (!engine->opaque_snapshot) {
            return;
        }

        engine->opaque_snapshot_view = wgpuTextureCreateView(
            engine->opaque_snapshot, &(WGPUTextureViewDescriptor){
                .format = engine->hdr_color_format,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = 0,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1
            });

        engine->opaque_snapshot_width = width;
        engine->opaque_snapshot_height = height;

        /* Rebuild the globals bind group so it picks up the new view */
        engine->scene_bind_version++;
    }

    /* Copy the resolved color target to the snapshot */
    WGPUTexelCopyTextureInfo src = {
        .texture = src_texture,
        .mipLevel = 0,
        .origin = { 0, 0, 0 }
    };
    WGPUTexelCopyTextureInfo dst = {
        .texture = engine->opaque_snapshot,
        .mipLevel = 0,
        .origin = { 0, 0, 0 }
    };
    WGPUExtent3D size = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &size);
}

void flecsEngine_transmission_release(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_view) {
        wgpuTextureViewRelease(engine->opaque_snapshot_view);
        engine->opaque_snapshot_view = NULL;
    }
    if (engine->opaque_snapshot) {
        wgpuTextureRelease(engine->opaque_snapshot);
        engine->opaque_snapshot = NULL;
    }
    engine->opaque_snapshot_width = 0;
    engine->opaque_snapshot_height = 0;
}
