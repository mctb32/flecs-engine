#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

/* ---- Material buffer management ----
 *
 * Manages the CPU-side FlecsGpuMaterial array and its backing GPU storage
 * buffer. The buffer is bound at @group(0) @binding(11) and indexed
 * per-fragment by material_id. Texture array building (bucket system)
 * lives in render_texture_arrays.c; the blit/mipgen build pass lives in
 * render_texture_blit.c. */

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

    flecsEngine_textureArray_release(impl);
    flecsEngine_textureBlit_release(impl);
}

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialUpload");

    /* Guarantee a non-null buffer regardless of whether any material is
     * defined yet — see flecsEngine_material_ensureBuffer. */
    flecsEngine_material_ensureBuffer(impl);

    if (impl->materials.dirty_version == impl->materials.uploaded_version) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    impl->materials.count = 0;
    if (!impl->materials.query || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    /* Snapshot the dirty version before iterating so that any mutations
     * that happen between now and the end of the upload get picked up on
     * the next frame. */
    uint32_t snapshot_version = impl->materials.dirty_version;

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
            const FlecsTransmission *transmissions =
                ecs_field(&it, FlecsTransmission, 4);

            bool colors_self = ecs_field_is_self(&it, 0);
            bool materials_self = ecs_field_is_self(&it, 1);
            bool emissives_self = ecs_field_is_self(&it, 3);
            bool transmissions_self = ecs_field_is_self(&it, 4);

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
                int32_t ti = transmissions_self ? i : 0;

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

                FlecsGpuMaterial *gm = &impl->materials.cpu_materials[index];
                *gm = (FlecsGpuMaterial){
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

                if (transmissions) {
                    const FlecsTransmission *tx = &transmissions[ti];
                    gm->transmission_factor = tx->transmission_factor;
                    gm->ior = tx->ior;
                    gm->thickness_factor = tx->thickness_factor;
                    gm->attenuation_distance = tx->attenuation_distance;
                    flecs_rgba_t ac = tx->attenuation_color;
                    gm->attenuation_color =
                        (uint32_t)ac.r | ((uint32_t)ac.g << 8) |
                        ((uint32_t)ac.b << 16) | ((uint32_t)ac.a << 24);
                }
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
        impl->materials.uploaded_version = snapshot_version;
    }
    FLECS_TRACY_ZONE_END;
}
