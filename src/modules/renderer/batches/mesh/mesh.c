#include "mesh.h"

void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)ptr;
    return flecsEngine_batch_create(world, NULL, group_id, false, 0, NULL);
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
    flecsEngine_batch_delete(group_ptr);
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

flecsEngine_batch_buffers_t* flecsEngine_mesh_createCtx(
    bool owns_material_data)
{
    flecsEngine_batch_buffers_t *buffers =
        ecs_os_calloc_t(flecsEngine_batch_buffers_t);
    flecsEngine_batch_buffers_init(buffers, owns_material_data, false);
    return buffers;
}

flecsEngine_batch_buffers_t* flecsEngine_mesh_createTransmissionDataCtx(void)
{
    flecsEngine_batch_buffers_t *buffers =
        ecs_os_calloc_t(flecsEngine_batch_buffers_t);
    flecsEngine_batch_buffers_init(buffers, true, true);
    return buffers;
}

void flecsEngine_mesh_deleteCtx(void *ptr)
{
    flecsEngine_batch_buffers_t *buffers = ptr;
    flecsEngine_batch_buffers_fini(buffers);
    ecs_os_free(buffers);
}

static void flecsEngine_mesh_extractGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    uint64_t group_id,
    flecsEngine_batch_buffers_t *shared)
{
    if (!group_id) {
        return;
    }

    flecsEngine_batch_t *ctx =
        ecs_query_get_group_ctx(batch->query, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    const FlecsMesh3Impl *mesh = ecs_get(
        world, (ecs_entity_t)group_id, FlecsMesh3Impl);
    if (!mesh || !mesh->index_buffer || !mesh->index_count) {
        ctx->count = 0;
        ecs_os_zeromem(&ctx->mesh);
        return;
    }

    ctx->mesh = *mesh;
    ctx->vertex_buffer = mesh->vertex_buffer;
    ctx->buffers = shared;
    flecsEngine_batch_extractInstances(world, engine, batch, ctx);
}

void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshExtract");

    flecsEngine_batch_buffers_t *shared = batch->ctx;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) {
        shared->count = 0;
        return;
    }

redo: {
        int32_t total = 0;
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            ctx->offset = total;
            flecsEngine_mesh_extractGroup(
                world, engine, batch, group_id, shared);
            total += ctx->count;
        }

        if (total > shared->capacity) {
            flecsEngine_batch_buffers_ensureCapacity(engine, shared, total);
            goto redo;
        }

        shared->count = total;
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    (void)world;
    flecsEngine_batch_buffers_upload(engine, batch->ctx);
}

void flecsEngine_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshRender");

    (void)world;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        if (!group) continue;

        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_draw(engine, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
}
