#include "common.h"

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
    FlecsAABB *batch_aabb = &buf->buffers.cpu_aabb[dst];

    for (int32_t i = 0; i < it->count; i ++) {
        batch_aabb[i] = mesh_aabb;
    }

    if (scale_aabb) {
        scale_aabb(batch_aabb, scale_data, it->count);
    }

    flecsEngine_computeWorldAABB(wt, batch_aabb, it->count);

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
