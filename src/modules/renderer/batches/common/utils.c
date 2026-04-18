#include "common.h"

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t *buf,
    flecsEngine_batch_flags_t flags)
{
    ecs_os_memset_t(buf, 0, flecsEngine_batch_t);
    buf->flags = flags;
}

static void flecsEngine_batch_releaseVisibleGpu(
    flecsEngine_batch_t *buf)
{
    FLECS_WGPU_RELEASE(buf->buffers.gpu_visible_slots, wgpuBufferRelease);
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        FLECS_WGPU_RELEASE(buf->buffers.gpu_shadow_visible_slots[c], wgpuBufferRelease);
    }
}

static void flecsEngine_batch_freeVisibleCpu(
    flecsEngine_batch_t *buf)
{
    ecs_os_free(buf->buffers.cpu_visible_slots);
    buf->buffers.cpu_visible_slots = NULL;
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        ecs_os_free(buf->buffers.cpu_shadow_visible_slots[c]);
        buf->buffers.cpu_shadow_visible_slots[c] = NULL;
        buf->buffers.shadow_visible_count[c] = 0;
    }
    buf->buffers.visible_count = 0;
}

static void flecsEngine_batch_releaseGpu(
    flecsEngine_batch_t *buf)
{
    FLECS_WGPU_RELEASE(buf->buffers.gpu_transforms, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_material_ids, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_material_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_instance_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_materials, wgpuBufferRelease);
}

static void flecsEngine_batch_freeCpu(
    flecsEngine_batch_t *buf)
{
    ecs_os_free(buf->buffers.cpu_transforms);
    buf->buffers.cpu_transforms = NULL;
    ecs_os_free(buf->buffers.cpu_material_ids);
    buf->buffers.cpu_material_ids = NULL;
    ecs_os_free(buf->buffers.cpu_materials);
    buf->buffers.cpu_materials = NULL;
    ecs_os_free(buf->buffers.cpu_aabb);
    buf->buffers.cpu_aabb = NULL;
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batch_releaseGpu(buf);
    flecsEngine_batch_freeCpu(buf);
    flecsEngine_batch_releaseVisibleGpu(buf);
    flecsEngine_batch_freeVisibleCpu(buf);
    buf->buffers.count = 0;
    buf->buffers.capacity = 0;
    buf->buffers.visible_capacity = 0;
}

static WGPUBuffer flecsEngine_makeInstanceStorageBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = size
        });
}

static WGPUBuffer flecsEngine_makeVisibleSlotBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = size
        });
}

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count)
{
    if (count <= buf->buffers.capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    flecsEngine_batch_releaseGpu(buf);
    flecsEngine_batch_freeCpu(buf);

    WGPUDevice device = engine->device;

    buf->buffers.gpu_transforms = flecsEngine_makeInstanceStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsGpuTransform));
    buf->buffers.cpu_transforms =
        ecs_os_malloc_n(FlecsGpuTransform, new_capacity);

    buf->buffers.cpu_aabb = ecs_os_malloc_n(FlecsAABB, new_capacity);

    buf->buffers.gpu_material_ids = flecsEngine_makeInstanceStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsMaterialId));
    buf->buffers.cpu_material_ids =
        ecs_os_malloc_n(FlecsMaterialId, new_capacity);

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        uint64_t storage_size =
            (uint64_t)new_capacity * sizeof(FlecsGpuMaterial);
        buf->buffers.gpu_materials = wgpuDeviceCreateBuffer(device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                .size = storage_size
            });
        buf->buffers.cpu_materials =
            ecs_os_malloc_n(FlecsGpuMaterial, new_capacity);

        flecsEngine_materialBind_ensureLayout((FlecsEngineImpl*)engine);
        buf->buffers.gpu_material_bind_group = flecsEngine_materialBind_createGroup(
            engine, buf->buffers.gpu_materials, storage_size);

        for (int32_t i = 0; i < new_capacity; i ++) {
            buf->buffers.cpu_material_ids[i].value = (uint32_t)i;
        }
    }

    flecsEngine_instanceBind_ensureLayout((FlecsEngineImpl*)engine);
    buf->buffers.gpu_instance_bind_group = flecsEngine_instanceBind_createGroup(
        engine,
        buf->buffers.gpu_transforms,
        (uint64_t)new_capacity * sizeof(FlecsGpuTransform),
        buf->buffers.gpu_material_ids,
        (uint64_t)new_capacity * sizeof(FlecsMaterialId));

    buf->buffers.capacity = new_capacity;
}

void flecsEngine_batch_ensureVisibleCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count)
{
    if (count <= buf->buffers.visible_capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    flecsEngine_batch_releaseVisibleGpu(buf);
    flecsEngine_batch_freeVisibleCpu(buf);

    WGPUDevice device = engine->device;
    uint64_t size = (uint64_t)new_capacity * sizeof(uint32_t);

    buf->buffers.gpu_visible_slots =
        flecsEngine_makeVisibleSlotBuffer(device, size);
    buf->buffers.cpu_visible_slots =
        ecs_os_malloc_n(uint32_t, new_capacity);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        buf->buffers.gpu_shadow_visible_slots[c] =
            flecsEngine_makeVisibleSlotBuffer(device, size);
        buf->buffers.cpu_shadow_visible_slots[c] =
            ecs_os_malloc_n(uint32_t, new_capacity);
    }

    buf->buffers.visible_capacity = new_capacity;
}

void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUpload");
    int32_t count = buf->buffers.count;
    if (!count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPUQueue queue = engine->queue;

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_transforms, 0,
        buf->buffers.cpu_transforms,
        (uint64_t)count * sizeof(FlecsGpuTransform));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_material_ids, 0,
        buf->buffers.cpu_material_ids,
        (uint64_t)count * sizeof(FlecsMaterialId));

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        wgpuQueueWriteBuffer(queue,
            buf->buffers.gpu_materials, 0,
            buf->buffers.cpu_materials,
            (uint64_t)count * sizeof(FlecsGpuMaterial));
    }

    int32_t visible_count = buf->buffers.visible_count;
    if (visible_count && buf->buffers.gpu_visible_slots) {
        wgpuQueueWriteBuffer(queue,
            buf->buffers.gpu_visible_slots, 0,
            buf->buffers.cpu_visible_slots,
            (uint64_t)visible_count * sizeof(uint32_t));
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_uploadShadow(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        int32_t count = buf->buffers.shadow_visible_count[c];
        if (!count || !buf->buffers.gpu_shadow_visible_slots[c]) {
            continue;
        }
        wgpuQueueWriteBuffer(
            engine->queue,
            buf->buffers.gpu_shadow_visible_slots[c],
            0,
            buf->buffers.cpu_shadow_visible_slots[c],
            (uint64_t)count * sizeof(uint32_t));
    }
}

/* --- Per-group batch lifecycle --- */

void flecsEngine_batch_group_init(
    flecsEngine_batch_group_t* result,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id)
{
    ecs_os_memset_t(result, 0, flecsEngine_batch_group_t);
    if (mesh) {
        result->mesh = *mesh;
    }
    result->group_id = group_id;
}

flecsEngine_batch_group_t* flecsEngine_batch_group_create(
    const FlecsMesh3Impl *mesh,
    uint64_t group_id)
{
    flecsEngine_batch_group_t *result = ecs_os_calloc_t(flecsEngine_batch_group_t);
    flecsEngine_batch_group_init(result, mesh, group_id);
    return result;
}

void flecsEngine_batch_group_fini(
    flecsEngine_batch_group_t *ptr)
{
    ptr->view.count = 0;
    ptr->view.offset = 0;
    ptr->batch = NULL;
}

void flecsEngine_batch_group_delete(
    void *ptr)
{
    flecsEngine_batch_group_fini(ptr);
    ecs_os_free(ptr);
}

/* --- Extract / Draw --- */

void flecsEngine_batch_transformInstance(
    FlecsGpuTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z)
{
    out->c0.x = wt->m[0][0] * scale_x;
    out->c0.y = wt->m[0][1] * scale_x;
    out->c0.z = wt->m[0][2] * scale_x;

    out->c1.x = wt->m[1][0] * scale_y;
    out->c1.y = wt->m[1][1] * scale_y;
    out->c1.z = wt->m[1][2] * scale_y;

    out->c2.x = wt->m[2][0] * scale_z;
    out->c2.y = wt->m[2][1] * scale_z;
    out->c2.z = wt->m[2][2] * scale_z;

    out->c3.x = wt->m[3][0];
    out->c3.y = wt->m[3][1];
    out->c3.z = wt->m[3][2];
}
