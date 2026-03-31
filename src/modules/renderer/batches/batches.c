#include <stddef.h>
#include <string.h>
#include "batches.h"
#include "../frustum_cull.h"
#include "../../../tracy_hooks.h"

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_buffers_init(
    flecsEngine_batch_buffers_t *buf,
    bool owns_material_data)
{
    ecs_os_memset_t(buf, 0, flecsEngine_batch_buffers_t);
    buf->owns_material_data = owns_material_data;
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
    if (buf->instance_material_id) {
        wgpuBufferRelease(buf->instance_material_id);
        buf->instance_material_id = NULL;
    }
    flecsEngine_batch_buffers_releaseShadowGpu(buf);
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
    ecs_os_free(buf->cpu_material_ids);
    buf->cpu_material_ids = NULL;
    flecsEngine_batch_buffers_freeShadowCpu(buf);
}

void flecsEngine_batch_buffers_fini(
    flecsEngine_batch_buffers_t *buf)
{
    flecsEngine_batch_buffers_releaseGpu(buf);
    flecsEngine_batch_buffers_freeCpu(buf);
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

static void flecsEngine_batch_buffers_resizeMaterialData(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t new_capacity)
{
    flecsEngine_batch_buffers_releaseGpu(buf);
    flecsEngine_batch_buffers_freeCpu(buf);

    buf->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
        });

    buf->instance_color = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsRgba)
        });

    buf->instance_pbr = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsPbrMaterial)
        });

    buf->instance_emissive = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsEmissive)
        });

    buf->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    buf->cpu_colors = ecs_os_malloc_n(FlecsRgba, new_capacity);
    buf->cpu_pbr_materials =
        ecs_os_malloc_n(FlecsPbrMaterial, new_capacity);
    buf->cpu_emissives = ecs_os_malloc_n(FlecsEmissive, new_capacity);
    buf->capacity = new_capacity;
}

static void flecsEngine_batch_buffers_resizeMaterialIds(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t new_capacity)
{
    flecsEngine_batch_buffers_releaseGpu(buf);
    flecsEngine_batch_buffers_freeCpu(buf);

    buf->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
        });

    buf->instance_material_id = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsMaterialId)
        });

    buf->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    buf->cpu_material_ids = ecs_os_malloc_n(FlecsMaterialId, new_capacity);
    buf->capacity = new_capacity;
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

    if (buf->owns_material_data) {
        flecsEngine_batch_buffers_resizeMaterialData(engine, buf, new_capacity);
    } else {
        flecsEngine_batch_buffers_resizeMaterialIds(engine, buf, new_capacity);
    }
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

    wgpuQueueWriteBuffer(
        engine->queue,
        buf->instance_transform,
        0,
        buf->cpu_transforms,
        (uint64_t)count * sizeof(FlecsInstanceTransform));

    if (buf->owns_material_data) {
        wgpuQueueWriteBuffer(
            engine->queue,
            buf->instance_color,
            0,
            buf->cpu_colors,
            (uint64_t)count * sizeof(FlecsRgba));

        wgpuQueueWriteBuffer(
            engine->queue,
            buf->instance_pbr,
            0,
            buf->cpu_pbr_materials,
            (uint64_t)count * sizeof(FlecsPbrMaterial));

        wgpuQueueWriteBuffer(
            engine->queue,
            buf->instance_emissive,
            0,
            buf->cpu_emissives,
            (uint64_t)count * sizeof(FlecsEmissive));
    } else {
        wgpuQueueWriteBuffer(
            engine->queue,
            buf->instance_material_id,
            0,
            buf->cpu_material_ids,
            (uint64_t)count * sizeof(FlecsMaterialId));
    }
    FLECS_TRACY_ZONE_END;
}

/* --- Per-group batch lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t* result,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
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
    result->owns_material_data = owns_material_data;
}

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    flecsEngine_batch_t *result = ecs_os_calloc_t(flecsEngine_batch_t);
    flecsEngine_batch_init(result, world, mesh, group_id, owns_material_data,
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

/* --- Material copy helpers --- */

static void flecsEngine_batch_copyMaterialData(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t offset,
    int32_t count,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    const FlecsEmissive *emissives)
{
    FlecsRgba *cpu_colors = &buf->cpu_colors[offset];
    FlecsPbrMaterial *cpu_materials = &buf->cpu_pbr_materials[offset];
    FlecsEmissive *cpu_emissives = &buf->cpu_emissives[offset];

    memcpy(
        cpu_colors,
        colors ? colors : flecsEngine_defaultAttrCache_getColor(engine, count),
        (size_t)count * sizeof(FlecsRgba));

    memcpy(
        cpu_materials,
        materials ? materials : flecsEngine_defaultAttrCache_getMaterial(engine, count),
        (size_t)count * sizeof(FlecsPbrMaterial));

    memcpy(
        cpu_emissives,
        emissives ? emissives : flecsEngine_defaultAttrCache_getEmissive(engine, count),
        (size_t)count * sizeof(FlecsEmissive));
}

static void flecsEngine_batch_copyMaterialIds(
    flecsEngine_batch_buffers_t *buf,
    int32_t offset,
    int32_t count,
    const FlecsMaterialId *material_id)
{
    FlecsMaterialId *mat_ids = &buf->cpu_material_ids[offset];
    for (int32_t i = 0; i < count; i ++) {
        mat_ids[i] = material_id[0];
    }
}

/* --- Extract / Draw --- */

/* Test a world-space AABB against the camera frustum and the shadow
 * frustum. Returns true if the AABB is inside either. */
static bool flecsEngine_isVisibleAABB(
    const FlecsEngineImpl *engine,
    const float wmin[3],
    const float wmax[3])
{
    if (flecsEngine_testAABBFrustum(engine->frustum_planes, wmin, wmax)) {
        return true;
    }

    if (engine->shadow_frustum_valid) {
        return flecsEngine_testAABBFrustum(
            engine->shadow_frustum_planes, wmin, wmax);
    }

    return false;
}

/* Write a visible instance's transform into each cascade's shadow buffer
 * that the instance's AABB intersects. */
static void flecsEngine_batch_shadowCascadeWrite(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    flecsEngine_batch_t *ctx,
    const FlecsInstanceTransform *transform,
    const float wmin[3],
    const float wmax[3],
    bool has_aabb)
{
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        int32_t sdst = ctx->shadow_offset[c] + ctx->shadow_count[c];
        if (sdst >= buf->shadow_capacity) {
            ctx->shadow_count[c]++;
            continue;
        }
        if (has_aabb) {
            if (!flecsEngine_testAABBFrustum(
                    engine->cascade_frustum_planes[c], wmin, wmax))
            {
                continue;
            }
        }
        buf->cpu_shadow_transforms[c][sdst] = *transform;
        ctx->shadow_count[c]++;
    }
}

void flecsEngine_batch_extractInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractInstances");
    flecsEngine_batch_buffers_t *buf = ctx->buffers;
    ecs_assert(buf != NULL, ECS_INTERNAL_ERROR, NULL);

    int32_t base = ctx->offset;
    ctx->count = 0;

    bool do_shadow_cull = engine->cascade_frustum_valid;
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        ctx->shadow_count[c] = 0;
    }

    /* Frustum culling state */
    bool do_cull = engine->frustum_valid &&
        (ctx->mesh.aabb_min[0] <= ctx->mesh.aabb_max[0]);
    const float *aabb_min = ctx->mesh.aabb_min;
    const float *aabb_max = ctx->mesh.aabb_max;

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    ecs_iter_set_group(&it, ctx->group_id);
    while (ecs_query_next(&it)) {
        int32_t dst = base + ctx->count;

        /* If not enough capacity, count conservatively for resize */
        if ((dst + it.count) > buf->capacity) {
            ctx->count += it.count;
            continue;
        }

        const FlecsWorldTransform3 *wt = ecs_field(
            &it, FlecsWorldTransform3, 1);

        /* Fetch material data pointers up front */
        const FlecsRgba *colors = NULL;
        const FlecsPbrMaterial *materials = NULL;
        const FlecsEmissive *emissives = NULL;
        const FlecsMaterialId *material_id = NULL;

        if (buf->owns_material_data) {
            colors = ecs_field(&it, FlecsRgba, 2);
            materials = ecs_field(&it, FlecsPbrMaterial, 3);
            emissives = ecs_field(&it, FlecsEmissive, 4);
        } else {
            material_id = ecs_field(&it, FlecsMaterialId, 2);
        }

        int32_t added = 0;

        if (ctx->scale_callback) {
            const void *scale_data = ecs_field_w_size(
                &it, ctx->component_size, 0);
            for (int32_t i = 0; i < it.count; i ++) {
                void *ptr = ECS_ELEM(
                    scale_data, ctx->component_size, i);
                vec3 scale;
                ctx->scale_callback(ptr, scale);

                float wmin[3], wmax[3];
                bool has_aabb = false;
                if (do_cull) {
                    flecsEngine_computeWorldAABB(&wt[i],
                        aabb_min, aabb_max,
                        scale[0], scale[1], scale[2], wmin, wmax);
                    has_aabb = true;
                    if (!flecsEngine_isVisibleAABB(engine, wmin, wmax)) {
                        /* Not in camera or shadow frustum - skip entirely,
                         * but still test against cascade frustums for
                         * shadow-only casters handled below. */
                        if (do_shadow_cull) {
                            FlecsInstanceTransform t;
                            flecsEngine_batch_transformInstance(
                                &t, &wt[i],
                                scale[0], scale[1], scale[2]);
                            flecsEngine_batch_shadowCascadeWrite(
                                engine, buf, ctx, &t, wmin, wmax, has_aabb);
                        }
                        continue;
                    }
                }

                int32_t out = dst + added;
                flecsEngine_batch_transformInstance(
                    &buf->cpu_transforms[out],
                    &wt[i], scale[0], scale[1], scale[2]);

                if (buf->owns_material_data) {
                    buf->cpu_colors[out] = colors
                        ? colors[i]
                        : flecsEngine_defaultAttrCache_getColor(engine, 1)[0];
                    buf->cpu_pbr_materials[out] = materials
                        ? materials[i]
                        : flecsEngine_defaultAttrCache_getMaterial(engine, 1)[0];
                    buf->cpu_emissives[out] = emissives
                        ? emissives[i]
                        : flecsEngine_defaultAttrCache_getEmissive(engine, 1)[0];
                } else {
                    buf->cpu_material_ids[out] = material_id[0];
                }

                if (do_shadow_cull) {
                    flecsEngine_batch_shadowCascadeWrite(
                        engine, buf, ctx, &buf->cpu_transforms[out],
                        wmin, wmax, has_aabb);
                }

                added ++;
            }
        } else {
            for (int32_t i = 0; i < it.count; i ++) {
                float wmin[3], wmax[3];
                bool has_aabb = false;
                if (do_cull) {
                    flecsEngine_computeWorldAABB(&wt[i],
                        aabb_min, aabb_max,
                        1.0f, 1.0f, 1.0f, wmin, wmax);
                    has_aabb = true;
                    if (!flecsEngine_isVisibleAABB(engine, wmin, wmax)) {
                        if (do_shadow_cull) {
                            FlecsInstanceTransform t;
                            flecsEngine_batch_transformInstance(
                                &t, &wt[i], 1.0f, 1.0f, 1.0f);
                            flecsEngine_batch_shadowCascadeWrite(
                                engine, buf, ctx, &t, wmin, wmax, has_aabb);
                        }
                        continue;
                    }
                }

                int32_t out = dst + added;
                flecsEngine_batch_transformInstance(
                    &buf->cpu_transforms[out],
                    &wt[i], 1.0f, 1.0f, 1.0f);

                if (buf->owns_material_data) {
                    buf->cpu_colors[out] = colors
                        ? colors[i]
                        : flecsEngine_defaultAttrCache_getColor(engine, 1)[0];
                    buf->cpu_pbr_materials[out] = materials
                        ? materials[i]
                        : flecsEngine_defaultAttrCache_getMaterial(engine, 1)[0];
                    buf->cpu_emissives[out] = emissives
                        ? emissives[i]
                        : flecsEngine_defaultAttrCache_getEmissive(engine, 1)[0];
                } else {
                    buf->cpu_material_ids[out] = material_id[0];
                }

                if (do_shadow_cull) {
                    flecsEngine_batch_shadowCascadeWrite(
                        engine, buf, ctx, &buf->cpu_transforms[out],
                        wmin, wmax, has_aabb);
                }

                added ++;
            }
        }

        ctx->count += added;
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_primitive_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_batch_buffers_t *buf = ctx->buffers;

redo:
    ctx->offset = 0;
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        ctx->shadow_offset[c] = 0;
    }
    flecsEngine_batch_extractInstances(world, engine, batch, ctx);

    /* Check if main buffers or shadow buffers need a resize */
    int32_t max_shadow = 0;
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (ctx->shadow_count[c] > max_shadow) {
            max_shadow = ctx->shadow_count[c];
        }
    }

    bool need_redo = false;
    if (ctx->count > buf->capacity) {
        flecsEngine_batch_buffers_ensureCapacity(engine, buf, ctx->count);
        need_redo = true;
    }
    if (max_shadow > buf->shadow_capacity) {
        flecsEngine_batch_buffers_ensureShadowCapacity(
            engine, buf, max_shadow);
        need_redo = true;
    }
    if (need_redo) {
        goto redo;
    }

    buf->count = ctx->count;
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        buf->shadow_count[c] = ctx->shadow_count[c];
    }
    flecsEngine_batch_buffers_upload(engine, buf);
    flecsEngine_batch_buffers_uploadShadow(engine, buf);
}

void flecsEngine_primitive_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)engine;

    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_batch_draw(pass, ctx);
}

void flecsEngine_batch_draw(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx)
{
    if (!ctx->count) {
        return;
    }

    const flecsEngine_batch_buffers_t *buf = ctx->buffers;
    if (!buf) {
        return;
    }

    WGPUBuffer vertex_buffer = ctx->vertex_buffer;
    if (!vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->offset * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)ctx->count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->instance_transform, transform_offset, transform_size);

    if (buf->owns_material_data) {
        uint64_t color_offset =
            (uint64_t)ctx->offset * sizeof(FlecsRgba);
        uint64_t color_size =
            (uint64_t)ctx->count * sizeof(FlecsRgba);
        uint64_t pbr_offset =
            (uint64_t)ctx->offset * sizeof(FlecsPbrMaterial);
        uint64_t pbr_size =
            (uint64_t)ctx->count * sizeof(FlecsPbrMaterial);
        uint64_t emissive_offset =
            (uint64_t)ctx->offset * sizeof(FlecsEmissive);
        uint64_t emissive_size =
            (uint64_t)ctx->count * sizeof(FlecsEmissive);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_color, color_offset, color_size);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 3, buf->instance_pbr, pbr_offset, pbr_size);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 4, buf->instance_emissive, emissive_offset, emissive_size);
    } else {
        uint64_t matid_offset =
            (uint64_t)ctx->offset * sizeof(FlecsMaterialId);
        uint64_t matid_size =
            (uint64_t)ctx->count * sizeof(FlecsMaterialId);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_material_id, matid_offset, matid_size);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batch_drawShadow(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx,
    int cascade)
{
    int32_t count = ctx->shadow_count[cascade];
    if (!count) {
        return;
    }

    const flecsEngine_batch_buffers_t *buf = ctx->buffers;
    if (!buf || !buf->shadow_transforms[cascade]) {
        return;
    }

    WGPUBuffer vertex_buffer = ctx->vertex_buffer;
    if (!vertex_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    uint64_t transform_offset =
        (uint64_t)ctx->shadow_offset[cascade] * sizeof(FlecsInstanceTransform);
    uint64_t transform_size =
        (uint64_t)count * sizeof(FlecsInstanceTransform);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, buf->shadow_transforms[cascade],
        transform_offset, transform_size);

    /* Bind remaining instance buffers (material data / material id) that the
     * shadow pipeline layout expects, even though the shadow shader does not
     * read from them. Use the main batch buffers for this. */
    if (buf->owns_material_data) {
        if (buf->instance_color) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 2, buf->instance_color, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_pbr) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 3, buf->instance_pbr, 0, WGPU_WHOLE_SIZE);
        }
        if (buf->instance_emissive) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 4, buf->instance_emissive, 0, WGPU_WHOLE_SIZE);
        }
    } else if (buf->instance_material_id) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_material_id, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, count, 0, 0, 0);
}

void flecsEngine_primitive_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_batch_drawShadow(pass, ctx, engine->shadow.current_cascade);
}

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

    flecsEngine_batch_buffers_upload(engine, buf);
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

void flecsEngine_box_scale(
    const void *ptr,
    float *scale)
{
    const FlecsBox *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

void flecsEngine_quad_scale(
    const void *ptr,
    float *scale)
{
    const FlecsQuad *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_right_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}
