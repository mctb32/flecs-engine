#include "common.h"

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t *buf,
    flecsEngine_batch_flags_t flags)
{
    ecs_os_memset_t(buf, 0, flecsEngine_batch_t);
    buf->flags = flags;
}

static void flecsEngine_batch_releaseCullGroup(
    flecsEngine_batch_t *buf)
{
    FLECS_WGPU_RELEASE(buf->buffers.gpu_cull_bind_group, wgpuBindGroupRelease);
}

static void flecsEngine_batch_releaseGpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batch_releaseCullGroup(buf);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_transforms, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_material_ids, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_material_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_instance_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_materials, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_aabb, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_slot_to_group, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_visible_slots, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_batch_info, wgpuBufferRelease);
}

static void flecsEngine_batch_releaseGroupGpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batch_releaseCullGroup(buf);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_group_info, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_indirect_args, wgpuBufferRelease);
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
    ecs_os_free(buf->buffers.cpu_slot_to_group);
    buf->buffers.cpu_slot_to_group = NULL;
}

static void flecsEngine_batch_freeGroupCpu(
    flecsEngine_batch_t *buf)
{
    ecs_os_free(buf->buffers.cpu_group_info);
    buf->buffers.cpu_group_info = NULL;
    ecs_os_free(buf->buffers.cpu_indirect_args);
    buf->buffers.cpu_indirect_args = NULL;
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batch_releaseGpu(buf);
    flecsEngine_batch_releaseGroupGpu(buf);
    flecsEngine_batch_freeCpu(buf);
    flecsEngine_batch_freeGroupCpu(buf);
    buf->buffers.count = 0;
    buf->buffers.capacity = 0;
    buf->buffers.group_count = 0;
    buf->buffers.group_capacity = 0;
}

static WGPUBuffer flecsEngine_makeStorageBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
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

    buf->buffers.gpu_transforms = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsGpuTransform));
    buf->buffers.cpu_transforms =
        ecs_os_malloc_n(FlecsGpuTransform, new_capacity);

    buf->buffers.cpu_aabb =
        ecs_os_malloc_n(flecsEngine_gpuAabb_t, new_capacity);
    buf->buffers.gpu_aabb = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(flecsEngine_gpuAabb_t));

    buf->buffers.cpu_slot_to_group =
        ecs_os_malloc_n(uint32_t, new_capacity);
    buf->buffers.gpu_slot_to_group = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(uint32_t));

    buf->buffers.gpu_material_ids = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsMaterialId));
    buf->buffers.cpu_material_ids =
        ecs_os_malloc_n(FlecsMaterialId, new_capacity);

    /* Visible slots: 5 views (main + 4 cascades) packed into one buffer.
     * Usage includes Storage (compute write) and Vertex (per-instance slot
     * input in render pipelines). */
    buf->buffers.gpu_visible_slots = wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage
                   | WGPUBufferUsage_Vertex
                   | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * 5ull * sizeof(uint32_t)
        });

    buf->buffers.gpu_batch_info = wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = 16
        });

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

void flecsEngine_batch_ensureGroupCapacity(
    flecsEngine_batch_t *buf,
    int32_t group_count)
{
    if (group_count <= buf->buffers.group_capacity) {
        return;
    }

    int32_t new_capacity = group_count;
    if (new_capacity < 4) {
        new_capacity = 4;
    }

    flecsEngine_batch_releaseGroupGpu(buf);
    flecsEngine_batch_freeGroupCpu(buf);

    buf->buffers.cpu_group_info =
        ecs_os_malloc_n(flecsEngine_gpuCullGroupInfo_t, new_capacity);
    buf->buffers.cpu_indirect_args =
        ecs_os_malloc_n(flecsEngine_gpuDrawArgs_t,
            new_capacity * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT));
    buf->buffers.group_capacity = new_capacity;
}

static void flecsEngine_batch_ensureGroupGpu(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf)
{
    if (buf->buffers.gpu_group_info && buf->buffers.gpu_indirect_args) {
        return;
    }

    FLECS_WGPU_RELEASE(buf->buffers.gpu_group_info, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_indirect_args, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(buf->buffers.gpu_cull_bind_group, wgpuBindGroupRelease);

    buf->buffers.gpu_group_info = flecsEngine_makeStorageBuffer(
        engine->device,
        (uint64_t)buf->buffers.group_capacity *
            sizeof(flecsEngine_gpuCullGroupInfo_t));

    buf->buffers.gpu_indirect_args = wgpuDeviceCreateBuffer(
        engine->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage
                   | WGPUBufferUsage_Indirect
                   | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)buf->buffers.group_capacity
                * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT)
                * sizeof(flecsEngine_gpuDrawArgs_t)
        });
}

typedef struct {
    uint32_t count;
    uint32_t capacity;
    uint32_t group_count;
    uint32_t _pad;
} flecsEngine_gpuCullBatchInfoUpload_t;

void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUpload");
    int32_t count = buf->buffers.count;
    int32_t group_count = buf->buffers.group_count;
    if (!count || !group_count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_ensureGroupGpu(engine, (flecsEngine_batch_t*)buf);

    WGPUQueue queue = engine->queue;

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_transforms, 0,
        buf->buffers.cpu_transforms,
        (uint64_t)count * sizeof(FlecsGpuTransform));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_material_ids, 0,
        buf->buffers.cpu_material_ids,
        (uint64_t)count * sizeof(FlecsMaterialId));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_aabb, 0,
        buf->buffers.cpu_aabb,
        (uint64_t)count * sizeof(flecsEngine_gpuAabb_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_slot_to_group, 0,
        buf->buffers.cpu_slot_to_group,
        (uint64_t)count * sizeof(uint32_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_group_info, 0,
        buf->buffers.cpu_group_info,
        (uint64_t)group_count * sizeof(flecsEngine_gpuCullGroupInfo_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_indirect_args, 0,
        buf->buffers.cpu_indirect_args,
        (uint64_t)group_count *
            (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT) *
            sizeof(flecsEngine_gpuDrawArgs_t));

    flecsEngine_gpuCullBatchInfoUpload_t info = {
        .count = (uint32_t)count,
        .capacity = (uint32_t)buf->buffers.capacity,
        .group_count = (uint32_t)group_count,
        ._pad = 0
    };
    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_batch_info, 0, &info, sizeof(info));

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        wgpuQueueWriteBuffer(queue,
            buf->buffers.gpu_materials, 0,
            buf->buffers.cpu_materials,
            (uint64_t)count * sizeof(FlecsGpuMaterial));
    }

    if ((buf->flags & FLECS_BATCH_NO_GPU_CULL)) {
        flecsEngine_batch_writeIdentityVisible(engine, buf);
    }

    FLECS_TRACY_ZONE_END;
}

WGPUBindGroup flecsEngine_batch_ensureCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf)
{
    if (buf->buffers.gpu_cull_bind_group) {
        return buf->buffers.gpu_cull_bind_group;
    }

    if (!buf->buffers.gpu_aabb ||
        !buf->buffers.gpu_slot_to_group ||
        !buf->buffers.gpu_group_info ||
        !buf->buffers.gpu_visible_slots ||
        !buf->buffers.gpu_indirect_args ||
        !buf->buffers.gpu_batch_info)
    {
        return NULL;
    }

    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = buf->buffers.gpu_aabb,
          .size = (uint64_t)buf->buffers.capacity
              * sizeof(flecsEngine_gpuAabb_t) },
        { .binding = 1, .buffer = buf->buffers.gpu_slot_to_group,
          .size = (uint64_t)buf->buffers.capacity * sizeof(uint32_t) },
        { .binding = 2, .buffer = buf->buffers.gpu_group_info,
          .size = (uint64_t)buf->buffers.group_capacity
              * sizeof(flecsEngine_gpuCullGroupInfo_t) },
        { .binding = 3, .buffer = buf->buffers.gpu_visible_slots,
          .size = (uint64_t)buf->buffers.capacity * 5ull * sizeof(uint32_t) },
        { .binding = 4, .buffer = buf->buffers.gpu_indirect_args,
          .size = (uint64_t)buf->buffers.group_capacity
              * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT)
              * sizeof(flecsEngine_gpuDrawArgs_t) },
        { .binding = 5, .buffer = buf->buffers.gpu_batch_info, .size = 16 }
    };

    buf->buffers.gpu_cull_bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->gpu_cull.batch_bind_layout,
            .entryCount = 6,
            .entries = entries
        });

    return buf->buffers.gpu_cull_bind_group;
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

/* --- Fill indirect draw args (CPU) --- */

void flecsEngine_batch_group_prepareArgs(
    flecsEngine_batch_group_t *ctx)
{
    flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || ctx->view.group_idx < 0) {
        return;
    }

    int32_t g = ctx->view.group_idx;
    int32_t gc = buf->buffers.group_count;
    uint32_t index_count = (uint32_t)ctx->mesh.index_count;
    bool identity = (buf->flags & FLECS_BATCH_NO_GPU_CULL) != 0;
    uint32_t initial_count = identity ? (uint32_t)ctx->view.count : 0u;

    buf->buffers.cpu_group_info[g] = (flecsEngine_gpuCullGroupInfo_t){
        .src_offset = (uint32_t)ctx->view.offset,
        .src_count = (uint32_t)ctx->view.count,
        ._pad0 = 0,
        ._pad1 = 0
    };

    /* Identity mode fills main view only; cascades stay at 0. Matches the
     * earlier 27ms baseline for an apples-to-apples compute-cost comparison. */
    for (int v = 0; v < 1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT; v ++) {
        flecsEngine_gpuDrawArgs_t *a =
            &buf->buffers.cpu_indirect_args[v * gc + g];
        a->index_count = index_count;
        a->instance_count = (v == 0) ? initial_count : 0u;
        a->first_index = 0;
        a->base_vertex = 0;
        a->first_instance = 0;
    }
}

/* For identity batches, slot_to_group isn't read but visible_slots for the
 * main view must be filled with identity indices [src_offset..src_offset+count).
 */
static void flecsEngine_batch_fillIdentityVisible(
    flecsEngine_batch_t *buf)
{
    /* Not needed if empty. */
    if (!buf->buffers.count) return;
}

void flecsEngine_batch_writeIdentityVisible(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    if (!(buf->flags & FLECS_BATCH_NO_GPU_CULL)) {
        return;
    }
    if (!buf->buffers.gpu_visible_slots || !buf->buffers.count) {
        return;
    }

    int32_t count = buf->buffers.count;
    uint32_t *tmp = ecs_os_malloc_n(uint32_t, count);
    for (int32_t i = 0; i < count; i ++) {
        tmp[i] = (uint32_t)i;
    }
    wgpuQueueWriteBuffer(engine->queue,
        buf->buffers.gpu_visible_slots, 0,
        tmp, (uint64_t)count * sizeof(uint32_t));
    ecs_os_free(tmp);
    (void)flecsEngine_batch_fillIdentityVisible;
}

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
