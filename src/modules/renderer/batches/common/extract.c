#include "common.h"

/* Compute world-space AABBs from local AABBs + world transforms, writing into
 * the padded GPU-ready layout. Uses Arvo's method (18 multiplies per instance).
 */
static void flecsEngine_extract_worldAabb(
    const FlecsWorldTransform3 *wt,
    const FlecsAABB local_aabb,
    flecsEngine_gpuAabb_t *out)
{
    float smin[3] = { local_aabb.min[0], local_aabb.min[1], local_aabb.min[2] };
    float smax[3] = { local_aabb.max[0], local_aabb.max[1], local_aabb.max[2] };

    float mn[3];
    float mx[3];

    for (int i = 0; i < 3; i ++) {
        mn[i] = mx[i] = wt->m[3][i];
        for (int j = 0; j < 3; j ++) {
            float e = wt->m[j][i] * smin[j];
            float f = wt->m[j][i] * smax[j];
            if (e < f) {
                mn[i] += e;
                mx[i] += f;
            } else {
                mn[i] += f;
                mx[i] += e;
            }
        }
    }

    out->min[0] = mn[0]; out->min[1] = mn[1]; out->min[2] = mn[2];
    out->max[0] = mx[0]; out->max[1] = mx[1]; out->max[2] = mx[2];
    out->_pad0 = 0; out->_pad1 = 0;
}

static void flecsEngine_extract_applyScaleAabb(
    FlecsAABB *aabb,
    const float *scale_xyz)
{
    aabb->min[0] *= scale_xyz[0];
    aabb->min[1] *= scale_xyz[1];
    aabb->min[2] *= scale_xyz[2];
    aabb->max[0] *= scale_xyz[0];
    aabb->max[1] *= scale_xyz[1];
    aabb->max[2] *= scale_xyz[2];
}

static void flecsEngine_batch_group_extractTable(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size,
    ecs_iter_t *it)
{
    flecsEngine_batch_t *buf = ctx->batch;
    int32_t base = ctx->view.offset + ctx->view.count;
    int32_t dst = base;

    FlecsAABB mesh_aabb = ctx->mesh.aabb;

    /* If not enough capacity, count for resize */
    if ((dst + it->count) > buf->buffers.capacity) {
        ctx->view.count += it->count;
        return;
    }

    const void *scale_data = scale ? ecs_field_w_size(it, scale_size, 0) : NULL;
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 1);

    /* Build world AABBs into the padded GPU buffer. Apply optional primitive
     * scale transformation to a copy of the mesh aabb per entity. */
    FlecsAABB batch_aabb_tmp[256];
    int32_t remaining = it->count;
    int32_t src = 0;
    while (remaining > 0) {
        int32_t n = remaining > 256 ? 256 : remaining;
        for (int32_t i = 0; i < n; i ++) {
            batch_aabb_tmp[i] = mesh_aabb;
        }
        if (scale_aabb) {
            scale_aabb(batch_aabb_tmp,
                scale_data ? ECS_ELEM(scale_data, scale_size, src) : NULL, n);
        }
        for (int32_t i = 0; i < n; i ++) {
            flecsEngine_extract_worldAabb(&wt[src + i], batch_aabb_tmp[i],
                &buf->buffers.cpu_aabb[dst + i]);
        }
        src += n;
        dst += n;
        remaining -= n;
    }

    /* Fill slot_to_group */
    uint32_t g = (uint32_t)ctx->view.group_idx;
    for (int32_t i = 0; i < it->count; i ++) {
        buf->buffers.cpu_slot_to_group[base + i] = g;
    }

    dst = base;
    if (!(buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        const FlecsMaterialId *material_id = ecs_field(it, FlecsMaterialId, 3);

        for (int32_t i = 0; i < it->count; i ++) {
            vec3 s = {1, 1, 1};
            if (scale_data) {
                scale(ECS_ELEM(scale_data, scale_size, i), s);
            }

            flecsEngine_batch_transformInstance(
                &buf->buffers.cpu_transforms[dst], &wt[i], s[0], s[1], s[2]);

            buf->buffers.cpu_material_ids[dst] = material_id[0];

            dst ++;
        }
    } else {
        const FlecsRgba *colors = ecs_field(it, FlecsRgba, 3);
        const FlecsPbrMaterial *materials = ecs_field(it, FlecsPbrMaterial, 4);
        const FlecsEmissive *emissives = ecs_field(it, FlecsEmissive, 5);
        const FlecsTransmission *transmissions = NULL;
        if ((buf->flags & FLECS_BATCH_OWNS_TRANSMISSION)) {
            transmissions = ecs_field(it, FlecsTransmission, 6);
        }

        for (int32_t i = 0; i < it->count; i ++) {
            vec3 s = {1, 1, 1};
            if (scale_data) {
                scale(ECS_ELEM(scale_data, scale_size, i), s);
            }

            flecsEngine_batch_transformInstance(
                &buf->buffers.cpu_transforms[dst], &wt[i], s[0], s[1], s[2]);

            buf->buffers.cpu_materials[dst] = flecsEngine_material_pack(
                engine,
                colors ? &colors[i] : NULL,
                materials ? &materials[i] : NULL,
                emissives ? &emissives[i] : NULL,
                transmissions ? &transmissions[i] : NULL,
                NULL);

            dst ++;
        }
    }

    ctx->view.count += (dst - base);
    (void)scale_aabb;
}

void flecsEngine_batch_group_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractGroup");

    ctx->view.count = 0;

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    ecs_iter_set_group(&it, ctx->group_id);
    while (ecs_query_next(&it)) {
        flecsEngine_batch_group_extractTable(
            engine, ctx, scale, scale_aabb, scale_size, &it);
    }

    FLECS_TRACY_ZONE_END;
}
