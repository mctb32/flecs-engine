#include "renderer.h"
#include "flecs_engine.h"

/* Maximum number of mips in the opaque snapshot pyramid. Higher roughness
 * values sample higher mips. Beyond ~6 the resolution is too low to contribute
 * meaningfully, so cap there. */
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_MAX_MIPS 6u

/* Downsample shader: fullscreen triangle + linear sample of the source mip.
 * The linear sampler does the 2x2 box filter for us. */
static const char *kDownsampleShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var src_tex : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_smp : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSampleLevel(src_tex, src_smp, in.uv, 0.0);\n"
    "}\n";

static uint32_t flecsEngine_transmission_computeMipCount(
    uint32_t width,
    uint32_t height)
{
    uint32_t max_dim = width > height ? width : height;
    uint32_t mips = 1u;
    while ((max_dim >> mips) > 0u && mips < FLECS_ENGINE_OPAQUE_SNAPSHOT_MAX_MIPS) {
        mips ++;
    }
    return mips;
}

static void flecsEngine_transmission_releaseTexture(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_mip_views) {
        for (uint32_t i = 0; i < engine->opaque_snapshot_mip_count; i ++) {
            if (engine->opaque_snapshot_mip_views[i]) {
                wgpuTextureViewRelease(engine->opaque_snapshot_mip_views[i]);
            }
        }
        ecs_os_free(engine->opaque_snapshot_mip_views);
        engine->opaque_snapshot_mip_views = NULL;
    }
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
    engine->opaque_snapshot_mip_count = 0;
}

static bool flecsEngine_transmission_ensurePipeline(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_downsample_pipeline) {
        return true;
    }

    /* Linear sampler for the 2x2 box downsample. Reused across all mip
     * levels during generation. */
    if (!engine->opaque_snapshot_sampler) {
        engine->opaque_snapshot_sampler = wgpuDeviceCreateSampler(
            engine->device, &(WGPUSamplerDescriptor){
                .addressModeU = WGPUAddressMode_ClampToEdge,
                .addressModeV = WGPUAddressMode_ClampToEdge,
                .addressModeW = WGPUAddressMode_ClampToEdge,
                .magFilter = WGPUFilterMode_Linear,
                .minFilter = WGPUFilterMode_Linear,
                .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                .lodMinClamp = 0.0f,
                .lodMaxClamp = 32.0f,
                .maxAnisotropy = 1
            });
        if (!engine->opaque_snapshot_sampler) {
            return false;
        }
    }

    /* Bind group layout: source texture + linear sampler. */
    if (!engine->opaque_snapshot_downsample_layout) {
        WGPUBindGroupLayoutEntry entries[2] = {
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false
                }
            },
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Filtering
                }
            }
        };
        engine->opaque_snapshot_downsample_layout =
            wgpuDeviceCreateBindGroupLayout(
                engine->device, &(WGPUBindGroupLayoutDescriptor){
                    .entryCount = 2,
                    .entries = entries
                });
        if (!engine->opaque_snapshot_downsample_layout) {
            return false;
        }
    }

    WGPUShaderModule shader = flecsEngine_createShaderModule(
        engine->device, kDownsampleShaderSource);
    if (!shader) {
        return false;
    }

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &engine->opaque_snapshot_downsample_layout
        });
    if (!pipeline_layout) {
        wgpuShaderModuleRelease(shader);
        return false;
    }

    WGPUColorTargetState color_target = {
        .format = engine->hdr_color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    engine->opaque_snapshot_downsample_pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = shader,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = shader,
                .entryPoint = WGPU_STR("fs_main"),
                .targetCount = 1,
                .targets = &color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);

    return engine->opaque_snapshot_downsample_pipeline != NULL;
}

static bool flecsEngine_transmission_createTexture(
    FlecsEngineImpl *engine,
    uint32_t width,
    uint32_t height)
{
    uint32_t mip_count = flecsEngine_transmission_computeMipCount(width, height);

    engine->opaque_snapshot = wgpuDeviceCreateTexture(
        engine->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, 1 },
            .format = engine->hdr_color_format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        });
    if (!engine->opaque_snapshot) {
        return false;
    }

    /* Full-mip-chain view for sampling from the transmission shader. */
    engine->opaque_snapshot_view = wgpuTextureCreateView(
        engine->opaque_snapshot, &(WGPUTextureViewDescriptor){
            .format = engine->hdr_color_format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1
        });
    if (!engine->opaque_snapshot_view) {
        return false;
    }

    /* Per-mip views: mip 0 is the copy destination, mips 1..N are both
     * render attachment (for generation) and sample source (for the next
     * downsample pass reading the previous level). */
    engine->opaque_snapshot_mip_views = ecs_os_calloc_n(
        WGPUTextureView, mip_count);
    if (!engine->opaque_snapshot_mip_views) {
        return false;
    }
    for (uint32_t i = 0; i < mip_count; i ++) {
        engine->opaque_snapshot_mip_views[i] = wgpuTextureCreateView(
            engine->opaque_snapshot, &(WGPUTextureViewDescriptor){
                .format = engine->hdr_color_format,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = i,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1
            });
        if (!engine->opaque_snapshot_mip_views[i]) {
            return false;
        }
    }

    engine->opaque_snapshot_width = width;
    engine->opaque_snapshot_height = height;
    engine->opaque_snapshot_mip_count = mip_count;
    return true;
}

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

    if (!flecsEngine_transmission_ensurePipeline(engine)) {
        return;
    }

    /* (Re)create snapshot texture + mip views if dimensions changed. */
    if (engine->opaque_snapshot_width != width ||
        engine->opaque_snapshot_height != height)
    {
        flecsEngine_transmission_releaseTexture(engine);
        if (!flecsEngine_transmission_createTexture(engine, width, height)) {
            flecsEngine_transmission_releaseTexture(engine);
            return;
        }
        /* Rebuild the globals bind group so it picks up the new view */
        engine->scene_bind_version++;
    }

    /* Copy the resolved color target to mip 0. */
    wgpuCommandEncoderCopyTextureToTexture(
        encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = src_texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUTexelCopyTextureInfo){
            .texture = engine->opaque_snapshot,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUExtent3D){ width, height, 1 });

    /* Generate mip pyramid by repeatedly downsampling the previous level. */
    for (uint32_t i = 1; i < engine->opaque_snapshot_mip_count; i ++) {
        WGPUBindGroupEntry bind_entries[2] = {
            { .binding = 0, .textureView = engine->opaque_snapshot_mip_views[i - 1] },
            { .binding = 1, .sampler = engine->opaque_snapshot_sampler }
        };
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = engine->opaque_snapshot_downsample_layout,
                .entryCount = 2,
                .entries = bind_entries
            });
        if (!bind_group) {
            continue;
        }

        WGPURenderPassColorAttachment color_attachment = {
            .view = engine->opaque_snapshot_mip_views[i],
            WGPU_DEPTH_SLICE
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = (WGPUColor){0}
        };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &(WGPURenderPassDescriptor){
                .colorAttachmentCount = 1,
                .colorAttachments = &color_attachment
            });
        if (!pass) {
            wgpuBindGroupRelease(bind_group);
            continue;
        }

        wgpuRenderPassEncoderSetPipeline(
            pass, engine->opaque_snapshot_downsample_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(bind_group);
    }
}

void flecsEngine_transmission_release(
    FlecsEngineImpl *engine)
{
    flecsEngine_transmission_releaseTexture(engine);

    if (engine->opaque_snapshot_downsample_pipeline) {
        wgpuRenderPipelineRelease(engine->opaque_snapshot_downsample_pipeline);
        engine->opaque_snapshot_downsample_pipeline = NULL;
    }
    if (engine->opaque_snapshot_downsample_layout) {
        wgpuBindGroupLayoutRelease(engine->opaque_snapshot_downsample_layout);
        engine->opaque_snapshot_downsample_layout = NULL;
    }
    if (engine->opaque_snapshot_sampler) {
        wgpuSamplerRelease(engine->opaque_snapshot_sampler);
        engine->opaque_snapshot_sampler = NULL;
    }
}
