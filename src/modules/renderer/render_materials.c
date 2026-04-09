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

    /* Preserve existing data so that texture_layer values (and any other
     * fields written by buildTextureArrays) survive a reallocation. */
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
        }
        bk->layer_count = 0;
        bk->mip_count = 0;
        bk->width = 0;
        bk->height = 0;
    }
    if (impl->materials.texture_array_bind_group) {
        wgpuBindGroupRelease(impl->materials.texture_array_bind_group);
        impl->materials.texture_array_bind_group = NULL;
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
                     * the bucket-1 slots. */
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

static bool flecsEngine_isCompressedFormat(
    WGPUTextureFormat fmt)
{
    uint32_t f = (uint32_t)fmt;
    /* BC1–BC7: 0x2C–0x39, ETC2: 0x3A–0x43, ASTC: 0x44–0x67 */
    if (f >= 0x2Cu && f <= 0x67u) {
        return true;
    }
    /* Guard against unknown formats outside any known range */
    if (f > 0x67u || (f > 0x1Du && f < 0x2Cu)) {
        ecs_err("texture array: unrecognized texture format 0x%x", f);
    }
    return false;
}

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
        }
        bk->layer_count = 0;
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
 * on the largest dimension across its 4 channels, and write the resulting
 * (texture_bucket, texture_layer) into cpu_materials so the shader can
 * look them up later.
 *
 * Phase 2 keeps the existing single-format-family constraint inherited
 * from the pre-bucket code: if any source texture uses a different format
 * family from the others, the build aborts and the renderer falls back to
 * per-prefab bind groups for the whole scene. Phase 3 will lift this
 * constraint via GPU blit normalization to RGBA8.
 *
 * Returns false if the build should abort (no materials with textures, or
 * mixed format families). On success, fills out_format with the chosen
 * format family and bucket_layer_counts with the per-bucket slot counts. */
static bool flecsEngine_textureArray_survey(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureFormat *out_format,
    uint32_t bucket_layer_counts[FLECS_ENGINE_TEXTURE_BUCKET_COUNT])
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        bucket_layer_counts[b] = 0;
    }

    WGPUTextureFormat fmt_seen = WGPUTextureFormat_Undefined;
    uint32_t fmt_base_seen = 0;
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

            uint32_t max_w = 0, max_h = 0;
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                uint32_t tw = wgpuTextureGetWidth(ti->texture);
                uint32_t th = wgpuTextureGetHeight(ti->texture);
                WGPUTextureFormat tf = wgpuTextureGetFormat(ti->texture);
                uint32_t base = (uint32_t)tf & ~1u;

                if (fmt_seen == WGPUTextureFormat_Undefined) {
                    fmt_seen = tf;
                    fmt_base_seen = base;
                } else if (base != fmt_base_seen) {
                    /* Mixed format families — abort the array build. */
                    return false;
                }

                if (tw > max_w) max_w = tw;
                if (th > max_h) max_h = th;
            }

            if (!max_w || !max_h) {
                /* Material has FlecsPbrTextures but no usable channels —
                 * leave it on the default bucket so it samples fallback. */
                continue;
            }

            uint8_t bucket = flecsEngine_textureArray_pickBucket(max_w, max_h);
            uint32_t slot = bucket_layer_counts[bucket]++;

            impl->materials.cpu_materials[mat_id].texture_bucket = bucket;
            impl->materials.cpu_materials[mat_id].texture_layer = slot;
            any_material = true;
        }
    }

    if (!any_material) {
        return false;
    }

    *out_format = fmt_seen;
    return true;
}

/* Compute the mip count for a square dimension. */
static uint32_t flecsEngine_textureArray_mipCount(uint32_t dim)
{
    uint32_t count = 1;
    while (dim > 1) { dim >>= 1; count++; }
    return count;
}

/* Create the four channel array textures for a single bucket. */
static bool flecsEngine_textureArray_createBucket(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx,
    uint32_t layer_count,
    WGPUTextureFormat arr_format)
{
    flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket_idx];
    uint32_t dim = flecs_engine_bucket_dim[bucket_idx];
    uint32_t mip_count = flecsEngine_textureArray_mipCount(dim);

    for (int ch = 0; ch < 4; ch++) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = {
                .width = dim,
                .height = dim,
                .depthOrArrayLayers = layer_count
            },
            .format = arr_format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        };
        bk->texture_arrays[ch] = wgpuDeviceCreateTexture(impl->device, &desc);
        if (!bk->texture_arrays[ch]) {
            ecs_err("failed to create texture array (bucket %u, channel %d, "
                "%ux%u, %u layers)", bucket_idx, ch, dim, dim, layer_count);
            return false;
        }
    }

    bk->layer_count = layer_count;
    bk->mip_count = mip_count;
    bk->width = dim;
    bk->height = dim;
    return true;
}

/* Fill every layer of an uncompressed bucket with channel fallback colours
 * so that materials whose textures couldn't be copied still sample sensible
 * defaults. Skipped for compressed formats — those rely on the
 * unpopulated-layer redirection logic in copy_textures instead. */
static void flecsEngine_textureArray_fillBucketFallback(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx,
    WGPUTextureFormat arr_format)
{
    if (flecsEngine_isCompressedFormat(arr_format)) {
        return;
    }

    flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket_idx];
    if (!bk->layer_count) {
        return;
    }

    static const uint8_t fallback_rgba[4][4] = {
        { 255, 255, 255, 255 }, /* albedo    */
        { 255, 255, 255, 255 }, /* emissive  */
        { 255, 255, 255, 255 }, /* roughness */
        { 128, 128, 255, 255 }  /* normal    */
    };

    uint32_t dim = bk->width;
    uint32_t mip_count = bk->mip_count;
    uint32_t layer_count = bk->layer_count;
    uint32_t row_bytes = dim * 4;
    uint32_t layer_bytes = row_bytes * dim;
    uint8_t *fill = ecs_os_malloc((ecs_size_t)layer_bytes);

    for (int ch = 0; ch < 4; ch++) {
        for (uint32_t mip = 0; mip < mip_count; mip++) {
            uint32_t mw = dim >> mip; if (!mw) mw = 1;
            uint32_t mh = dim >> mip; if (!mh) mh = 1;
            uint32_t mip_row_bytes = mw * 4;
            uint32_t mip_layer_bytes = mip_row_bytes * mh;

            for (uint32_t p = 0; p < mw * mh; p++) {
                memcpy(fill + p * 4, fallback_rgba[ch], 4);
            }

            for (uint32_t layer = 0; layer < layer_count; layer++) {
                WGPUTexelCopyTextureInfo dst = {
                    .texture = bk->texture_arrays[ch],
                    .mipLevel = mip,
                    .origin = { 0, 0, layer }
                };
                WGPUTexelCopyBufferLayout src_layout = {
                    .bytesPerRow = mip_row_bytes,
                    .rowsPerImage = mh
                };
                WGPUExtent3D size = { mw, mh, 1 };
                wgpuQueueWriteTexture(
                    impl->queue, &dst, fill,
                    mip_layer_bytes, &src_layout, &size);
            }
        }
    }

    ecs_os_free(fill);
}

/* Copy real textures into bucket array slots via GPU copy. Phase 2 only
 * succeeds when the source's dimensions exactly match its assigned bucket
 * (after mip-offset adjustment); other cases fall through to the fallback
 * fill colour. Phase 3 replaces this with a GPU blit pass that handles
 * arbitrary source dimensions/formats. */
static void flecsEngine_textureArray_copyTextures(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureFormat arr_format)
{
    bool compressed = flecsEngine_isCompressedFormat(arr_format);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    /* Per-bucket "first populated layer" used to redirect materials whose
     * textures couldn't be copied to a layer that does have real data. */
    uint32_t first_populated[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        first_populated[b] = UINT32_MAX;
    }

    /* Track which bucket slots actually received real texture data so we
     * can redirect unpopulated ones below. */
    bool *bucket_populated[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];
        bucket_populated[b] = bk->layer_count
            ? ecs_os_calloc_n(bool, (int32_t)bk->layer_count)
            : NULL;
    }

    ecs_iter_t it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            uint8_t bucket =
                impl->materials.cpu_materials[mat_id].texture_bucket;
            uint32_t slot =
                impl->materials.cpu_materials[mat_id].texture_layer;
            if (bucket >= FLECS_ENGINE_TEXTURE_BUCKET_COUNT) continue;

            flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket];
            if (slot >= bk->layer_count) continue;

            uint32_t arr_w = bk->width;
            uint32_t arr_h = bk->height;
            uint32_t mip_count = bk->mip_count;

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };

            bool copied_any = false;
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                WGPUTextureFormat src_fmt =
                    wgpuTextureGetFormat(ti->texture);
                if (((uint32_t)src_fmt & ~1u) !=
                    ((uint32_t)arr_format & ~1u)) continue;

                uint32_t tw = wgpuTextureGetWidth(ti->texture);
                uint32_t th = wgpuTextureGetHeight(ti->texture);

                /* Phase 2 exact-match copy: source must be square and
                 * have an integer mip offset that lands it on the bucket
                 * dimension. Mismatched cases get the fallback colour
                 * and will be fixed in Phase 3. */
                if (tw != th) continue;
                if (tw < arr_w) continue;
                uint32_t mip_offset = 0;
                while ((tw >> mip_offset) > arr_w) mip_offset++;
                if ((tw >> mip_offset) != arr_w) continue;

                uint32_t src_mips =
                    wgpuTextureGetMipLevelCount(ti->texture);
                uint32_t avail =
                    src_mips > mip_offset ? src_mips - mip_offset : 0;
                uint32_t copy_mips =
                    avail < mip_count ? avail : mip_count;

                for (uint32_t mip = 0; mip < copy_mips; mip++) {
                    uint32_t mw = arr_w >> mip; if (!mw) mw = 1;
                    uint32_t mh = arr_h >> mip; if (!mh) mh = 1;
                    if (compressed && (mw < 4 || mh < 4)) break;

                    WGPUTexelCopyTextureInfo src = {
                        .texture = ti->texture,
                        .mipLevel = mip + mip_offset,
                        .origin = { 0, 0, 0 }
                    };
                    WGPUTexelCopyTextureInfo dst = {
                        .texture = bk->texture_arrays[ch],
                        .mipLevel = mip,
                        .origin = { 0, 0, slot }
                    };
                    WGPUExtent3D size = { mw, mh, 1 };
                    wgpuCommandEncoderCopyTextureToTexture(
                        encoder, &src, &dst, &size);
                }
                copied_any = true;
            }

            if (copied_any) {
                bucket_populated[bucket][slot] = true;
                if (first_populated[bucket] == UINT32_MAX) {
                    first_populated[bucket] = slot;
                }
            }
        }
    }

    /* Per-bucket: redirect unpopulated slots to the bucket's first
     * populated slot so that materials whose textures could not be copied
     * still sample real (opaque) data instead of zeros. */
    for (uint32_t mat_id = 0; mat_id < impl->materials.count; mat_id++) {
        uint8_t bucket =
            impl->materials.cpu_materials[mat_id].texture_bucket;
        if (bucket >= FLECS_ENGINE_TEXTURE_BUCKET_COUNT) continue;

        uint32_t slot =
            impl->materials.cpu_materials[mat_id].texture_layer;
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[bucket];
        if (slot >= bk->layer_count) continue;
        if (!bucket_populated[bucket]) continue;
        if (bucket_populated[bucket][slot]) continue;
        if (first_populated[bucket] == UINT32_MAX) continue;

        impl->materials.cpu_materials[mat_id].texture_layer =
            first_populated[bucket];
    }

    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        ecs_os_free(bucket_populated[b]);
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

/* Create 2D-array views per bucket and assemble the single bind group. */
static void flecsEngine_textureArray_createBindGroup(
    FlecsEngineImpl *impl,
    WGPUTextureFormat arr_format)
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecs_engine_texture_bucket_t *bk = &impl->materials.buckets[b];
        if (!bk->layer_count) continue;

        for (int ch = 0; ch < 4; ch++) {
            WGPUTextureViewDescriptor vd = {
                .format = arr_format,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = 0,
                .mipLevelCount = bk->mip_count,
                .baseArrayLayer = 0,
                .arrayLayerCount = bk->layer_count
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
 * (texture_bucket, texture_layer) pairs on FlecsGpuMaterial. When textures
 * use mixed format families the array cannot be built and the renderer
 * falls back to per-prefab texture binding automatically. */
void flecsEngine_material_buildTextureArrays(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (!impl->materials.texture_query || !impl->device) {
        return;
    }

    flecsEngine_textureArray_release(impl);
    flecsEngine_pbr_texture_ensureFallbacks(impl);

    WGPUTextureFormat arr_format;
    uint32_t bucket_layer_counts[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    if (!flecsEngine_textureArray_survey(
            world, impl, &arr_format, bucket_layer_counts))
    {
        return;
    }

    for (uint8_t b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        if (!bucket_layer_counts[b]) continue;
        if (!flecsEngine_textureArray_createBucket(
                impl, b, bucket_layer_counts[b], arr_format))
        {
            flecsEngine_textureArray_release(impl);
            return;
        }
        flecsEngine_textureArray_fillBucketFallback(impl, b, arr_format);
    }

    flecsEngine_textureArray_copyTextures(world, impl, arr_format);

    flecsEngine_textureArray_createBindGroup(impl, arr_format);
}
