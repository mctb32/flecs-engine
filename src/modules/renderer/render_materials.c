#include <limits.h>
#include <string.h>

#include "renderer.h"
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
}

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (impl->materials.last_id == impl->materials.next_id) {
        return;
    }

    impl->materials.count = 0;
    if (!impl->materials.query || !impl->queue) {
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
}
