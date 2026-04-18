#include "common.h"

void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)world;
    (void)ptr;
    return flecsEngine_batch_group_create(NULL, group_id);
}

void flecsEngine_mesh_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    (void)world;
    (void)group_id;
    (void)ptr;
    flecsEngine_batch_group_delete(group_ptr);
}

uint64_t flecsEngine_mesh_groupByMesh(
    ecs_world_t *world,
    ecs_table_t *table,
    ecs_id_t id,
    void *ctx)
{
    (void)id;
    (void)ctx;

    ecs_entity_t tgt = 0;
    if (ecs_search_relation(
        world,
        table,
        0,
        ecs_id(FlecsMesh3Impl),
        EcsIsA,
        EcsSelf | EcsUp,
        &tgt,
        NULL,
        NULL) == -1)
    {
        return 0;
    }

    return tgt;
}

flecsEngine_batch_t* flecsEngine_mesh_createCtx(
    flecsEngine_batch_flags_t flags)
{
    flecsEngine_batch_t *buffers =
        ecs_os_calloc_t(flecsEngine_batch_t);
    flecsEngine_batch_init(buffers, flags);
    return buffers;
}

void flecsEngine_mesh_deleteCtx(void *ptr)
{
    flecsEngine_batch_t *buffers = ptr;
    flecsEngine_batch_fini(buffers);
    ecs_os_free(buffers);
}

static void flecsEngine_mesh_extractGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
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

    const FlecsMesh3Impl *mesh = ecs_get(
        world, (ecs_entity_t)group_id, FlecsMesh3Impl);
    if (!mesh || !mesh->index_buffer || !mesh->index_count) {
        ctx->view.count = 0;
        ecs_os_zeromem(&ctx->mesh);
        return;
    }

    ctx->mesh = *mesh;
    ctx->batch = shared;
    flecsEngine_batch_group_extract(world, engine, batch, ctx, NULL, NULL, 0);
}

void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshExtract");
    (void)view_impl;

    flecsEngine_batch_t *shared = batch->ctx;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) {
        shared->buffers.count = 0;
        FLECS_TRACY_ZONE_END;
        return;
    }

redo: {
        int32_t total = 0;
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            ctx->view.offset = total;
            flecsEngine_mesh_extractGroup(
                world, engine, batch, group_id, shared);
            total += ctx->view.count;
        }

        if (total > shared->buffers.capacity) {
            flecsEngine_batch_ensureCapacity(engine, shared, total);
            goto redo;
        }

        shared->buffers.count = total;
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_cull(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshCull");
    (void)world;

    flecsEngine_batch_t *shared = batch->ctx;
    if (!shared->buffers.count) {
        shared->buffers.visible_count = 0;
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_ensureVisibleCapacity(
        engine, shared, shared->buffers.count);

    int32_t total = 0;
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (groups) {
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx || !ctx->view.count) {
                if (ctx) {
                    ctx->view.visible_offset = total;
                    ctx->view.visible_count = 0;
                }
                continue;
            }

            ctx->view.visible_offset = total;
            flecsEngine_batch_group_cull(view_impl, ctx);
            total += ctx->view.visible_count;
        }
    }

    shared->buffers.visible_count = total;
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_cullShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshCullShadow");
    (void)world;

    flecsEngine_batch_t *shared = batch->ctx;
    if (!shared->buffers.count || !view_impl->cascade_frustum_valid) {
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
            shared->buffers.shadow_visible_count[c] = 0;
        }
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_ensureVisibleCapacity(
        engine, shared, shared->buffers.count);

    int32_t totals[FLECS_ENGINE_SHADOW_CASCADE_COUNT] = {0};
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (groups) {
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
                ctx->view.shadow_visible_offset[c] = totals[c];
            }

            if (!ctx->view.count) {
                for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
                    ctx->view.shadow_visible_count[c] = 0;
                }
                continue;
            }

            flecsEngine_batch_group_cullShadow(view_impl, ctx);

            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
                totals[c] += ctx->view.shadow_visible_count[c];
            }
        }
    }

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        shared->buffers.shadow_visible_count[c] = totals[c];
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;
    flecsEngine_batch_upload(engine, batch->ctx);
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

void flecsEngine_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshRender");

    (void)world;
    (void)view_impl;

    flecsEngine_batch_t *buf = batch->ctx;
    flecsEngine_batch_bindMaterialGroup((FlecsEngineImpl*)engine, pass, buf);
    flecsEngine_batch_bindInstanceGroup((FlecsEngineImpl*)engine, pass, buf);

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

        flecsEngine_batch_group_draw(engine, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_renderDepthPrepass(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshRenderDepthPrepass");

    (void)world;
    (void)view_impl;

    flecsEngine_batch_t *buf = batch->ctx;
    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, buf);

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_group_drawDepthPrepass(engine, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
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

    flecsEngine_batch_t *buf = batch->ctx;
    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, buf);

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
