#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

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

    ecs_trace("upload materials buffer");

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

            for (int32_t i = 0; i < it.count; i ++) {
                uint32_t index = material_ids[i].value;
                if ((index + 1u) > required_count) {
                    required_count = index + 1u;
                }

                if (index >= capacity) {
                    continue;
                }

                FlecsEmissive emissive = {0};
                if (emissives) {
                    emissive = emissives[i];
                }

                flecs_rgba_t em_color = emissive.color;
                if (em_color.r == 0 && em_color.g == 0 &&
                    em_color.b == 0 && emissive.strength > 0.0f)
                {
                    em_color = (flecs_rgba_t){255, 255, 255, 255};
                }

                impl->materials.cpu_materials[index] = (FlecsGpuMaterial){
                    .color = colors[i],
                    .metallic = materials[i].metallic,
                    .roughness = materials[i].roughness,
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

        /* Extend required_count to also cover material IDs from texture
         * variant prefabs that may not match the main materials query
         * (they inherit FlecsRgba/FlecsPbrMaterial instead of owning them). */
        if (impl->materials.texture_query) {
            ecs_iter_t tex_it = ecs_query_iter(
                world, impl->materials.texture_query);
            while (ecs_query_next(&tex_it)) {
                const FlecsMaterialId *mat_ids =
                    ecs_field(&tex_it, FlecsMaterialId, 1);
                for (int32_t ti = 0; ti < tex_it.count; ti++) {
                    if ((mat_ids[ti].value + 1u) > required_count) {
                        required_count = mat_ids[ti].value + 1u;
                    }
                }
            }

            if (required_count > capacity) {
                flecsEngine_material_ensureBufferCapacity(
                    impl, required_count);
                goto redo;
            }
        }

        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)required_count * sizeof(FlecsGpuMaterial));
        impl->materials.count = required_count;

        /* Populate material properties and texture layer indices for
         * texture variant prefabs (they inherit FlecsRgba/FlecsPbrMaterial
         * instead of owning them, so the main query misses them). */
        if (impl->materials.texture_query) {
            ecs_iter_t tex_it = ecs_query_iter(
                world, impl->materials.texture_query);
            while (ecs_query_next(&tex_it)) {
                const FlecsMaterialId *mat_ids =
                    ecs_field(&tex_it, FlecsMaterialId, 1);
                for (int32_t ti = 0; ti < tex_it.count; ti++) {
                    uint32_t idx = mat_ids[ti].value;
                    if (idx >= required_count) continue;

                    FlecsGpuMaterial *gpu_mat =
                        &impl->materials.cpu_materials[idx];

                    /* Fill inherited material properties if missing */
                    if (!gpu_mat->color.r && !gpu_mat->color.g &&
                        !gpu_mat->color.b && !gpu_mat->color.a)
                    {
                        ecs_entity_t e = tex_it.entities[ti];
                        const FlecsRgba *color =
                            ecs_get(world, e, FlecsRgba);
                        const FlecsPbrMaterial *mat =
                            ecs_get(world, e, FlecsPbrMaterial);
                        const FlecsEmissive *em =
                            ecs_get(world, e, FlecsEmissive);
                        if (color && mat) {
                            flecs_rgba_t em_color = {0};
                            float em_str = 0.0f;
                            if (em) {
                                em_str = em->strength;
                                em_color = em->color;
                                if (!em_color.r && !em_color.g &&
                                    !em_color.b && em_str > 0.0f)
                                {
                                    em_color = (flecs_rgba_t){
                                        255, 255, 255, 255};
                                }
                            }
                            *gpu_mat = (FlecsGpuMaterial){
                                .color = *color,
                                .metallic = mat->metallic,
                                .roughness = mat->roughness,
                                .emissive_strength = em_str,
                                .emissive_color = em_color
                            };
                        }
                    }
                }
            }

            /* Re-upload with newly-filled material slots */
            wgpuQueueWriteBuffer(
                impl->queue,
                impl->materials.buffer,
                0,
                impl->materials.cpu_materials,
                (uint64_t)required_count * sizeof(FlecsGpuMaterial));
        }

        impl->materials.last_id = impl->materials.next_id;
    }
    FLECS_TRACY_ZONE_END;
}

/* Build texture 2D arrays containing one layer per material that has PBR
 * textures, so the shader can index by material_id.texture_layer and a
 * single instanced draw call can cover multiple texture variants. */
void flecsEngine_material_buildTextureArrays(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (!impl->materials.texture_query || !impl->device) {
        return;
    }

    /* Release previous arrays */
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

    flecsEngine_pbr_texture_ensureFallbacks(impl);

    /* First pass: determine array dimensions from the first real texture,
     * and count the total number of layers needed. We use one layer per
     * material that has FlecsPbrTextures. */
    uint32_t arr_w = 0, arr_h = 0;
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

            if (!arr_w) {
                /* Probe texture dimensions from albedo */
                ecs_entity_t alb = textures[i].albedo;
                if (alb) {
                    const FlecsTextureImpl *ti_impl =
                        ecs_get(world, alb, FlecsTextureImpl);
                    if (ti_impl && ti_impl->texture) {
                        arr_w = wgpuTextureGetWidth(ti_impl->texture);
                        arr_h = wgpuTextureGetHeight(ti_impl->texture);
                    }
                }
            }
        }
    }

    /* Also include material IDs from the main materials query so that
     * base GLTF materials (which also have FlecsPbrTextures via the
     * loader) get a layer. */
    if (impl->materials.count > layer_count) {
        layer_count = impl->materials.count;
    }

    if (!layer_count || !arr_w) {
        return;
    }

    uint32_t mip_count = 1;
    {
        uint32_t dim = arr_w > arr_h ? arr_w : arr_h;
        while (dim > 1) { dim >>= 1; mip_count++; }
    }

    /* Create the four array textures */
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
            .format = WGPUTextureFormat_RGBA8Unorm,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        };
        impl->materials.texture_arrays[ch] =
            wgpuDeviceCreateTexture(impl->device, &desc);
    }

    /* Fill every layer with the fallback colour (white for albedo /
     * emissive / roughness, flat normal for the normal channel). Real
     * textures overwrite individual layers via GPU copy below. */
    {
        uint8_t fallback_rgba[4][4] = {
            { 255, 255, 255, 255 }, /* albedo    */
            { 255, 255, 255, 255 }, /* emissive  */
            { 255, 255, 255, 255 }, /* roughness */
            { 128, 128, 255, 255 }  /* normal    */
        };

        uint32_t row_bytes = arr_w * 4;
        uint32_t layer_bytes = row_bytes * arr_h;
        uint8_t *fill = ecs_os_malloc((ecs_size_t)layer_bytes);

        for (int ch = 0; ch < 4; ch++) {
            /* Build a solid-colour image */
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

    /* Second pass: copy real textures into their layers via GPU copy */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    it = ecs_query_iter(world, impl->materials.texture_query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);
        for (int32_t i = 0; i < it.count; i++) {
            uint32_t layer = mat_ids[i].value;
            if (layer >= layer_count) continue;

            /* Store layer index in the CPU material buffer */
            impl->materials.cpu_materials[layer].texture_layer = layer;

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };

            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti_impl =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti_impl || !ti_impl->texture) continue;

                uint32_t tw = wgpuTextureGetWidth(ti_impl->texture);
                uint32_t th = wgpuTextureGetHeight(ti_impl->texture);
                if (tw != arr_w || th != arr_h) continue;

                uint32_t src_mips =
                    wgpuTextureGetMipLevelCount(ti_impl->texture);
                uint32_t copy_mips =
                    src_mips < mip_count ? src_mips : mip_count;

                for (uint32_t mip = 0; mip < copy_mips; mip++) {
                    uint32_t mw = arr_w >> mip; if (!mw) mw = 1;
                    uint32_t mh = arr_h >> mip; if (!mh) mh = 1;
                    WGPUTexelCopyTextureInfo src = {
                        .texture = ti_impl->texture,
                        .mipLevel = mip,
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
            }
        }
    }

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

    /* Create array views and bind group */
    for (int ch = 0; ch < 4; ch++) {
        WGPUTextureViewDescriptor vd = {
            .format = WGPUTextureFormat_RGBA8Unorm,
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
