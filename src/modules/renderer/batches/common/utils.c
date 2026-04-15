#include "common.h"

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_buffers_init(
    flecsEngine_batch_buffers_t *buf,
    flecsEngine_batch_buffers_flags_t flags)
{
    ecs_os_memset_t(buf, 0, flecsEngine_batch_buffers_t);
    buf->owns_material_data =
        (flags & FLECS_BATCH_BUFFERS_OWNS_MATERIAL) != 0;
    buf->owns_transmission_data =
        (flags & FLECS_BATCH_BUFFERS_OWNS_TRANSMISSION) != 0;
    buf->use_material_storage =
        (flags & FLECS_BATCH_BUFFERS_STORAGE) != 0;
}

static void flecsEngine_batch_buffers_releaseShadowGpu(
    flecsEngine_batch_buffers_t *buf)
{
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (buf->shadow_transforms[c]) {
            wgpuBufferRelease(buf->shadow_transforms[c]);
            buf->shadow_transforms[c] = NULL;
        }
    }
}

static void flecsEngine_batch_buffers_freeShadowCpu(
    flecsEngine_batch_buffers_t *buf)
{
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        ecs_os_free(buf->cpu_shadow_transforms[c]);
        buf->cpu_shadow_transforms[c] = NULL;
        buf->shadow_count[c] = 0;
    }
}

static void flecsEngine_batch_buffers_releaseGpu(
    flecsEngine_batch_buffers_t *buf)
{
    if (buf->instance_transform) {
        wgpuBufferRelease(buf->instance_transform);
        buf->instance_transform = NULL;
    }
    if (buf->instance_color) {
        wgpuBufferRelease(buf->instance_color);
        buf->instance_color = NULL;
    }
    if (buf->instance_pbr) {
        wgpuBufferRelease(buf->instance_pbr);
        buf->instance_pbr = NULL;
    }
    if (buf->instance_emissive) {
        wgpuBufferRelease(buf->instance_emissive);
        buf->instance_emissive = NULL;
    }
    if (buf->instance_transmission) {
        wgpuBufferRelease(buf->instance_transmission);
        buf->instance_transmission = NULL;
    }
    if (buf->instance_material_id) {
        wgpuBufferRelease(buf->instance_material_id);
        buf->instance_material_id = NULL;
    }
    if (buf->material_bind_group) {
        wgpuBindGroupRelease(buf->material_bind_group);
        buf->material_bind_group = NULL;
    }
    if (buf->material_storage) {
        wgpuBufferRelease(buf->material_storage);
        buf->material_storage = NULL;
    }
}

static void flecsEngine_batch_buffers_freeCpu(
    flecsEngine_batch_buffers_t *buf)
{
    ecs_os_free(buf->cpu_transforms);
    buf->cpu_transforms = NULL;
    ecs_os_free(buf->cpu_colors);
    buf->cpu_colors = NULL;
    ecs_os_free(buf->cpu_pbr_materials);
    buf->cpu_pbr_materials = NULL;
    ecs_os_free(buf->cpu_emissives);
    buf->cpu_emissives = NULL;
    ecs_os_free(buf->cpu_transmissions);
    buf->cpu_transmissions = NULL;
    ecs_os_free(buf->cpu_material_ids);
    buf->cpu_material_ids = NULL;
    ecs_os_free(buf->cpu_gpu_materials);
    buf->cpu_gpu_materials = NULL;
}

void flecsEngine_batch_buffers_fini(
    flecsEngine_batch_buffers_t *buf)
{
    flecsEngine_batch_buffers_releaseGpu(buf);
    flecsEngine_batch_buffers_freeCpu(buf);
    flecsEngine_batch_buffers_releaseShadowGpu(buf);
    flecsEngine_batch_buffers_freeShadowCpu(buf);
    buf->count = 0;
    buf->capacity = 0;
    buf->shadow_capacity = 0;
}

void flecsEngine_batch_buffers_ensureShadowCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t count)
{
    if (count <= buf->shadow_capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    flecsEngine_batch_buffers_releaseShadowGpu(buf);
    flecsEngine_batch_buffers_freeShadowCpu(buf);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        buf->shadow_transforms[c] = wgpuDeviceCreateBuffer(engine->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
            });
        buf->cpu_shadow_transforms[c] =
            ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    }

    buf->shadow_capacity = new_capacity;
}

void flecsEngine_batch_buffers_uploadShadow(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf)
{
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        int32_t count = buf->shadow_count[c];
        if (!count || !buf->shadow_transforms[c]) {
            continue;
        }
        wgpuQueueWriteBuffer(
            engine->queue,
            buf->shadow_transforms[c],
            0,
            buf->cpu_shadow_transforms[c],
            (uint64_t)count * sizeof(FlecsInstanceTransform));
    }
}

static WGPUBuffer flecsEngine_makeVertexBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = size
        });
}

void flecsEngine_batch_buffers_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t count)
{
    if (count <= buf->capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    flecsEngine_batch_buffers_releaseGpu(buf);
    flecsEngine_batch_buffers_freeCpu(buf);

    WGPUDevice device = engine->device;

    /* Transforms are always needed. */
    buf->instance_transform = flecsEngine_makeVertexBuffer(device,
        (uint64_t)new_capacity * sizeof(FlecsInstanceTransform));
    buf->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);

    if (!buf->owns_material_data) {
        buf->instance_material_id = flecsEngine_makeVertexBuffer(device,
            (uint64_t)new_capacity * sizeof(FlecsMaterialId));
        buf->cpu_material_ids =
            ecs_os_malloc_n(FlecsMaterialId, new_capacity);
    } else if (buf->use_material_storage) {
        uint64_t storage_size =
            (uint64_t)new_capacity * sizeof(FlecsGpuMaterial);
        buf->material_storage = wgpuDeviceCreateBuffer(device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                .size = storage_size
            });
        buf->cpu_gpu_materials =
            ecs_os_malloc_n(FlecsGpuMaterial, new_capacity);

        flecsEngine_materialBind_ensureLayout((FlecsEngineImpl*)engine);
        buf->material_bind_group = flecsEngine_materialBind_createGroup(
            engine, buf->material_storage, storage_size);
    } else {
        buf->instance_color = flecsEngine_makeVertexBuffer(device,
            (uint64_t)new_capacity * sizeof(FlecsRgba));
        buf->instance_pbr = flecsEngine_makeVertexBuffer(device,
            (uint64_t)new_capacity * sizeof(FlecsPbrMaterial));
        buf->instance_emissive = flecsEngine_makeVertexBuffer(device,
            (uint64_t)new_capacity * sizeof(FlecsEmissive));
        buf->cpu_colors = ecs_os_malloc_n(FlecsRgba, new_capacity);
        buf->cpu_pbr_materials =
            ecs_os_malloc_n(FlecsPbrMaterial, new_capacity);
        buf->cpu_emissives = ecs_os_malloc_n(FlecsEmissive, new_capacity);

        if (buf->owns_transmission_data) {
            buf->instance_transmission = flecsEngine_makeVertexBuffer(device,
                (uint64_t)new_capacity * sizeof(FlecsTransmission));
            buf->cpu_transmissions =
                ecs_os_malloc_n(FlecsTransmission, new_capacity);
        }
    }

    buf->capacity = new_capacity;
}

void flecsEngine_batch_buffers_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUpload");
    int32_t count = buf->count;
    if (!count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPUQueue queue = engine->queue;

    wgpuQueueWriteBuffer(queue,
        buf->instance_transform, 0,
        buf->cpu_transforms,
        (uint64_t)count * sizeof(FlecsInstanceTransform));

    if (!buf->owns_material_data) {
        wgpuQueueWriteBuffer(queue,
            buf->instance_material_id, 0,
            buf->cpu_material_ids,
            (uint64_t)count * sizeof(FlecsMaterialId));
    } else if (buf->use_material_storage) {
        wgpuQueueWriteBuffer(queue,
            buf->material_storage, 0,
            buf->cpu_gpu_materials,
            (uint64_t)count * sizeof(FlecsGpuMaterial));
    } else {
        wgpuQueueWriteBuffer(queue,
            buf->instance_color, 0,
            buf->cpu_colors,
            (uint64_t)count * sizeof(FlecsRgba));
        wgpuQueueWriteBuffer(queue,
            buf->instance_pbr, 0,
            buf->cpu_pbr_materials,
            (uint64_t)count * sizeof(FlecsPbrMaterial));
        wgpuQueueWriteBuffer(queue,
            buf->instance_emissive, 0,
            buf->cpu_emissives,
            (uint64_t)count * sizeof(FlecsEmissive));

        if (buf->owns_transmission_data) {
            wgpuQueueWriteBuffer(queue,
                buf->instance_transmission, 0,
                buf->cpu_transmissions,
                (uint64_t)count * sizeof(FlecsTransmission));
        }
    }
    FLECS_TRACY_ZONE_END;
}

/* --- Per-group batch lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t* result,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    ecs_os_memset_t(result, 0, flecsEngine_batch_t);
    if (mesh) {
        result->mesh = *mesh;
        result->vertex_buffer = mesh->vertex_buffer;
    }
    result->component = component;
    result->component_size = component
        ? flecsEngine_type_sizeof(world, component) : 0;
    result->scale_callback = scale_callback;
    result->group_id = group_id;
}

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    flecsEngine_batch_t *result = ecs_os_calloc_t(flecsEngine_batch_t);
    flecsEngine_batch_init(result, world, mesh, group_id,
        component, scale_callback);
    return result;
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ptr)
{
    ptr->count = 0;
    ptr->offset = 0;
    ptr->buffers = NULL;
}

void flecsEngine_batch_delete(
    void *ptr)
{
    flecsEngine_batch_fini(ptr);
    ecs_os_free(ptr);
}

/* --- Extract / Draw --- */

void flecsEngine_batch_extractSingleInstance(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *batch,
    const FlecsWorldTransform3 *transform,
    const FlecsRgba *color,
    float scale_x,
    float scale_y,
    float scale_z)
{
    flecsEngine_batch_buffers_t *buf = batch->buffers;
    ecs_assert(buf != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_batch_buffers_ensureCapacity(engine, buf, 1);

    flecsEngine_batch_transformInstance(
        &buf->cpu_transforms[0],
        transform,
        scale_x,
        scale_y,
        scale_z);

    buf->cpu_colors[0] = *color;
    buf->count = 1;
    batch->count = 1;
    batch->offset = 0;
}

void flecsEngine_batch_transformInstance(
    FlecsInstanceTransform *out,
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
