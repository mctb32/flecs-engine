#include "renderer.h"

FlecsDefaultAttrCache* flecsEngine_defaultAttrCache_create(void) {
    return ecs_os_calloc_t(FlecsDefaultAttrCache);
}

void flecsEngine_defaultAttrCache_free(
    FlecsDefaultAttrCache *ptr)
{
    ecs_os_free(ptr->color);
    ecs_os_free(ptr->material);
    ecs_os_free(ptr->emissive);
    if (ptr->material_id_identity_buffer) {
        wgpuBufferRelease(ptr->material_id_identity_buffer);
    }
    ecs_os_free(ptr);
}

WGPUBuffer flecsEngine_defaultAttrCache_getMaterialIdIdentityBuffer(
    FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;

    if (cache->material_id_identity_buffer &&
        count <= cache->material_id_identity_capacity)
    {
        return cache->material_id_identity_buffer;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    if (cache->material_id_identity_buffer) {
        wgpuBufferRelease(cache->material_id_identity_buffer);
        cache->material_id_identity_buffer = NULL;
    }

    cache->material_id_identity_buffer = wgpuDeviceCreateBuffer(
        engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsMaterialId)
        });

    FlecsMaterialId *ids = ecs_os_malloc_n(FlecsMaterialId, new_capacity);
    for (int32_t i = 0; i < new_capacity; i ++) {
        ids[i].value = (uint32_t)i;
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        cache->material_id_identity_buffer,
        0,
        ids,
        (uint64_t)new_capacity * sizeof(FlecsMaterialId));

    ecs_os_free(ids);

    cache->material_id_identity_capacity = new_capacity;
    return cache->material_id_identity_buffer;
}

FlecsRgba* flecsEngine_defaultAttrCache_getColor(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->color_count) {
        ecs_os_free(cache->color);
        cache->color = ecs_os_malloc_n(FlecsRgba, count);
        cache->color_count = count;

        for (int i = 0; i < count; i ++) {
            cache->color[i] = (FlecsRgba){230, 230, 230};
        }
    }

    return cache->color;
}

FlecsPbrMaterial* flecsEngine_defaultAttrCache_getMaterial(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->material_count) {
        ecs_os_free(cache->material);
        cache->material = ecs_os_malloc_n(FlecsPbrMaterial, count);
        cache->material_count = count;

        for (int i = 0; i < count; i ++) {
            cache->material[i] = (FlecsPbrMaterial){
                .metallic = 0, .roughness = 1
            };
        }
    }

    return cache->material;
}

FlecsEmissive* flecsEngine_defaultAttrCache_getEmissive(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->emissive_count) {
        ecs_os_free(cache->emissive);
        cache->emissive = ecs_os_calloc_n(FlecsEmissive, count);
        cache->emissive_count = count;
    }
    return cache->emissive;
}
