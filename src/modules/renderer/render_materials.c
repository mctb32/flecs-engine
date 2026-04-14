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

    flecsEngine_materialBind_releaseScene(impl);

    flecsEngine_textureArray_release(impl);
    flecsEngine_textureBlit_release(impl);
}

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialUpload");

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
            const FlecsTextureTransform *tex_transforms =
                ecs_field(&it, FlecsTextureTransform, 5);

            bool colors_self = ecs_field_is_self(&it, 0);
            bool materials_self = ecs_field_is_self(&it, 1);
            bool emissives_self = ecs_field_is_self(&it, 3);
            bool transmissions_self = ecs_field_is_self(&it, 4);
            bool tex_transforms_self = ecs_field_is_self(&it, 5);

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
                    .texture_bucket = 1,
                    .uv_scale_x = 1.0f,
                    .uv_scale_y = 1.0f
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

                if (tex_transforms) {
                    int32_t tti = tex_transforms_self ? i : 0;
                    gm->uv_scale_x = tex_transforms[tti].scale_x;
                    gm->uv_scale_y = tex_transforms[tti].scale_y;
                    gm->uv_offset_x = tex_transforms[tti].offset_x;
                    gm->uv_offset_y = tex_transforms[tti].offset_y;
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

        flecsEngine_textureArray_release(impl);

        impl->materials.uploaded_version = snapshot_version;
    }
    FLECS_TRACY_ZONE_END;
}
