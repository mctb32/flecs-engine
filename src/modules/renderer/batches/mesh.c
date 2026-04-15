#include "common/common.h"

/* --- Non-textured mesh batch creation --- */

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
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
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .upload_callback = flecsEngine_mesh_upload,
        .shadow_upload_callback = flecsEngine_mesh_uploadShadow,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_DEFAULT),
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
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf, .oper = EcsNot },
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
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .upload_callback = flecsEngine_mesh_upload,
        .shadow_upload_callback = flecsEngine_mesh_uploadShadow,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_OWNS_MATERIAL),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Textured mesh batch --- */

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
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
        .extract_callback = flecsEngine_mesh_extract,
        .shadow_extract_callback = flecsEngine_mesh_extractShadow,
        .upload_callback = flecsEngine_mesh_upload,
        .shadow_upload_callback = flecsEngine_mesh_uploadShadow,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_DEFAULT),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Unified transparent mesh batch --- */

typedef struct {
    uint64_t group_id;
    int32_t instance_index;
    float distance_sq;
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
    flecsEngine_batch_t buffers;
    ecs_entity_t self_entity;
} flecsEngine_transparent_mesh_ctx_t;

static flecsEngine_transparent_mesh_ctx_t* flecsEngine_transparent_mesh_createCtx(
    ecs_entity_t self_entity)
{
    flecsEngine_transparent_mesh_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_transparent_mesh_ctx_t);
    flecsEngine_batch_init(&ctx->buffers,
        FLECS_BATCH_DEFAULT);
    ctx->self_entity = self_entity;
    return ctx;
}

static void flecsEngine_transparent_mesh_deleteCtx(void *ptr)
{
    flecsEngine_transparent_mesh_ctx_t *ctx = ptr;
    flecsEngine_batch_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_transparent_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("TransparentMeshRender");

    flecsEngine_transparent_mesh_ctx_t *tctx = batch->ctx;
    flecsEngine_batch_bindMaterialGroup(
        (FlecsEngineImpl*)engine, pass, &tctx->buffers);

    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    /* Count total instances across all groups */
    int32_t total_instances = 0;
    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group_id = ecs_map_key(&git);
        if (!group_id) continue;
        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || !ctx->view.count) continue;
        total_instances += ctx->view.count;
    }

    if (!total_instances) {
        goto done;
    }

    /* Collect every instance with its camera distance */
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

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || !ctx->view.count) continue;

        for (int32_t j = 0; j < ctx->view.count; j ++) {
            const FlecsInstanceTransform *t =
                &ctx->batch->buffers.cpu_transforms[ctx->view.offset + j];
            float dx = t->c3.x - cam_x;
            float dy = t->c3.y - cam_y;
            float dz = t->c3.z - cam_z;

            sorted[si].group_id = group_id;
            sorted[si].instance_index = j;
            sorted[si].distance_sq = dx * dx + dy * dy + dz * dz;
            si ++;
        }
    }

    /* Sort all instances back-to-front */
    qsort(sorted, (size_t)total_instances,
        sizeof(flecsEngine_sorted_instance_t),
        flecsEngine_sortInstanceByDistance);

    /* Single pipeline + texture array bind — the pbr shader handles both
     * textured and non-textured meshes via layer_* != 0 gating. Every mesh
     * has vertex_uv_buffer built unconditionally (see geometry3.c). */
    const FlecsRenderBatchImpl *self_impl =
        ecs_get(world, tctx->self_entity, FlecsRenderBatchImpl);
    wgpuRenderPassEncoderSetPipeline(pass, self_impl->pipeline_hdr);
    if (engine->materials.texture_array_bind_group) {
        wgpuRenderPassEncoderSetBindGroup(
            pass, 1, engine->materials.texture_array_bind_group, 0, NULL);
    }

    uint64_t active_group = 0;

    for (int32_t i = 0; i < total_instances; i ++) {
        uint64_t group_id = sorted[i].group_id;

        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

        const flecsEngine_batch_t *buf = ctx->batch;
        if (!buf) {
            continue;
        }

        /* When the group changes, rebind mesh vertex/index buffers. */
        if (group_id != active_group) {
            active_group = group_id;

            if (!ctx->mesh.vertex_uv_buffer ||
                !ctx->mesh.index_buffer || !ctx->mesh.index_count)
            {
                continue;
            }
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 0, ctx->mesh.vertex_uv_buffer,
                0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32,
                0, WGPU_WHOLE_SIZE);
        }

        /* Bind this instance's transform and material id */
        int32_t global_index = ctx->view.offset + sorted[i].instance_index;
        uint64_t transform_offset =
            (uint64_t)global_index * sizeof(FlecsInstanceTransform);
        uint64_t matid_offset =
            (uint64_t)global_index * sizeof(FlecsMaterialId);

        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 1, buf->buffers.gpu_transforms,
            transform_offset, sizeof(FlecsInstanceTransform));
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, buf->buffers.gpu_material_ids,
            matid_offset, sizeof(FlecsMaterialId));

        wgpuRenderPassEncoderDrawIndexed(
            pass, ctx->mesh.index_count, 1, 0, 0, 0);
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

    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA,
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
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_transparent_mesh_render,
        .ctx = flecsEngine_transparent_mesh_createCtx(batch),
        .free_ctx = flecsEngine_transparent_mesh_deleteCtx,
        .render_after_snapshot = true
    });

    return batch;
}

/* --- Transmissive mesh batch --- */

ecs_entity_t flecsEngine_createBatch_mesh_transmission(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTransmission(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf,
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
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_DEFAULT),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = true,
        .needs_transmission = true
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_mesh_transmissionData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTransmission(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
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
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_DEFAULT |
            FLECS_BATCH_OWNS_MATERIAL |
            FLECS_BATCH_OWNS_TRANSMISSION),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = true,
        .needs_transmission = true
    });

    return batch;
}
