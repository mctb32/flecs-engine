#include "mesh/mesh.h"

/* --- Non-textured mesh batch creation --- */

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
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
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_BUFFERS_STORAGE),
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
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_BUFFERS_STORAGE | FLECS_BATCH_BUFFERS_OWNS_MATERIAL),
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
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
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
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_BUFFERS_STORAGE),
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
    flecsEngine_batch_buffers_t buffers;
    ecs_entity_t self_entity;
    ecs_entity_t textured_helper;
} flecsEngine_transparent_mesh_ctx_t;

static flecsEngine_transparent_mesh_ctx_t* flecsEngine_transparent_mesh_createCtx(
    ecs_entity_t self_entity,
    ecs_entity_t textured_helper)
{
    flecsEngine_transparent_mesh_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_transparent_mesh_ctx_t);
    flecsEngine_batch_buffers_init(&ctx->buffers,
        FLECS_BATCH_BUFFERS_STORAGE);
    ctx->self_entity = self_entity;
    ctx->textured_helper = textured_helper;
    return ctx;
}

static void flecsEngine_transparent_mesh_deleteCtx(void *ptr)
{
    flecsEngine_transparent_mesh_ctx_t *ctx = ptr;
    flecsEngine_batch_buffers_fini(&ctx->buffers);
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

            if (sorted[i].is_textured && tex_pipeline &&
                engine->materials.texture_array_bind_group)
            {
                if (active_pipeline != tex_pipeline) {
                    wgpuRenderPassEncoderSetPipeline(pass, tex_pipeline);
                    active_pipeline = tex_pipeline;
                }

                wgpuRenderPassEncoderSetBindGroup(
                    pass, 1,
                    engine->materials.texture_array_bind_group,
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

                    if (engine->empty_bind_group) {
                        wgpuRenderPassEncoderSetBindGroup(
                            pass, 1, engine->empty_bind_group, 0, NULL);
                    }
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

    ecs_entity_t textured_helper = ecs_entity(world, {
        .parent = batch });
    ecs_set(world, textured_helper, FlecsRenderBatch, {
        .shader = flecsEngine_shader_pbrColored(world),
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
        .depth_write = false
    });

    /* Unified query matches all transparent meshes regardless of textures */
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
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
        .ctx = flecsEngine_transparent_mesh_createCtx(batch, textured_helper),
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
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA },
            /* Entities with self-level FlecsTransmission go to the
             * per-instance colored transmission batch below. */
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
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_BUFFERS_STORAGE),
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
    ecs_entity_t shader = flecsEngine_shader_pbrTransmissionColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
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
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba),
            ecs_id(FlecsPbrMaterial),
            ecs_id(FlecsEmissive),
            ecs_id(FlecsTransmission)
        },
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_BUFFERS_OWNS_MATERIAL | FLECS_BATCH_BUFFERS_OWNS_TRANSMISSION),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = true,
        .needs_transmission = true
    });

    return batch;
}
