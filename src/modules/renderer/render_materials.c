#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

/* ---- Material buffer management ---- */

static void flecsEngine_textureArray_release(FlecsEngineImpl *impl);

static void flecsEngine_material_ensureBufferCapacity(
    FlecsEngineImpl *impl,
    uint32_t required_count)
{
    if (!required_count) {
        return;
    }

    if (impl->materials.buffer &&
        impl->materials.cpu_materials &&
        required_count <= impl->materials.buffer_capacity)
    {
        return;
    }

    uint32_t new_capacity = impl->materials.buffer_capacity;
    if (!new_capacity) {
        new_capacity = 64;
    }

    while (new_capacity < required_count) {
        if (new_capacity > (UINT32_MAX / 2u)) {
            new_capacity = required_count;
            break;
        }
        new_capacity *= 2u;
    }

    FlecsGpuMaterial *new_cpu_materials =
        ecs_os_malloc_n(FlecsGpuMaterial, new_capacity);
    ecs_assert(new_cpu_materials != NULL, ECS_OUT_OF_MEMORY, NULL);

    /* Preserve existing data so that per-channel layer values (and any
     * other fields written by buildTextureArrays) survive a reallocation. */
    if (impl->materials.cpu_materials && impl->materials.buffer_capacity) {
        ecs_os_memcpy_n(new_cpu_materials, impl->materials.cpu_materials,
            FlecsGpuMaterial, (int32_t)impl->materials.buffer_capacity);
    }

    WGPUBuffer new_material_buffer = wgpuDeviceCreateBuffer(
        impl->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsGpuMaterial)
        });
    ecs_assert(new_material_buffer != NULL, ECS_INTERNAL_ERROR, NULL);

    if (impl->materials.buffer) {
        wgpuBufferRelease(impl->materials.buffer);
    }

    ecs_os_free(impl->materials.cpu_materials);

    impl->materials.buffer = new_material_buffer;
    impl->materials.cpu_materials = new_cpu_materials;
    impl->materials.buffer_capacity = new_capacity;

    /* The materials storage buffer lives in the group-0 scene-globals bind
     * group. Bump scene_bind_version so the cached bind group is rebuilt
     * against the new buffer handle on the next frame. */
    impl->scene_bind_version ++;
}

void flecsEngine_material_ensureBuffer(
    FlecsEngineImpl *impl)
{
    if (!impl->materials.buffer) {
        flecsEngine_material_ensureBufferCapacity(impl, 1);
    }
}

void flecsEngine_material_releaseBuffer(
    FlecsEngineImpl *impl)
{
    if (impl->materials.buffer) {
        wgpuBufferRelease(impl->materials.buffer);
        impl->materials.buffer = NULL;
    }

    ecs_os_free(impl->materials.cpu_materials);
    impl->materials.cpu_materials = NULL;

    impl->materials.buffer_capacity = 0;
    impl->materials.count = 0;

    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];
        for (int ch = 0; ch < 4; ch++) {
            if (bk->texture_array_views[ch]) {
                wgpuTextureViewRelease(bk->texture_array_views[ch]);
                bk->texture_array_views[ch] = NULL;
            }
            if (bk->texture_arrays[ch]) {
                wgpuTextureRelease(bk->texture_arrays[ch]);
                bk->texture_arrays[ch] = NULL;
            }
            bk->layer_counts[ch] = 0;
        }
        bk->mip_count = 0;
        bk->width = 0;
        bk->height = 0;
    }
    if (impl->materials.texture_array_bind_group) {
        wgpuBindGroupRelease(impl->materials.texture_array_bind_group);
        impl->materials.texture_array_bind_group = NULL;
    }
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

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialUpload");

    /* Guarantee a non-null buffer regardless of whether any material is
     * defined yet — see flecsEngine_material_ensureBuffer. */
    flecsEngine_material_ensureBuffer(impl);

    if (impl->materials.last_id == impl->materials.next_id) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    impl->materials.count = 0;
    if (!impl->materials.query || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    ecs_trace("upload materials buffer (last material id = %u)",
        impl->materials.next_id);

redo: {
        uint32_t required_count = 0;
        uint32_t capacity = impl->materials.buffer_capacity;

        if (capacity) {
            ecs_os_memset_n(
                impl->materials.cpu_materials, 0, FlecsGpuMaterial, capacity);
        }

        ecs_iter_t it = ecs_query_iter(world, impl->materials.query);
        while (ecs_query_next(&it)) {
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 0);
            const FlecsPbrMaterial *materials =
                ecs_field(&it, FlecsPbrMaterial, 1);
            const FlecsMaterialId *material_ids =
                ecs_field(&it, FlecsMaterialId, 2);
            const FlecsEmissive *emissives =
                ecs_field(&it, FlecsEmissive, 3);

            bool colors_self = ecs_field_is_self(&it, 0);
            bool materials_self = ecs_field_is_self(&it, 1);
            bool emissives_self = ecs_field_is_self(&it, 3);

            for (int32_t i = 0; i < it.count; i ++) {
                uint32_t index = material_ids[i].value;
                if ((index + 1u) > required_count) {
                    required_count = index + 1u;
                }

                if (index >= capacity) {
                    continue;
                }

                int32_t ci = colors_self ? i : 0;
                int32_t mi = materials_self ? i : 0;
                int32_t ei = emissives_self ? i : 0;

                FlecsEmissive emissive = {0};
                if (emissives) {
                    emissive = emissives[ei];
                }

                flecs_rgba_t em_color = emissive.color;
                if (em_color.r == 0 && em_color.g == 0 &&
                    em_color.b == 0 && emissive.strength > 0.0f)
                {
                    em_color = (flecs_rgba_t){255, 255, 255, 255};
                }

                impl->materials.cpu_materials[index] = (FlecsGpuMaterial){
                    .color = colors[ci],
                    .metallic = materials[mi].metallic,
                    .roughness = materials[mi].roughness,
                    .emissive_strength = emissive.strength,
                    .emissive_color = em_color,
                    /* Default to bucket 1 (1024). The array build pass
                     * will override this for materials with PBR textures
                     * once it has assigned them to a real bucket. The
                     * per-prefab fallback path also relies on this default
                     * because its bind group plugs source textures into
                     * the bucket-1 slots. All per-channel layer fields
                     * default to 0 (the reserved neutral slot). */
                    .texture_bucket = 1
                };
            }
        }

        if (required_count > capacity) {
            flecsEngine_material_ensureBufferCapacity(impl, required_count);
            goto redo;
        }

        if (!required_count) {
            FLECS_TRACY_ZONE_END;
            return;
        }

        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)required_count * sizeof(FlecsGpuMaterial));
        impl->materials.count = required_count;

        /* Materials changed — invalidate texture arrays so they are
         * rebuilt on the next check with up-to-date layer assignments. */
        flecsEngine_textureArray_release(impl);

        impl->materials.last_id = impl->materials.next_id;
    }
    FLECS_TRACY_ZONE_END;
}

/* ---- Texture array building ---- */

/* Bucket dimensions, indexed by bucket number (0/1/2). */
static const uint32_t flecs_engine_bucket_dim[FLECS_ENGINE_TEXTURE_BUCKET_COUNT] = {
    512, 1024, 2048
};

/* Bucket arrays are always RGBA8Unorm. The blit pass converts arbitrary
 * source formats (BC7, RGBA8, etc.) to this canonical format on the GPU,
 * and the mip-gen compute shader writes mips back through the same format. */
#define FLECS_ENGINE_BUCKET_FORMAT WGPUTextureFormat_RGBA8Unorm

/* Pick a bucket index for a given source texture dimension. The chosen
 * bucket is the smallest one whose dimension is >= the source's max side.
 * Sources larger than the largest bucket are clamped to the top bucket. */
static uint8_t flecsEngine_textureArray_pickBucket(uint32_t w, uint32_t h)
{
    uint32_t max_dim = w > h ? w : h;
    for (uint8_t b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        if (max_dim <= flecs_engine_bucket_dim[b]) {
            return b;
        }
    }
    return FLECS_ENGINE_TEXTURE_BUCKET_COUNT - 1;
}

/* ---- Blit + mip-gen pipelines ---- */

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

/* Release previous texture arrays so they can be rebuilt. */
static void flecsEngine_textureArray_release(
    FlecsEngineImpl *impl)
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];
        for (int ch = 0; ch < 4; ch++) {
            if (bk->texture_array_views[ch]) {
                wgpuTextureViewRelease(bk->texture_array_views[ch]);
                bk->texture_array_views[ch] = NULL;
            }
            if (bk->texture_arrays[ch]) {
                wgpuTextureRelease(bk->texture_arrays[ch]);
                bk->texture_arrays[ch] = NULL;
            }
            bk->layer_counts[ch] = 0;
        }
        bk->mip_count = 0;
        bk->width = 0;
        bk->height = 0;
    }
    if (impl->materials.texture_array_bind_group) {
        wgpuBindGroupRelease(impl->materials.texture_array_bind_group);
        impl->materials.texture_array_bind_group = NULL;
    }
}

/* Survey all materials with PBR textures, assign each one a bucket based
 * on the largest dimension across its 4 channels, and assign each
 * contributing channel its own dense slot inside that bucket's channel
 * array. Layer 0 of every (bucket, channel) is reserved as a neutral
 * slot pre-filled with the channel's fallback colour; contributors get
 * slots 1..N. Materials without a given channel leave their layer at 0
 * so the shader samples the neutral fill there.
 *
 * This decouples channel allocation so that e.g. a bucket with 100 albedo
 * users but only 1 emissive user sizes its emissive array at 2 layers,
 * not 100. Fills bucket_channel_layers with (contributors + 1) for each
 * populated (bucket, channel), and 0 for channels with no contributors
 * so the create pass can skip them entirely. */
static bool flecsEngine_textureArray_survey(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t bucket_channel_layers[FLECS_ENGINE_TEXTURE_BUCKET_COUNT][4])
{
    /* Start each (bucket, channel) at 1 for the reserved neutral slot.
     * Contributors will bump this to 2, 3, ... We'll collapse 1 back
     * to 0 at the end for channels with zero real contributors so the
     * create pass knows to skip allocating that channel. */
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        for (int ch = 0; ch < 4; ch++) {
            bucket_channel_layers[b][ch] = 1;
        }
    }

    bool any_material = false;

    ecs_iter_t it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };

            /* First pass: resolve each channel's source (if any) and
             * find the max side across all contributing channels. */
            bool has_channel[4] = { false, false, false, false };
            uint32_t max_w = 0, max_h = 0;
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                has_channel[ch] = true;
                uint32_t tw = wgpuTextureGetWidth(ti->texture);
                uint32_t th = wgpuTextureGetHeight(ti->texture);
                if (tw > max_w) max_w = tw;
                if (th > max_h) max_h = th;
            }

            if (!max_w || !max_h) {
                /* Material has FlecsPbrTextures but no usable channels —
                 * leave it on the default bucket so it samples fallback. */
                continue;
            }

            uint8_t bucket = flecsEngine_textureArray_pickBucket(max_w, max_h);
            FlecsGpuMaterial *gm = &impl->materials.cpu_materials[mat_id];
            gm->texture_bucket = bucket;

            /* Per-channel dense slot assignment. Non-contributors stay at
             * layer 0 (neutral fill). */
            if (has_channel[0]) {
                gm->layer_albedo = bucket_channel_layers[bucket][0]++;
            }
            if (has_channel[1]) {
                gm->layer_emissive = bucket_channel_layers[bucket][1]++;
            }
            if (has_channel[2]) {
                gm->layer_mr = bucket_channel_layers[bucket][2]++;
            }
            if (has_channel[3]) {
                gm->layer_normal = bucket_channel_layers[bucket][3]++;
            }

            any_material = true;
        }
    }

    /* Channels that never saw a contributor are still sitting at 1 (just
     * the neutral slot). Collapse those to 0 so createBucket skips them
     * and the bind group backfills with the 1x1 fallback view. */
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        for (int ch = 0; ch < 4; ch++) {
            if (bucket_channel_layers[b][ch] == 1) {
                bucket_channel_layers[b][ch] = 0;
            }
        }
    }

    return any_material;
}

/* Compute the mip count for a square dimension. */
static uint32_t flecsEngine_textureArray_mipCount(uint32_t dim)
{
    uint32_t count = 1;
    while (dim > 1) { dim >>= 1; count++; }
    return count;
}

/* Create the channel array textures for a single bucket. Each channel
 * is allocated independently at its own layer count; channels with a
 * layer count of 0 are skipped entirely (their bind group slot will be
 * backfilled with the 1x1 fallback view). */
static bool flecsEngine_textureArray_createBucket(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx,
    const uint32_t channel_layer_counts[4])
{
    flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket_idx];
    uint32_t dim = flecs_engine_bucket_dim[bucket_idx];
    uint32_t mip_count = flecsEngine_textureArray_mipCount(dim);

    for (int ch = 0; ch < 4; ch++) {
        uint32_t lc = channel_layer_counts[ch];
        if (!lc) {
            bk->layer_counts[ch] = 0;
            continue;
        }

        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_RenderAttachment
                   | WGPUTextureUsage_StorageBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = {
                .width = dim,
                .height = dim,
                .depthOrArrayLayers = lc
            },
            .format = FLECS_ENGINE_BUCKET_FORMAT,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        };
        bk->texture_arrays[ch] = wgpuDeviceCreateTexture(impl->device, &desc);
        if (!bk->texture_arrays[ch]) {
            ecs_err("failed to create texture array (bucket %u, channel %d, "
                "%ux%u, %u layers)", bucket_idx, ch, dim, dim, lc);
            return false;
        }
        bk->layer_counts[ch] = lc;
    }

    bk->mip_count = mip_count;
    bk->width = dim;
    bk->height = dim;
    return true;
}

/* Pre-fill mip 0 of every layer with the channel's fallback colour so
 * that materials whose textures couldn't be blitted still sample sensible
 * defaults. The blit pass writes mip 0 over the fallback for materials
 * that have a real source. The mip-gen compute pass then reads mip 0
 * (mixed fallback + blitted) to generate mips 1..N. */
static void flecsEngine_textureArray_fillBucketFallback(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx)
{
    flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket_idx];

    static const uint8_t fallback_rgba[4][4] = {
        { 255, 255, 255, 255 }, /* albedo    */
        { 255, 255, 255, 255 }, /* emissive  */
        { 255, 255, 255, 255 }, /* roughness */
        { 128, 128, 255, 255 }  /* normal    */
    };

    uint32_t dim = bk->width;
    if (!dim) {
        return;
    }
    uint32_t row_bytes = dim * 4;
    uint32_t layer_bytes = row_bytes * dim;
    uint8_t *fill = ecs_os_malloc((ecs_size_t)layer_bytes);

    for (int ch = 0; ch < 4; ch++) {
        uint32_t lc = bk->layer_counts[ch];
        if (!lc || !bk->texture_arrays[ch]) continue;

        for (uint32_t p = 0; p < dim * dim; p++) {
            memcpy(fill + p * 4, fallback_rgba[ch], 4);
        }

        for (uint32_t layer = 0; layer < lc; layer++) {
            WGPUTexelCopyTextureInfo dst = {
                .texture = bk->texture_arrays[ch],
                .mipLevel = 0,
                .origin = { 0, 0, layer }
            };
            WGPUTexelCopyBufferLayout src_layout = {
                .bytesPerRow = row_bytes,
                .rowsPerImage = dim
            };
            WGPUExtent3D size = { dim, dim, 1 };
            wgpuQueueWriteTexture(
                impl->queue, &dst, fill,
                layer_bytes, &src_layout, &size);
        }
    }

    ecs_os_free(fill);
}

/* For each material with PBR textures, run a GPU blit per channel that
 * samples the source texture and writes the result into mip 0 of the
 * material's slot in its bucket array. Then run the mip-gen compute pass
 * to fill mips 1..N for every channel of every populated bucket. */
static void flecsEngine_textureArray_blitTextures(
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

                /* Create a 2D sampling view of the source. The source
                 * is a 2D texture with 1 array layer (per the loader). */
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

                /* Create a 2D render-target view of the destination
                 * (single slice, single mip). */
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

    /* Re-upload material buffer with updated (bucket, layer) values */
    if (impl->materials.count) {
        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)impl->materials.count * sizeof(FlecsGpuMaterial));
    }
}

/* Create 2D-array views per bucket and assemble the single bind group.
 * Channels with no allocation get a NULL view here; the backfill loop
 * below plugs in the 1x1 fallback view in their place. */
static void flecsEngine_textureArray_createBindGroup(
    FlecsEngineImpl *impl)
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];

        for (int ch = 0; ch < 4; ch++) {
            if (!bk->layer_counts[ch] || !bk->texture_arrays[ch]) continue;
            WGPUTextureViewDescriptor vd = {
                .format = FLECS_ENGINE_BUCKET_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = 0,
                .mipLevelCount = bk->mip_count,
                .baseArrayLayer = 0,
                .arrayLayerCount = bk->layer_counts[ch]
            };
            bk->texture_array_views[ch] = wgpuTextureCreateView(
                bk->texture_arrays[ch], &vd);
        }
    }

    WGPUSampler sampler = flecsEngine_pbr_texture_ensureSampler(impl);
    WGPUBindGroupLayout layout =
        flecsEngine_textures_ensureBindLayout(impl);

    WGPUTextureView fallback_white  = impl->materials.fallback_white_view;
    WGPUTextureView fallback_normal = impl->materials.fallback_normal_view;

    /* Layout: 4 channels × 3 buckets, in (channel, bucket) row-major.
     * binding = channel * 3 + bucket. */
    WGPUTextureView views[12];
    for (int ch = 0; ch < 4; ch++) {
        WGPUTextureView fb = (ch == 3) ? fallback_normal : fallback_white;
        for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
            WGPUTextureView v = impl->materials.buckets[b].texture_array_views[ch];
            views[ch * 3 + b] = v ? v : fb;
        }
    }

    WGPUBindGroupEntry entries[13];
    for (uint32_t i = 0; i < 12; i++) {
        entries[i] = (WGPUBindGroupEntry){
            .binding = i,
            .textureView = views[i]
        };
    }
    entries[12] = (WGPUBindGroupEntry){
        .binding = 12,
        .sampler = sampler
    };

    impl->materials.texture_array_bind_group =
        wgpuDeviceCreateBindGroup(impl->device,
            &(WGPUBindGroupDescriptor){
                .layout = layout,
                .entries = entries,
                .entryCount = 13
            });
}

/* Build per-bucket texture 2D arrays so the shader can index layers via
 * texture_bucket plus per-channel layer fields on FlecsGpuMaterial.
 * Channels are sized independently per bucket: only materials that
 * actually have a texture for a given (bucket, channel) consume a
 * layer there. The GPU blit pass normalizes any source format/dimension
 * to the bucket's RGBA8Unorm arrays, so this build always succeeds when
 * there's at least one material with PBR textures. */
void flecsEngine_material_buildTextureArrays(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (!impl->materials.texture_query || !impl->device) {
        return;
    }

    flecsEngine_textureArray_release(impl);
    flecsEngine_pbr_texture_ensureFallbacks(impl);

    uint32_t bucket_channel_layers[FLECS_ENGINE_TEXTURE_BUCKET_COUNT][4];
    if (!flecsEngine_textureArray_survey(
            world, impl, bucket_channel_layers))
    {
        return;
    }

    for (uint8_t b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        uint32_t bucket_total = 0;
        for (int ch = 0; ch < 4; ch++) {
            bucket_total += bucket_channel_layers[b][ch];
        }
        if (!bucket_total) continue;

        if (!flecsEngine_textureArray_createBucket(
                impl, b, bucket_channel_layers[b]))
        {
            flecsEngine_textureArray_release(impl);
            return;
        }
        flecsEngine_textureArray_fillBucketFallback(impl, b);
    }

    flecsEngine_textureArray_blitTextures(world, impl);

    flecsEngine_textureArray_createBindGroup(impl);
}
