#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

/* ---- Material buffer management ---- */

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

    for (int i = 0; i < 4; i++) {
        if (impl->materials.texture_array_views[i]) {
            wgpuTextureViewRelease(impl->materials.texture_array_views[i]);
            impl->materials.texture_array_views[i] = NULL;
        }
        if (impl->materials.texture_arrays[i]) {
            wgpuTextureRelease(impl->materials.texture_arrays[i]);
            impl->materials.texture_arrays[i] = NULL;
        }
    }
    if (impl->materials.texture_array_bind_group) {
        wgpuBindGroupRelease(impl->materials.texture_array_bind_group);
        impl->materials.texture_array_bind_group = NULL;
    }
    impl->materials.texture_array_layer_count = 0;
}

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialUpload");
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
                    .emissive_color = em_color
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

        impl->materials.last_id = impl->materials.next_id;
    }
    FLECS_TRACY_ZONE_END;
}

/* ---- Texture array building ---- */

/* Candidate entry used during the survey pass to find the best array
 * format and dimensions across all PBR textures. */
typedef struct {
    uint32_t w, h;
    WGPUTextureFormat fmt;
    uint32_t count;
} flecs_tex_candidate_t;

/* Survey all PBR textures to determine the best array format/dimensions.
 * Returns false if the textures use mixed format families, in which case
 * a texture array cannot be built and the renderer should fall back to
 * per-group texture binding. */
static bool flecsEngine_textureArray_survey(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t *out_w,
    uint32_t *out_h,
    WGPUTextureFormat *out_format,
    uint32_t *out_layer_count)
{
    flecs_tex_candidate_t candidates[32] = {0};
    int32_t candidate_count = 0;
    uint32_t layer_count = 0;

    ecs_iter_t it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t idx = mat_ids[i].value;
            if ((idx + 1) > layer_count) {
                layer_count = idx + 1;
            }

            ecs_entity_t alb = textures[i].albedo;
            if (!alb) continue;
            const FlecsTextureImpl *ti =
                ecs_get(world, alb, FlecsTextureImpl);
            if (!ti || !ti->texture) continue;

            uint32_t tw = wgpuTextureGetWidth(ti->texture);
            uint32_t th = wgpuTextureGetHeight(ti->texture);
            WGPUTextureFormat tf = wgpuTextureGetFormat(ti->texture);

            int32_t ci;
            for (ci = 0; ci < candidate_count; ci++) {
                if (candidates[ci].w == tw && candidates[ci].h == th &&
                    ((uint32_t)candidates[ci].fmt & ~1u) ==
                    ((uint32_t)tf & ~1u))
                {
                    candidates[ci].count++;
                    break;
                }
            }
            if (ci == candidate_count && candidate_count < 32) {
                candidates[candidate_count++] =
                    (flecs_tex_candidate_t){ tw, th, tf, 1 };
            }
        }
    }

    if (!candidate_count) {
        return false;
    }

    /* Bail out when textures use multiple format families — the renderer
     * will fall back to per-group texture binding instead. */
    uint32_t fmt_base = (uint32_t)candidates[0].fmt & ~1u;
    for (int32_t ci = 1; ci < candidate_count; ci++) {
        if (((uint32_t)candidates[ci].fmt & ~1u) != fmt_base) {
            return false;
        }
    }

    /* Pick the smallest square dimension >= 1024.  Larger source textures
     * are down-sampled via mip-offset during the copy pass. */
    uint32_t arr_w = 0, arr_h = 0;
    for (int32_t ci = 0; ci < candidate_count; ci++) {
        uint32_t cw = candidates[ci].w, ch = candidates[ci].h;
        if (cw != ch || cw < 1024) continue;
        if (!arr_w || cw < arr_w) {
            arr_w = cw;
            arr_h = ch;
        }
    }

    /* Fallback: use the largest available dimension */
    if (!arr_w) {
        for (int32_t ci = 0; ci < candidate_count; ci++) {
            uint32_t cw = candidates[ci].w, ch = candidates[ci].h;
            if (cw * ch > arr_w * arr_h) {
                arr_w = cw;
                arr_h = ch;
            }
        }
    }

    if (impl->materials.count > layer_count) {
        layer_count = impl->materials.count;
    }

    *out_w = arr_w;
    *out_h = arr_h;
    *out_format = candidates[0].fmt;
    *out_layer_count = layer_count;
    return arr_w > 0;
}

/* Release previous texture arrays so they can be rebuilt. */
static void flecsEngine_textureArray_release(
    FlecsEngineImpl *impl)
{
    for (int i = 0; i < 4; i++) {
        if (impl->materials.texture_array_views[i]) {
            wgpuTextureViewRelease(impl->materials.texture_array_views[i]);
            impl->materials.texture_array_views[i] = NULL;
        }
        if (impl->materials.texture_arrays[i]) {
            wgpuTextureRelease(impl->materials.texture_arrays[i]);
            impl->materials.texture_arrays[i] = NULL;
        }
    }
    if (impl->materials.texture_array_bind_group) {
        wgpuBindGroupRelease(impl->materials.texture_array_bind_group);
        impl->materials.texture_array_bind_group = NULL;
    }
}

/* Create the four channel array textures (albedo, emissive, roughness,
 * normal) at the given dimensions and format. */
static void flecsEngine_textureArray_create(
    FlecsEngineImpl *impl,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    uint32_t mip_count,
    WGPUTextureFormat arr_format)
{
    for (int ch = 0; ch < 4; ch++) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = {
                .width = arr_w,
                .height = arr_h,
                .depthOrArrayLayers = layer_count
            },
            .format = arr_format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        };
        impl->materials.texture_arrays[ch] =
            wgpuDeviceCreateTexture(impl->device, &desc);
    }
}

/* Fill every layer of uncompressed arrays with a solid fallback colour
 * so that unpopulated layers have sensible defaults (opaque white for
 * albedo/emissive/roughness, flat normal for the normal channel). */
static void flecsEngine_textureArray_fillFallback(
    FlecsEngineImpl *impl,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    WGPUTextureFormat arr_format)
{
    bool compressed = (uint32_t)arr_format >= 0x2Cu
                   && (uint32_t)arr_format <= 0x39u;
    if (compressed) {
        return;
    }

    static const uint8_t fallback_rgba[4][4] = {
        { 255, 255, 255, 255 }, /* albedo    */
        { 255, 255, 255, 255 }, /* emissive  */
        { 255, 255, 255, 255 }, /* roughness */
        { 128, 128, 255, 255 }  /* normal    */
    };

    uint32_t row_bytes = arr_w * 4;
    uint32_t layer_bytes = row_bytes * arr_h;
    uint8_t *fill = ecs_os_malloc((ecs_size_t)layer_bytes);

    for (int ch = 0; ch < 4; ch++) {
        for (uint32_t p = 0; p < arr_w * arr_h; p++) {
            memcpy(fill + p * 4, fallback_rgba[ch], 4);
        }

        for (uint32_t layer = 0; layer < layer_count; layer++) {
            WGPUTexelCopyTextureInfo dst = {
                .texture = impl->materials.texture_arrays[ch],
                .mipLevel = 0,
                .origin = { 0, 0, layer }
            };
            WGPUTexelCopyBufferLayout src_layout = {
                .bytesPerRow = row_bytes,
                .rowsPerImage = arr_h
            };
            WGPUExtent3D size = { arr_w, arr_h, 1 };
            wgpuQueueWriteTexture(
                impl->queue, &dst, fill,
                layer_bytes, &src_layout, &size);
        }
    }

    ecs_os_free(fill);
}

/* Compute the mip offset needed to down-sample a source texture to the
 * array dimensions.  Returns the offset, or UINT32_MAX if the source
 * cannot be made to match (e.g. non-square or smaller than the array). */
static uint32_t flecsEngine_textureArray_mipOffset(
    uint32_t src_w,
    uint32_t src_h,
    uint32_t arr_w,
    uint32_t arr_h)
{
    uint32_t offset = 0;
    if (src_w <= arr_w && src_h <= arr_h) {
        return (src_w == arr_w && src_h == arr_h) ? 0 : UINT32_MAX;
    }

    while ((src_w >> offset) > arr_w || (src_h >> offset) > arr_h) {
        offset++;
    }

    if ((src_w >> offset) != arr_w || (src_h >> offset) != arr_h) {
        return UINT32_MAX;
    }

    return offset;
}

/* Copy real textures into the array layers via GPU copy.  Returns the
 * number of layers that received real texture data. */
static void flecsEngine_textureArray_copyTextures(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    uint32_t mip_count,
    WGPUTextureFormat arr_format)
{
    bool compressed = (uint32_t)arr_format >= 0x2Cu
                   && (uint32_t)arr_format <= 0x39u;

    bool *layer_populated = ecs_os_calloc_n(bool, (int32_t)layer_count);
    uint32_t first_populated = UINT32_MAX;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    ecs_iter_t it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t layer = mat_ids[i].value;
            if (layer >= layer_count) continue;

            impl->materials.cpu_materials[layer].texture_layer = layer;

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
                uint32_t mip_offset =
                    flecsEngine_textureArray_mipOffset(tw, th, arr_w, arr_h);
                if (mip_offset == UINT32_MAX) continue;

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
                        .texture = impl->materials.texture_arrays[ch],
                        .mipLevel = mip,
                        .origin = { 0, 0, layer }
                    };
                    WGPUExtent3D size = { mw, mh, 1 };
                    wgpuCommandEncoderCopyTextureToTexture(
                        encoder, &src, &dst, &size);
                }
                copied_any = true;
            }

            if (copied_any) {
                layer_populated[layer] = true;
                if (first_populated == UINT32_MAX) {
                    first_populated = layer;
                }
            }
        }
    }

    /* Redirect unpopulated layers to the first populated layer so that
     * materials whose textures could not be copied still sample real
     * (opaque) data instead of zeros. */
    if (first_populated != UINT32_MAX) {
        for (uint32_t layer = 0; layer < layer_count; layer++) {
            if (!layer_populated[layer] &&
                layer < (uint32_t)impl->materials.count)
            {
                impl->materials.cpu_materials[layer].texture_layer =
                    first_populated;
            }
        }
    }

    ecs_os_free(layer_populated);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    wgpuQueueSubmit(impl->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    /* Re-upload material buffer with texture_layer values */
    if (impl->materials.count) {
        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)impl->materials.count * sizeof(FlecsGpuMaterial));
    }
}

/* Create 2DArray views and the bind group for the filled arrays. */
static void flecsEngine_textureArray_createBindGroup(
    FlecsEngineImpl *impl,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    uint32_t mip_count,
    WGPUTextureFormat arr_format)
{
    for (int ch = 0; ch < 4; ch++) {
        WGPUTextureViewDescriptor vd = {
            .format = arr_format,
            .dimension = WGPUTextureViewDimension_2DArray,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = layer_count
        };
        impl->materials.texture_array_views[ch] =
            wgpuTextureCreateView(
                impl->materials.texture_arrays[ch], &vd);
    }

    WGPUSampler sampler = flecsEngine_pbr_texture_ensureSampler(impl);
    WGPUBindGroupLayout layout =
        flecsEngine_pbr_texture_ensureBindLayout(impl);

    WGPUBindGroupEntry entries[5] = {
        { .binding = 0,
          .textureView = impl->materials.texture_array_views[0] },
        { .binding = 1,
          .textureView = impl->materials.texture_array_views[1] },
        { .binding = 2,
          .textureView = impl->materials.texture_array_views[2] },
        { .binding = 3,
          .textureView = impl->materials.texture_array_views[3] },
        { .binding = 4, .sampler = sampler }
    };

    impl->materials.texture_array_bind_group =
        wgpuDeviceCreateBindGroup(impl->device,
            &(WGPUBindGroupDescriptor){
                .layout = layout,
                .entries = entries,
                .entryCount = 5
            });

    impl->materials.texture_array_layer_count = layer_count;
    impl->materials.texture_array_width = arr_w;
    impl->materials.texture_array_height = arr_h;
    impl->materials.texture_array_mip_count = mip_count;
}

/* Build texture 2D arrays so the shader can index layers by material ID,
 * enabling instanced draw calls across different texture variants.
 * When textures use mixed formats the array cannot be built and the
 * renderer falls back to per-group texture binding automatically. */
void flecsEngine_material_buildTextureArrays(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (!impl->materials.texture_query || !impl->device) {
        return;
    }

    flecsEngine_textureArray_release(impl);
    flecsEngine_pbr_texture_ensureFallbacks(impl);

    uint32_t arr_w, arr_h, layer_count;
    WGPUTextureFormat arr_format;
    if (!flecsEngine_textureArray_survey(
            world, impl, &arr_w, &arr_h, &arr_format, &layer_count))
    {
        return;
    }

    uint32_t mip_count = 1;
    {
        uint32_t dim = arr_w > arr_h ? arr_w : arr_h;
        while (dim > 1) { dim >>= 1; mip_count++; }
    }

    flecsEngine_textureArray_create(
        impl, arr_w, arr_h, layer_count, mip_count, arr_format);

    flecsEngine_textureArray_fillFallback(
        impl, arr_w, arr_h, layer_count, arr_format);

    flecsEngine_textureArray_copyTextures(
        world, impl, arr_w, arr_h, layer_count, mip_count, arr_format);

    flecsEngine_textureArray_createBindGroup(
        impl, arr_w, arr_h, layer_count, mip_count, arr_format);
}
