#include "renderer.h"
#include "flecs_engine.h"

static const uint32_t flecs_engine_bucket_dim[FLECS_ENGINE_TEXTURE_BUCKET_COUNT] = {
    512, 1024, 2048
};

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

/* Release previous texture arrays so they can be rebuilt. Does NOT
 * release the blit/mipgen pipelines — those are long-lived and cleaned
 * up separately by flecsEngine_textureBlit_release. */
void flecsEngine_textureArray_release(
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

    /* Re-upload material buffer with updated (bucket, layer) values */
    if (impl->materials.count) {
        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)impl->materials.count * sizeof(FlecsGpuMaterial));
    }

    flecsEngine_textureArray_createBindGroup(impl);
}
