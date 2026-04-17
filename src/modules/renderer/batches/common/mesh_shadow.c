#include "common.h"

static void flecsEngine_mesh_extractShadowGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    uint64_t group_id,
    flecsEngine_batch_t *shared)
{
    if (!group_id) {
        return;
    }

    flecsEngine_batch_group_t *ctx =
        ecs_query_get_group_ctx(batch->query, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    if (!ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            ctx->view.shadow_count[c] = 0;
        }
        return;
    }

    ctx->batch = shared;
    flecsEngine_batch_group_extractShadow(world, engine, view_impl, batch, ctx, NULL, 0);
}

void flecsEngine_mesh_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshExtractShadow");

    flecsEngine_batch_t *shared = batch->ctx;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) {
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            shared->buffers.shadow_count[c] = 0;
        }
        return;
    }

redo_shadow: {
        int32_t shadow_totals[FLECS_ENGINE_SHADOW_CASCADE_COUNT] = {0};
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
                ctx->view.shadow_offset[c] = shadow_totals[c];
            }
            flecsEngine_mesh_extractShadowGroup(
                world, engine, view_impl, batch, group_id, shared);
            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
                shadow_totals[c] += ctx->view.shadow_count[c];
            }
        }

        int32_t max_shadow = 0;
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            if (shadow_totals[c] > max_shadow) {
                max_shadow = shadow_totals[c];
            }
        }
        if (max_shadow > shared->buffers.shadow_capacity) {
            flecsEngine_batch_ensureShadowCapacity(
                engine, shared, max_shadow);
            goto redo_shadow;
        }

        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            shared->buffers.shadow_count[c] = shadow_totals[c];
        }
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_uploadShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;
    flecsEngine_batch_uploadShadow(engine, batch->ctx);
}

void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshRenderShadow");

    (void)world;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_group_drawShadow(engine, view_impl, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
}
