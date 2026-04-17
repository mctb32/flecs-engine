#include "renderer.h"
#include "mip_pyramid.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static const char *kDownsampleShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var src_tex : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_smp : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let ps = 1.0 / vec2<f32>(textureDimensions(src_tex));\n"
    "  let pl = 2.0 * ps;\n"
    "  let ns = -1.0 * ps;\n"
    "  let nl = -2.0 * ps;\n"
    "  let uv = in.uv;\n"
    "  let a = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, pl.y), 0.0).rgb;\n"
    "  let b = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(0.00, pl.y), 0.0).rgb;\n"
    "  let c = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, pl.y), 0.0).rgb;\n"
    "  let d = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, 0.00), 0.0).rgb;\n"
    "  let e = textureSampleLevel(src_tex, src_smp, uv, 0.0).rgb;\n"
    "  let f = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, 0.00), 0.0).rgb;\n"
    "  let g = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, nl.y), 0.0).rgb;\n"
    "  let h = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(0.00, nl.y), 0.0).rgb;\n"
    "  let i = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, nl.y), 0.0).rgb;\n"
    "  let j = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ns.x, ps.y), 0.0).rgb;\n"
    "  let k = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ps.x, ps.y), 0.0).rgb;\n"
    "  let l = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ns.x, ns.y), 0.0).rgb;\n"
    "  let m = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ps.x, ns.y), 0.0).rgb;\n"
    "  var sample = (a + c + g + i) * 0.03125;\n"
    "  sample += (b + d + f + h) * 0.0625;\n"
    "  sample += (e + j + k + l + m) * 0.125;\n"
    "  return vec4<f32>(sample, 1.0);\n"
    "}\n";

static void flecsEngine_transmission_releaseTexture(
    FlecsRenderViewImpl *view_impl)
{
    flecs_view_opaque_snapshot_t *s = &view_impl->opaque_snapshot;
    flecsEngine_mipPyramid_release(
        &s->texture,
        &s->mip_views,
        s->mip_count);

    if (s->view) {
        wgpuTextureViewRelease(s->view);
        s->view = NULL;
    }

    s->width = 0;
    s->height = 0;
    s->mip_count = 0;
}

static bool flecsEngine_transmission_ensureDownsamplePipeline(
    FlecsEngineImpl *engine)
{
    if (engine->pipelines.opaque_snapshot_downsample_pipeline) {
        return true;
    }

    if (!engine->pipelines.opaque_snapshot_sampler) {
        engine->pipelines.opaque_snapshot_sampler =
            flecsEngine_mipPyramid_createFilteredSampler(engine->device);
        if (!engine->pipelines.opaque_snapshot_sampler) {
            return false;
        }
    }

    if (!engine->pipelines.opaque_snapshot_downsample_layout) {
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
        engine->pipelines.opaque_snapshot_downsample_layout =
            wgpuDeviceCreateBindGroupLayout(
                engine->device, &(WGPUBindGroupLayoutDescriptor){
                    .entryCount = 2,
                    .entries = entries
                });
        if (!engine->pipelines.opaque_snapshot_downsample_layout) {
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
            .bindGroupLayouts = &engine->pipelines.opaque_snapshot_downsample_layout
        });
    if (!pipeline_layout) {
        wgpuShaderModuleRelease(shader);
        return false;
    }

    WGPUColorTargetState color_target = {
        .format = engine->hdr_color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    engine->pipelines.opaque_snapshot_downsample_pipeline = wgpuDeviceCreateRenderPipeline(
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

    return engine->pipelines.opaque_snapshot_downsample_pipeline != NULL;
}

static bool flecsEngine_transmission_createTexture(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    uint32_t width,
    uint32_t height)
{
    flecs_view_opaque_snapshot_t *s = &view_impl->opaque_snapshot;
    uint32_t mip_count = flecsEngine_mipPyramid_maxMips(width, height);
    if (mip_count > 1u) {
        mip_count --;
    }

    if (!flecsEngine_mipPyramid_create(
        engine->device, width, height, mip_count,
        engine->hdr_color_format,
        WGPUTextureUsage_TextureBinding
            | WGPUTextureUsage_CopyDst
            | WGPUTextureUsage_RenderAttachment,
        &s->texture,
        &s->mip_views))
    {
        return false;
    }

    s->view = wgpuTextureCreateView(
        s->texture, &(WGPUTextureViewDescriptor){
            .format = engine->hdr_color_format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1
        });
    if (!s->view) {
        return false;
    }

    s->width = width;
    s->height = height;
    s->mip_count = mip_count;
    return true;
}

/* Run a downsample pass: read mip_views[i-1], write to mip_views[i].
 * Builds the gaussian blur pyramid used by the transmission shader. */
static void flecsEngine_transmission_downsampleMip(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    uint32_t dst_mip)
{
    const flecs_view_opaque_snapshot_t *s = &view_impl->opaque_snapshot;
    WGPUBindGroupEntry bind_entries[2] = {
        {
            .binding = 0,
            .textureView = s->mip_views[dst_mip - 1]
        },
        {
            .binding = 1,
            .sampler = engine->pipelines.opaque_snapshot_sampler
        }
    };
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->pipelines.opaque_snapshot_downsample_layout,
            .entryCount = 2,
            .entries = bind_entries
        });
    if (!bind_group) {
        return;
    }

    WGPURenderPassColorAttachment color_attachment = {
        .view = s->mip_views[dst_mip],
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
        return;
    }

    wgpuRenderPassEncoderSetPipeline(
        pass, engine->pipelines.opaque_snapshot_downsample_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bind_group);
}

void flecsEngine_transmission_updateSnapshot(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    WGPUTexture src_texture,
    uint32_t width,
    uint32_t height)
{
    FLECS_TRACY_ZONE_BEGIN("TransmissionSnapshot");
    if (!width || !height) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (!flecsEngine_transmission_ensureDownsamplePipeline(engine)) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecs_view_opaque_snapshot_t *s = &view_impl->opaque_snapshot;

    /* (Re)create snapshot texture if dimensions changed. */
    if (s->width != width || s->height != height) {
        flecsEngine_transmission_releaseTexture(view_impl);
        if (!flecsEngine_transmission_createTexture(
            engine, view_impl, width, height))
        {
            flecsEngine_transmission_releaseTexture(view_impl);
            FLECS_TRACY_ZONE_END;
            return;
        }
        /* Rebuild the view's group 0 so it picks up the new snapshot view */
        view_impl->scene_bind_version = 0;
    }

    uint32_t mip_count = s->mip_count;

    /* 1. Copy the resolved color target to mip 0. */
    wgpuCommandEncoderCopyTextureToTexture(
        encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = src_texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUTexelCopyTextureInfo){
            .texture = s->texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUExtent3D){ width, height, 1 });

    /* 2. Build the gaussian blur pyramid via Jimenez downsample. */
    for (uint32_t i = 1; i < mip_count; i ++) {
        flecsEngine_transmission_downsampleMip(engine, view_impl, encoder, i);
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_transmission_releaseView(
    FlecsRenderViewImpl *view_impl)
{
    flecsEngine_transmission_releaseTexture(view_impl);
}

void flecsEngine_transmission_releaseShared(
    FlecsEngineImpl *engine)
{
    if (engine->pipelines.opaque_snapshot_downsample_pipeline) {
        wgpuRenderPipelineRelease(engine->pipelines.opaque_snapshot_downsample_pipeline);
        engine->pipelines.opaque_snapshot_downsample_pipeline = NULL;
    }
    if (engine->pipelines.opaque_snapshot_downsample_layout) {
        wgpuBindGroupLayoutRelease(engine->pipelines.opaque_snapshot_downsample_layout);
        engine->pipelines.opaque_snapshot_downsample_layout = NULL;
    }
    if (engine->pipelines.opaque_snapshot_sampler) {
        wgpuSamplerRelease(engine->pipelines.opaque_snapshot_sampler);
        engine->pipelines.opaque_snapshot_sampler = NULL;
    }
}
