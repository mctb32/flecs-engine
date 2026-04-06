#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "../../../tracy_hooks.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_buffers_t buffers;
} flecsEngine_mesh_ctx_t;

static uint64_t flecsEngine_mesh_groupByMesh(
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

static void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)ptr;
    return flecsEngine_batch_create(world, NULL, group_id, false, 0, NULL);
}

static void flecsEngine_mesh_onGroupDelete(
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

static void flecsEngine_mesh_extractShadowGroup(
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

    if (!ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            ctx->shadow_count[c] = 0;
        }
        return;
    }

    ctx->buffers = shared;
    flecsEngine_batch_extractShadowInstances(world, engine, batch, ctx);
}

static void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshExtract");

    flecsEngine_mesh_ctx_t *mctx = batch->ctx;
    flecsEngine_batch_buffers_t *shared = &mctx->buffers;

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

    flecsEngine_batch_buffers_upload(engine, shared);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_mesh_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("MeshExtractShadow");

    flecsEngine_mesh_ctx_t *mctx = batch->ctx;
    flecsEngine_batch_buffers_t *shared = &mctx->buffers;

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) {
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            shared->shadow_count[c] = 0;
        }
        return;
    }

redo_shadow: {
        int32_t shadow_totals[FLECS_ENGINE_SHADOW_CASCADE_COUNT] = {0};
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
                ctx->shadow_offset[c] = shadow_totals[c];
            }
            flecsEngine_mesh_extractShadowGroup(
                world, engine, batch, group_id, shared);
            for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
                shadow_totals[c] += ctx->shadow_count[c];
            }
        }

        int32_t max_shadow = 0;
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            if (shadow_totals[c] > max_shadow) {
                max_shadow = shadow_totals[c];
            }
        }
        if (max_shadow > shared->shadow_capacity) {
            flecsEngine_batch_buffers_ensureShadowCapacity(
                engine, shared, max_shadow);
            goto redo_shadow;
        }

        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            shared->shadow_count[c] = shadow_totals[c];
        }
    }

    flecsEngine_batch_buffers_uploadShadow(engine, shared);
    FLECS_TRACY_ZONE_END;
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

static void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
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

        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);
        flecsEngine_batch_drawShadow(engine, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
}

flecsEngine_mesh_ctx_t* flecsEngine_mesh_createCtx(
    bool owns_material_data)
{
    flecsEngine_mesh_ctx_t *ctx = ecs_os_calloc_t(flecsEngine_mesh_ctx_t);
    flecsEngine_batch_buffers_init(&ctx->buffers, owns_material_data);
    return ctx;
}

void flecsEngine_mesh_deleteCtx(void *ptr)
{
    flecsEngine_mesh_ctx_t *ctx = ptr;
    flecsEngine_batch_buffers_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

/* --- Non-textured mesh batch creation --- */

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(false),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_mesh_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba),
            ecs_id(FlecsPbrMaterial),
            ecs_id(FlecsEmissive)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(true),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Textured mesh batch --- */

static void flecsEngine_textured_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("TexturedMeshRender");

    bool use_array = engine->materials.texture_array_bind_group;

    /* When a texture array is available, bind it once for all groups */
    if (use_array) {
        wgpuRenderPassEncoderSetBindGroup(
            pass, 2,
            engine->materials.texture_array_bind_group, 0, NULL);
    }

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group_id = ecs_map_key(&git);
        if (!group_id) continue;

        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

        /* Fall back to per-group texture binding when no array */
        if (!use_array) {
            const FlecsPbrTexturesImpl *tex_impl = ecs_get(
                world, (ecs_entity_t)group_id, FlecsPbrTexturesImpl);
            if (!tex_impl || !tex_impl->bind_group) {
                continue;
            }
            wgpuRenderPassEncoderSetBindGroup(
                pass, 2, tex_impl->bind_group, 0, NULL);
        }

        ctx->vertex_buffer = ctx->mesh.vertex_uv_buffer;
        flecsEngine_batch_draw(engine, pass, ctx);
    }

    FLECS_TRACY_ZONE_END;
}

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTextured(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertexUv),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .callback = flecsEngine_textured_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(false),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Unified transparent mesh batch --- */

typedef struct {
    uint64_t group_id;
    int32_t instance_index;
    float distance_sq;
    bool is_textured;
} flecsEngine_sorted_instance_t;

static int flecsEngine_sortInstanceByDistance(
    const void *a,
    const void *b)
{
    const flecsEngine_sorted_instance_t *ia = a;
    const flecsEngine_sorted_instance_t *ib = b;
    /* Back-to-front: farthest first. Break ties by group to cluster
     * same-mesh instances together and reduce GPU state switches. */
    if (ia->distance_sq < ib->distance_sq) return 1;
    if (ia->distance_sq > ib->distance_sq) return -1;
    if (ia->group_id < ib->group_id) return -1;
    if (ia->group_id > ib->group_id) return 1;
    return 0;
}

typedef struct {
    flecsEngine_mesh_ctx_t base;
    ecs_entity_t self_entity;
    ecs_entity_t textured_helper;
} flecsEngine_transparent_mesh_ctx_t;

static flecsEngine_transparent_mesh_ctx_t* flecsEngine_transparent_mesh_createCtx(
    ecs_entity_t self_entity,
    ecs_entity_t textured_helper)
{
    flecsEngine_transparent_mesh_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_transparent_mesh_ctx_t);
    flecsEngine_batch_buffers_init(&ctx->base.buffers, false);
    ctx->self_entity = self_entity;
    ctx->textured_helper = textured_helper;
    return ctx;
}

static void flecsEngine_transparent_mesh_deleteCtx(void *ptr)
{
    flecsEngine_transparent_mesh_ctx_t *ctx = ptr;
    flecsEngine_batch_buffers_fini(&ctx->base.buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_transparent_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    if (engine->shadow.in_pass) {
        return;
    }

    FLECS_TRACY_ZONE_BEGIN("TransparentMeshRender");

    flecsEngine_transparent_mesh_ctx_t *tctx = batch->ctx;
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    /* Count total instances across all groups */
    int32_t total_instances = 0;
    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group_id = ecs_map_key(&git);
        if (!group_id) continue;
        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || !ctx->count) continue;
        total_instances += ctx->count;
    }

    if (!total_instances) {
        goto done;
    }

    /* Collect every instance with its distance and GPU state metadata */
    flecsEngine_sorted_instance_t *sorted =
        ecs_os_malloc_n(flecsEngine_sorted_instance_t, total_instances);

    float cam_x = engine->camera_pos[0];
    float cam_y = engine->camera_pos[1];
    float cam_z = engine->camera_pos[2];

    int32_t si = 0;
    git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group_id = ecs_map_key(&git);
        if (!group_id) continue;

        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || !ctx->count) continue;

        bool is_textured =
            ecs_has(world, (ecs_entity_t)group_id, FlecsPbrTextures);

        for (int32_t j = 0; j < ctx->count; j ++) {
            const FlecsInstanceTransform *t =
                &ctx->buffers->cpu_transforms[ctx->offset + j];
            float dx = t->c3.x - cam_x;
            float dy = t->c3.y - cam_y;
            float dz = t->c3.z - cam_z;

            sorted[si].group_id = group_id;
            sorted[si].instance_index = j;
            sorted[si].distance_sq = dx * dx + dy * dy + dz * dz;
            sorted[si].is_textured = is_textured;
            si ++;
        }
    }

    /* Sort all instances back-to-front */
    qsort(sorted, (size_t)total_instances,
        sizeof(flecsEngine_sorted_instance_t),
        flecsEngine_sortInstanceByDistance);

    /* Look up pipeline handles for both rendering modes */
    const FlecsRenderBatchImpl *self_impl =
        ecs_get(world, tctx->self_entity, FlecsRenderBatchImpl);
    const FlecsRenderBatchImpl *tex_impl =
        ecs_get(world, tctx->textured_helper, FlecsRenderBatchImpl);

    WGPURenderPipeline non_tex_pipeline = self_impl->pipeline_hdr;
    WGPURenderPipeline tex_pipeline =
        tex_impl ? tex_impl->pipeline_hdr : NULL;
    WGPURenderPipeline active_pipeline = non_tex_pipeline;
    uint64_t active_group = 0;

    /* Render each instance individually in sorted order */
    for (int32_t i = 0; i < total_instances; i ++) {
        uint64_t group_id = sorted[i].group_id;

        flecsEngine_batch_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

        const flecsEngine_batch_buffers_t *buf = ctx->buffers;
        if (!buf) {
            continue;
        }

        /* When the group changes, rebind mesh vertex/index buffers and
         * update per-group GPU state (pipeline, textures). */
        if (group_id != active_group) {
            active_group = group_id;

            if (sorted[i].is_textured && tex_pipeline) {
                if (active_pipeline != tex_pipeline) {
                    wgpuRenderPassEncoderSetPipeline(pass, tex_pipeline);
                    active_pipeline = tex_pipeline;
                }

                const FlecsPbrTexturesImpl *tex_impl = ecs_get(
                    world, (ecs_entity_t)group_id, FlecsPbrTexturesImpl);
                if (!tex_impl || !tex_impl->bind_group) {
                    continue;
                }
                wgpuRenderPassEncoderSetBindGroup(
                    pass, 2, tex_impl->bind_group,
                    0, NULL);

                if (!ctx->mesh.vertex_uv_buffer) {
                    continue;
                }
                wgpuRenderPassEncoderSetVertexBuffer(
                    pass, 0, ctx->mesh.vertex_uv_buffer,
                    0, WGPU_WHOLE_SIZE);
            } else {
                if (active_pipeline != non_tex_pipeline) {
                    wgpuRenderPassEncoderSetPipeline(pass, non_tex_pipeline);
                    active_pipeline = non_tex_pipeline;
                }

                if (!ctx->mesh.vertex_buffer) {
                    continue;
                }
                wgpuRenderPassEncoderSetVertexBuffer(
                    pass, 0, ctx->mesh.vertex_buffer,
                    0, WGPU_WHOLE_SIZE);
            }

            if (!ctx->mesh.index_buffer || !ctx->mesh.index_count) {
                continue;
            }
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32,
                0, WGPU_WHOLE_SIZE);
        }

        /* Bind this instance's transform and material id */
        int32_t global_index = ctx->offset + sorted[i].instance_index;
        uint64_t transform_offset =
            (uint64_t)global_index * sizeof(FlecsInstanceTransform);
        uint64_t matid_offset =
            (uint64_t)global_index * sizeof(FlecsMaterialId);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 1, buf->instance_transform,
            transform_offset, sizeof(FlecsInstanceTransform));
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->instance_material_id,
            matid_offset, sizeof(FlecsMaterialId));

        wgpuRenderPassEncoderDrawIndexed(
            pass, ctx->mesh.index_count, 1, 0, 0, 0);
    }

    /* Restore original pipeline so engine->last_pipeline stays consistent */
    if (active_pipeline != non_tex_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, non_tex_pipeline);
    }

    ecs_os_free(sorted);

done:
    FLECS_TRACY_ZONE_END;
}

ecs_entity_t flecsEngine_createBatch_mesh_transparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, {
        .parent = parent, .name = name });

    /* Create helper entity for textured transparent pipeline. This entity
     * is not added to any batch set; it exists solely so that the normal
     * FlecsRenderBatch on_set hook creates a pipeline we can reference. */
    ecs_entity_t textured_helper = ecs_entity(world, {
        .parent = batch });
    ecs_set(world, textured_helper, FlecsRenderBatch, {
        .shader = flecsEngine_shader_pbrTextured(world),
        .vertex_type = ecs_id(FlecsLitVertexUv),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .depth_test = WGPUCompareFunction_Less,
        .cull_mode = WGPUCullMode_None,
        .blend = {
            .color = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            }
        },
        .depth_write = false
    });

    /* Unified query matches all transparent meshes regardless of textures */
    ecs_entity_t shader = flecsEngine_shader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .depth_test = WGPUCompareFunction_Less,
        .cull_mode = WGPUCullMode_None,
        .blend = {
            .color = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            }
        },
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .callback = flecsEngine_transparent_mesh_render,
        .ctx = flecsEngine_transparent_mesh_createCtx(batch, textured_helper),
        .free_ctx = flecsEngine_transparent_mesh_deleteCtx
    });

    return batch;
}
