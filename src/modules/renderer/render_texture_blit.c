#include "renderer.h"
#include "flecs_engine.h"

/* ---- Blit + mip-gen pipelines ----
 *
 * The blit pipeline normalizes arbitrary source textures (any format,
 * any dimension) into a single RGBA8 slice of a bucket array.  The
 * mip-gen compute pipeline then fills mips 1..N from the blitted mip 0.
 *
 * Both pipelines are created lazily on first use and live on the shared
 * engine state (impl->materials.blit_*, impl->materials.mipgen_*). */

/* Fullscreen-triangle blit shader. Reads a single 2D source texture and
 * writes it (with bilinear filtering) to the bound render target, which
 * is a single (slice, mip 0) view of a bucket array. */
static const char *kBlitShaderSource =
    "struct VertexOut {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>,\n"
    "};\n"
    "@vertex fn vs_main(@builtin(vertex_index) id : u32) -> VertexOut {\n"
    "  var out : VertexOut;\n"
    "  let x = f32((id << 1u) & 2u);\n"
    "  let y = f32(id & 2u);\n"
    "  out.uv = vec2<f32>(x, y);\n"
    "  out.pos = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);\n"
    "  return out;\n"
    "}\n"
    "@group(0) @binding(0) var src_tex  : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_samp : sampler;\n"
    "@fragment fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {\n"
    "  return textureSample(src_tex, src_samp, in.uv);\n"
    "}\n";

/* Mip-gen compute shader. Reads mip N of a bucket array as a 2DArray
 * sampled texture and writes mip N+1 via a 2DArray storage texture. The
 * dispatch z dimension covers all slices in one shot. */
static const char *kMipGenShaderSource =
    "@group(0) @binding(0) var src : texture_2d_array<f32>;\n"
    "@group(0) @binding(1) var dst : texture_storage_2d_array<rgba8unorm, write>;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {\n"
    "  let dst_dim = textureDimensions(dst);\n"
    "  if (id.x >= dst_dim.x || id.y >= dst_dim.y) { return; }\n"
    "  let layer = i32(id.z);\n"
    "  let src_xy = vec2<i32>(id.xy * 2u);\n"
    "  let s00 = textureLoad(src, src_xy + vec2<i32>(0, 0), layer, 0);\n"
    "  let s10 = textureLoad(src, src_xy + vec2<i32>(1, 0), layer, 0);\n"
    "  let s01 = textureLoad(src, src_xy + vec2<i32>(0, 1), layer, 0);\n"
    "  let s11 = textureLoad(src, src_xy + vec2<i32>(1, 1), layer, 0);\n"
    "  let avg = (s00 + s10 + s01 + s11) * 0.25;\n"
    "  textureStore(dst, vec2<i32>(id.xy), layer, avg);\n"
    "}\n";

static void flecsEngine_textureArray_ensureBlitPipeline(
    FlecsEngineImpl *impl)
{
    if (impl->materials.blit_pipeline) return;

    WGPUBindGroupLayoutEntry blit_entries[2] = {0};
    blit_entries[0].binding = 0;
    blit_entries[0].visibility = WGPUShaderStage_Fragment;
    blit_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    blit_entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    blit_entries[1].binding = 1;
    blit_entries[1].visibility = WGPUShaderStage_Fragment;
    blit_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    impl->materials.blit_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = blit_entries,
            .entryCount = 2
        });

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl->materials.blit_bind_layout
        });

    WGPUShaderSourceWGSL wgsl_src = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = WGPU_STR(kBlitShaderSource)
    };
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(
        impl->device, &(WGPUShaderModuleDescriptor){
            .nextInChain = &wgsl_src.chain
        });

    WGPUColorTargetState color_target = {
        .format = FLECS_ENGINE_BUCKET_FORMAT,
        .writeMask = WGPUColorWriteMask_All
    };

    impl->materials.blit_pipeline = wgpuDeviceCreateRenderPipeline(
        impl->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = module,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = module,
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
    wgpuShaderModuleRelease(module);

    /* Linear-filter, clamp-to-edge sampler used for the blit. */
    impl->materials.blit_sampler = wgpuDeviceCreateSampler(
        impl->device, &(WGPUSamplerDescriptor){
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .maxAnisotropy = 1
        });
}

static void flecsEngine_textureArray_ensureMipGenPipeline(
    FlecsEngineImpl *impl)
{
    if (impl->materials.mipgen_pipeline) return;

    WGPUBindGroupLayoutEntry mip_entries[2] = {0};
    mip_entries[0].binding = 0;
    mip_entries[0].visibility = WGPUShaderStage_Compute;
    mip_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    mip_entries[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    mip_entries[1].binding = 1;
    mip_entries[1].visibility = WGPUShaderStage_Compute;
    mip_entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    mip_entries[1].storageTexture.format = FLECS_ENGINE_BUCKET_FORMAT;
    mip_entries[1].storageTexture.viewDimension =
        WGPUTextureViewDimension_2DArray;

    impl->materials.mipgen_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = mip_entries,
            .entryCount = 2
        });

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl->materials.mipgen_bind_layout
        });

    WGPUShaderSourceWGSL wgsl_src = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = WGPU_STR(kMipGenShaderSource)
    };
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(
        impl->device, &(WGPUShaderModuleDescriptor){
            .nextInChain = &wgsl_src.chain
        });

    impl->materials.mipgen_pipeline = wgpuDeviceCreateComputePipeline(
        impl->device, &(WGPUComputePipelineDescriptor){
            .layout = pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = WGPU_STR("cs_main")
            }
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);
}

/* Run a single blit: sample from src_2d_view and write to dst_slice_view
 * (which must be a 2D view of one slice/mip 0 of a bucket array). */
static void flecsEngine_textureArray_doBlit(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView src_2d_view,
    WGPUTextureView dst_slice_view)
{
    WGPUBindGroupEntry bg_entries[2] = {
        { .binding = 0, .textureView = src_2d_view },
        { .binding = 1, .sampler = impl->materials.blit_sampler }
    };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(
        impl->device, &(WGPUBindGroupDescriptor){
            .layout = impl->materials.blit_bind_layout,
            .entries = bg_entries,
            .entryCount = 2
        });

    WGPURenderPassColorAttachment color_att = {
        .view = dst_slice_view,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 0 },
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        });
    wgpuRenderPassEncoderSetPipeline(pass, impl->materials.blit_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
}

/* Generate mips 1..N for one channel of a bucket via the compute shader. */
static void flecsEngine_textureArray_genMipsForChannel(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTexture array_tex,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    uint32_t mip_count)
{
    for (uint32_t mip = 1; mip < mip_count; mip++) {
        uint32_t dst_w = arr_w >> mip; if (!dst_w) dst_w = 1;
        uint32_t dst_h = arr_h >> mip; if (!dst_h) dst_h = 1;

        WGPUTextureView src_view = wgpuTextureCreateView(
            array_tex, &(WGPUTextureViewDescriptor){
                .format = FLECS_ENGINE_BUCKET_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = mip - 1,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = layer_count
            });
        WGPUTextureView dst_view = wgpuTextureCreateView(
            array_tex, &(WGPUTextureViewDescriptor){
                .format = FLECS_ENGINE_BUCKET_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = mip,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = layer_count
            });

        WGPUBindGroupEntry bg_entries[2] = {
            { .binding = 0, .textureView = src_view },
            { .binding = 1, .textureView = dst_view }
        };
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(
            impl->device, &(WGPUBindGroupDescriptor){
                .layout = impl->materials.mipgen_bind_layout,
                .entries = bg_entries,
                .entryCount = 2
            });

        WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(
            encoder, &(WGPUComputePassDescriptor){0});
        wgpuComputePassEncoderSetPipeline(cpass, impl->materials.mipgen_pipeline);
        wgpuComputePassEncoderSetBindGroup(cpass, 0, bg, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(
            cpass,
            (dst_w + 7) / 8,
            (dst_h + 7) / 8,
            layer_count);
        wgpuComputePassEncoderEnd(cpass);
        wgpuComputePassEncoderRelease(cpass);

        wgpuBindGroupRelease(bg);
        wgpuTextureViewRelease(src_view);
        wgpuTextureViewRelease(dst_view);
    }
}

void flecsEngine_textureArray_blitTextures(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    flecsEngine_textureArray_ensureBlitPipeline(impl);
    flecsEngine_textureArray_ensureMipGenPipeline(impl);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    /* Pass 1: blit each material's source textures into mip 0 of its
     * assigned (bucket, channel, slot). Each channel uses its own
     * per-material layer index. */
    ecs_iter_t it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            const FlecsGpuMaterial *gm =
                &impl->materials.cpu_materials[mat_id];
            uint8_t bucket = (uint8_t)gm->texture_bucket;
            if (bucket >= FLECS_ENGINE_TEXTURE_BUCKET_COUNT) continue;

            flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket];

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };
            uint32_t per_channel_slot[4] = {
                gm->layer_albedo,
                gm->layer_emissive,
                gm->layer_mr,
                gm->layer_normal
            };

            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                if (!bk->texture_arrays[ch]) continue;
                uint32_t slot = per_channel_slot[ch];
                if (slot == 0) continue;  /* neutral, never blitted */
                if (slot >= bk->layer_counts[ch]) continue;

                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                WGPUTextureView src_view = wgpuTextureCreateView(
                    ti->texture, &(WGPUTextureViewDescriptor){
                        .format = wgpuTextureGetFormat(ti->texture),
                        .dimension = WGPUTextureViewDimension_2D,
                        .baseMipLevel = 0,
                        .mipLevelCount = wgpuTextureGetMipLevelCount(
                            ti->texture),
                        .baseArrayLayer = 0,
                        .arrayLayerCount = 1
                    });

                WGPUTextureView dst_view = wgpuTextureCreateView(
                    bk->texture_arrays[ch], &(WGPUTextureViewDescriptor){
                        .format = FLECS_ENGINE_BUCKET_FORMAT,
                        .dimension = WGPUTextureViewDimension_2D,
                        .baseMipLevel = 0,
                        .mipLevelCount = 1,
                        .baseArrayLayer = slot,
                        .arrayLayerCount = 1
                    });

                flecsEngine_textureArray_doBlit(
                    impl, encoder, src_view, dst_view);

                wgpuTextureViewRelease(src_view);
                wgpuTextureViewRelease(dst_view);
            }
        }
    }

    /* Pass 2: generate mips 1..N for every populated (bucket, channel).
     * Each channel uses its own layer count; channels with no
     * allocation are skipped. */
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];
        if (bk->mip_count <= 1) continue;
        for (int ch = 0; ch < 4; ch++) {
            if (!bk->layer_counts[ch] || !bk->texture_arrays[ch]) continue;
            flecsEngine_textureArray_genMipsForChannel(
                impl, encoder,
                bk->texture_arrays[ch],
                bk->width, bk->height,
                bk->layer_counts[ch], bk->mip_count);
        }
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    wgpuQueueSubmit(impl->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

void flecsEngine_textureBlit_release(
    FlecsEngineImpl *impl)
{
    if (impl->materials.blit_pipeline) {
        wgpuRenderPipelineRelease(impl->materials.blit_pipeline);
        impl->materials.blit_pipeline = NULL;
    }
    if (impl->materials.blit_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->materials.blit_bind_layout);
        impl->materials.blit_bind_layout = NULL;
    }
    if (impl->materials.blit_sampler) {
        wgpuSamplerRelease(impl->materials.blit_sampler);
        impl->materials.blit_sampler = NULL;
    }
    if (impl->materials.mipgen_pipeline) {
        wgpuComputePipelineRelease(impl->materials.mipgen_pipeline);
        impl->materials.mipgen_pipeline = NULL;
    }
    if (impl->materials.mipgen_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->materials.mipgen_bind_layout);
        impl->materials.mipgen_bind_layout = NULL;
    }
}
