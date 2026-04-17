#include "common.h"

void flecsEngine_batch_group_extractTable(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size,
    ecs_iter_t *it)
{
    flecsEngine_batch_t *buf = ctx->batch;
    int32_t base = ctx->view.offset + ctx->view.count;
    int32_t dst = base;

    bool do_screen_cull = view_impl->screen_cull_valid;
    FlecsAABB mesh_aabb = ctx->mesh.aabb;

    /* If not enough capacity, count for resize */
    if ((dst + it->count) > buf->buffers.capacity) {
        ctx->view.count += it->count;
        return;
    }

    const void *scale_data = scale ? ecs_field_w_size(it, scale_size, 0) : NULL;
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 1);
    FlecsAABB *aabb = ecs_field(it, FlecsAABB, 2);

    /* Compute per-instance AABB */
    for (int32_t i = 0; i < it->count; i ++) {
        aabb[i] = mesh_aabb;
    }

    if (scale_aabb) {
        scale_aabb(aabb, scale_data, it->count);
    }

    flecsEngine_computeWorldAABB(wt, aabb, it->count);

    /* Extract ECS data */
    if (!(buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        const FlecsMaterialId *material_id = ecs_field(it, FlecsMaterialId, 3);

        for (int32_t i = 0; i < it->count; i ++) {
            if (!flecsEngine_testAABBFrustum(
                view_impl->frustum_planes, aabb[i].min, aabb[i].max) ||
                (do_screen_cull && !flecsEngine_testScreenSize(
                    view_impl->camera_pos,
                    aabb[i].min, aabb[i].max,
                    view_impl->screen_cull_factor,
                    view_impl->screen_cull_threshold)))
            {
                continue;
            }

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
        const FlecsRgba *colors = ecs_field(it, FlecsRgba, 3);;
        const FlecsPbrMaterial *materials = ecs_field(it, FlecsPbrMaterial, 4);
        const FlecsEmissive *emissives = ecs_field(it, FlecsEmissive, 5);
        const FlecsTransmission *transmissions = NULL;
        if ((buf->flags & FLECS_BATCH_OWNS_TRANSMISSION)) {
            transmissions = ecs_field(it, FlecsTransmission, 6);
        }

        for (int32_t i = 0; i < it->count; i ++) {
            if (!flecsEngine_testAABBFrustum(
                view_impl->frustum_planes, aabb[i].min, aabb[i].max))
            {
                continue;
            }

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
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractGroup");

    int32_t base = ctx->view.offset;
    ctx->view.count = 0;

    ecs_assert(view_impl->frustum_valid, ECS_INTERNAL_ERROR, NULL);

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    ecs_iter_set_group(&it, ctx->group_id);
    while (ecs_query_next(&it)) {
        flecsEngine_batch_group_extractTable(
            world, engine, view_impl, batch, ctx, scale, scale_aabb, scale_size, &it);
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_group_extractShadowTable(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    ecs_size_t scale_size,
    ecs_iter_t *it)
{
    (void)engine;
    flecsEngine_batch_t *buf = ctx->batch;

    const void *scale_data = scale
        ? ecs_field_w_size(it, scale_size, 0) : NULL;
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 1);
    const FlecsAABB *aabb = ecs_field(it, FlecsAABB, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsInstanceTransform t;
        bool transformed = false;

        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
            int32_t sdst = ctx->view.shadow_offset[c] + ctx->view.shadow_count[c];
            if (sdst >= buf->buffers.shadow_capacity) {
                ctx->view.shadow_count[c]++;
                continue;
            }

            if (!flecsEngine_testAABBFrustum(
                    view_impl->cascade_frustum_planes[c],
                    aabb[i].min, aabb[i].max))
            {
                continue;
            }

            if (!transformed) {
                vec3 s = {1, 1, 1};
                if (scale_data) {
                    scale(ECS_ELEM(scale_data, scale_size, i), s);
                }
                flecsEngine_batch_transformInstance(&t, &wt[i], s[0], s[1], s[2]);
                transformed = true;
            }

            buf->buffers.cpu_shadow_transforms[c][sdst] = t;
            ctx->view.shadow_count[c]++;
        }
    }
}

void flecsEngine_batch_group_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    ecs_size_t scale_size)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractGroupShadow");
    flecsEngine_batch_t *buf = ctx->batch;
    ecs_assert(buf != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        ctx->view.shadow_count[c] = 0;
    }

    if (!view_impl->cascade_frustum_valid) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    if (ctx->group_id) {
        ecs_iter_set_group(&it, ctx->group_id);
    }
    while (ecs_query_next(&it)) {
        flecsEngine_batch_group_extractShadowTable(
            engine, view_impl, ctx, scale, scale_size, &it);
    }

    FLECS_TRACY_ZONE_END;
}
