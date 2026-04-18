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

void* flecsEngine_mesh_getCullBuf(
    const FlecsRenderBatch *batch)
{
    return batch->ctx;
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
        ctx->static_view.count = 0;
        ctx->static_view.group_idx = -1;
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
        shared->buffers.group_count = 0;
        shared->static_buffers.group_count = 0;
        FLECS_TRACY_ZONE_END;
        return;
    }

redo: {
        int32_t total = 0;
        int32_t group_idx = 0;
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            ctx->view.offset = total;
            ctx->view.group_idx = group_idx;
            flecsEngine_mesh_extractGroup(
                world, engine, batch, group_id, shared);
            total += ctx->view.count;
            group_idx ++;
        }

        if (total > shared->buffers.capacity) {
            flecsEngine_batch_ensureCapacity(engine, shared, total);
            goto redo;
        }

        shared->buffers.count = total;
        shared->buffers.group_count = group_idx;
        flecsEngine_batch_ensureGroupCapacity(shared, group_idx);

        /* Fill group_info + zero indirect args on CPU. */
        ecs_map_iter_t it2 = ecs_map_iter(groups);
        while (ecs_map_next(&it2)) {
            uint64_t group_id = ecs_map_key(&it2);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx || !ctx->view.count) continue;

            flecsEngine_batch_group_prepareArgs(ctx);
        }

        int32_t static_group_idx = 0;
        int32_t static_offset = 0;
        ecs_map_iter_t sit = ecs_map_iter(groups);
        while (ecs_map_next(&sit)) {
            uint64_t group_id = ecs_map_key(&sit);
            if (!group_id) continue;
            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            int32_t sc = ecs_vec_count(&ctx->slots);
            if (!sc || !ctx->mesh.index_buffer || !ctx->mesh.index_count) {
                ctx->static_view.count = 0;
                ctx->static_view.offset = 0;
                ctx->static_view.group_idx = -1;
                continue;
            }

            ctx->static_view.count = sc;
            ctx->static_view.offset = static_offset;
            ctx->static_view.group_idx = static_group_idx;
            static_offset += sc;
            static_group_idx ++;
        }

        shared->static_buffers.group_count = static_group_idx;
        if (static_group_idx > 0) {
            flecsEngine_batch_ensureStaticGroupCapacity(shared, static_group_idx);

            ecs_map_iter_t ait = ecs_map_iter(groups);
            while (ecs_map_next(&ait)) {
                uint64_t group_id = ecs_map_key(&ait);
                if (!group_id) continue;
                flecsEngine_batch_group_t *ctx =
                    ecs_query_get_group_ctx(batch->query, group_id);
                if (!ctx || ctx->static_view.group_idx < 0) continue;
                flecsEngine_batch_group_applyChanges(world, ctx);
                flecsEngine_batch_group_prepareStaticArgs(ctx);
            }
        }
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)view_impl;
    flecsEngine_batch_t *buf = batch->ctx;
    flecsEngine_batch_upload(engine, buf);

    if (!(buf->flags & FLECS_BATCH_TRACK_STATIC)) {
        return;
    }
    if (!buf->static_buffers.group_count) {
        return;
    }

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) return;

    int32_t cap = buf->static_buffers.group_count;
    flecsEngine_batch_group_t **list =
        ecs_os_malloc_n(flecsEngine_batch_group_t*, cap);
    int32_t n = 0;
    ecs_map_iter_t it = ecs_map_iter(groups);
    while (ecs_map_next(&it)) {
        uint64_t group_id = ecs_map_key(&it);
        if (!group_id) continue;
        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || ctx->static_view.group_idx < 0) continue;
        list[n ++] = ctx;
    }

    flecsEngine_batch_uploadStatic(engine, buf, list, n);
    ecs_os_free(list);
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

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_batch_bindMaterialGroup((FlecsEngineImpl*)engine, pass, buf);
    flecsEngine_batch_bindInstanceGroup((FlecsEngineImpl*)engine, pass, buf);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

        flecsEngine_batch_group_draw(engine, pass, ctx);
    }

    if (buf->static_buffers.group_count > 0) {
        flecsEngine_batch_bindMaterialGroupStatic(
            (FlecsEngineImpl*)engine, pass, buf);
        flecsEngine_batch_bindInstanceGroupStatic(
            (FlecsEngineImpl*)engine, pass, buf);

        ecs_map_iter_t sit = ecs_map_iter(groups);
        while (ecs_map_next(&sit)) {
            uint64_t group = ecs_map_key(&sit);
            if (!group) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group);
            if (!ctx || ctx->static_view.group_idx < 0) continue;

            flecsEngine_batch_group_drawStatic(engine, pass, ctx);
        }
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

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, buf);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_group_drawDepthPrepass(engine, pass, ctx);
    }

    if (buf->static_buffers.group_count > 0) {
        flecsEngine_batch_bindInstanceGroupShadowStatic(
            (FlecsEngineImpl*)engine, pass, buf);
        ecs_map_iter_t sit = ecs_map_iter(groups);
        while (ecs_map_next(&sit)) {
            uint64_t group = ecs_map_key(&sit);
            if (!group) continue;
            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group);
            if (!ctx || ctx->static_view.group_idx < 0) continue;
            flecsEngine_batch_group_drawDepthPrepassStatic(engine, pass, ctx);
        }
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

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, buf);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_group_drawShadow(engine, view_impl, pass, ctx);
    }

    if (buf->static_buffers.group_count > 0) {
        flecsEngine_batch_bindInstanceGroupShadowStatic(
            (FlecsEngineImpl*)engine, pass, buf);
        ecs_map_iter_t sit = ecs_map_iter(groups);
        while (ecs_map_next(&sit)) {
            uint64_t group = ecs_map_key(&sit);
            if (!group) continue;
            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group);
            if (!ctx || ctx->static_view.group_idx < 0) continue;
            flecsEngine_batch_group_drawShadowStatic(engine, view_impl, pass, ctx);
        }
    }

    FLECS_TRACY_ZONE_END;
}
