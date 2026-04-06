#include "render_batch.h"
#include "frustum_cull.h"
#include "../../tracy_hooks.h"

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

    ecs_assert(engine->frustum_valid, ECS_INTERNAL_ERROR, NULL);

    bool do_screen_cull = engine->screen_cull_valid;
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

        const void *scale_data = ctx->scale_callback
            ? ecs_field_w_size(&it, ctx->component_size, 0) : NULL;

        for (int32_t i = 0; i < it.count; i ++) {
            float sx, sy, sz;
            if (scale_data) {
                vec3 scale;
                ctx->scale_callback(
                    ECS_ELEM(scale_data, ctx->component_size, i), scale);
                sx = scale[0]; sy = scale[1]; sz = scale[2];
            } else {
                sx = sy = sz = 1.0f;
            }

            float wmin[3], wmax[3];
            flecsEngine_computeWorldAABB(
                &wt[i], aabb_min, aabb_max, sx, sy, sz, wmin, wmax);

            if (!flecsEngine_testAABBFrustum(
                    engine->frustum_planes, wmin, wmax))
            {
                continue;
            }

            if (do_screen_cull && !flecsEngine_testScreenSize(
                    engine->camera_pos, wmin, wmax,
                    engine->screen_cull_factor,
                    engine->screen_cull_threshold))
            {
                continue;
            }

            int32_t out = dst + added;
            flecsEngine_batch_transformInstance(
                &buf->cpu_transforms[out], &wt[i], sx, sy, sz);

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

            added ++;
        }

        ctx->count += added;
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_extractShadowInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractShadowInstances");
    flecsEngine_batch_buffers_t *buf = ctx->buffers;
    ecs_assert(buf != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        ctx->shadow_count[c] = 0;
    }

    if (!engine->cascade_frustum_valid) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    const float *aabb_min = ctx->mesh.aabb_min;
    const float *aabb_max = ctx->mesh.aabb_max;

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    ecs_iter_set_group(&it, ctx->group_id);
    while (ecs_query_next(&it)) {
        const FlecsWorldTransform3 *wt = ecs_field(
            &it, FlecsWorldTransform3, 1);

        const void *scale_data = ctx->scale_callback
            ? ecs_field_w_size(&it, ctx->component_size, 0) : NULL;

        for (int32_t i = 0; i < it.count; i ++) {
            float sx, sy, sz;
            if (scale_data) {
                vec3 scale;
                ctx->scale_callback(
                    ECS_ELEM(scale_data, ctx->component_size, i), scale);
                sx = scale[0]; sy = scale[1]; sz = scale[2];
            } else {
                sx = sy = sz = 1.0f;
            }

            float wmin[3], wmax[3];
            flecsEngine_computeWorldAABB(
                &wt[i], aabb_min, aabb_max, sx, sy, sz, wmin, wmax);

            FlecsInstanceTransform t;
            flecsEngine_batch_transformInstance(&t, &wt[i], sx, sy, sz);

            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
                int32_t sdst = ctx->shadow_offset[c] + ctx->shadow_count[c];
                if (sdst >= buf->shadow_capacity) {
                    ctx->shadow_count[c]++;
                    continue;
                }
                if (!flecsEngine_testAABBFrustum(
                        engine->cascade_frustum_planes[c], wmin, wmax))
                {
                    continue;
                }
                buf->cpu_shadow_transforms[c][sdst] = t;
                ctx->shadow_count[c]++;
            }
        }
    }

    FLECS_TRACY_ZONE_END;
}
