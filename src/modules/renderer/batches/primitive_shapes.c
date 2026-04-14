#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t group;
    flecsEngine_batch_buffers_t buffers;
} flecsEngine_primitive_ctx_t;

static void* flecsEngine_primitive_createCtx(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    flecsEngine_primitive_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_primitive_ctx_t);
    flecsEngine_batch_buffers_init(&ctx->buffers,
        FLECS_BATCH_BUFFERS_STORAGE |
        (owns_material_data ? FLECS_BATCH_BUFFERS_OWNS_MATERIAL : 0));
    flecsEngine_batch_init(&ctx->group, world, mesh, 0,
        component, scale_callback);
    ctx->group.buffers = &ctx->buffers;
    return ctx;
}

static void flecsEngine_primitive_deleteCtx(void *ptr)
{
    flecsEngine_primitive_ctx_t *ctx = ptr;
    flecsEngine_batch_fini(&ctx->group);
    flecsEngine_batch_buffers_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_primitive_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_batch_buffers_t *buf = ctx->buffers;

    ctx->vertex_buffer = ctx->mesh.vertex_buffer;

redo:
    ctx->offset = 0;
    flecsEngine_batch_extractInstances(world, engine, batch, ctx);

    if (ctx->count > buf->capacity) {
        flecsEngine_batch_buffers_ensureCapacity(engine, buf, ctx->count);
        goto redo;
    }

    buf->count = ctx->count;
}

static void flecsEngine_primitive_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    (void)world;
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_buffers_upload(engine, &pctx->buffers);
}

static void flecsEngine_primitive_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_batch_buffers_t *buf = ctx->buffers;

redo_shadow:
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        ctx->shadow_offset[c] = 0;
    }
    flecsEngine_batch_extractShadowInstances(world, engine, batch, ctx);

    {
        int32_t max_shadow = 0;
        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
            if (ctx->shadow_count[c] > max_shadow) {
                max_shadow = ctx->shadow_count[c];
            }
        }
        if (max_shadow > buf->shadow_capacity) {
            flecsEngine_batch_buffers_ensureShadowCapacity(
                engine, buf, max_shadow);
            goto redo_shadow;
        }
    }

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        buf->shadow_count[c] = ctx->shadow_count[c];
    }
}

static void flecsEngine_primitive_uploadShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    (void)world;
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_buffers_uploadShadow(engine, &pctx->buffers);
}

static void flecsEngine_primitive_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_bindMaterialGroup(
        (FlecsEngineImpl*)engine, pass, &pctx->buffers);

    if (batch->vertex_type == ecs_id(FlecsLitVertexUv)) {
        pctx->group.vertex_buffer = pctx->group.mesh.vertex_uv_buffer;
    }
    flecsEngine_batch_draw(engine, pass, &pctx->group);
}

static void flecsEngine_primitive_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_drawShadow(engine, pass, &pctx->group);
}

static void flecsEngine_box_scale(
    const void *ptr,
    float *scale)
{
    const FlecsBox *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static void flecsEngine_quad_scale(
    const void *ptr,
    float *scale)
{
    const FlecsQuad *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_right_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static ecs_entity_t flecsEngine_createBatch_primitive_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsMesh3Impl *mesh,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    ecs_entity_t exclude)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_desc_t desc = {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA }
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[3] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
    }

    ecs_query_t *q = ecs_query_init(world, &desc);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertexUv),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .extract_callback = flecsEngine_primitive_extract,
        .shadow_extract_callback = flecsEngine_primitive_extractShadow,
        .upload_callback = flecsEngine_primitive_upload,
        .shadow_upload_callback = flecsEngine_primitive_uploadShadow,
        .callback = flecsEngine_primitive_render,
        .shadow_callback = flecsEngine_primitive_renderShadow,
        .ctx = flecsEngine_primitive_createCtx(
            world, mesh, false, component, scale_callback),
        .free_ctx = flecsEngine_primitive_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_primitive(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsMesh3Impl *mesh,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    ecs_entity_t exclude)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_desc_t desc = {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional  },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[6] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
    }

    ecs_query_t *q = ecs_query_init(world, &desc);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertexUv),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .extract_callback = flecsEngine_primitive_extract,
        .shadow_extract_callback = flecsEngine_primitive_extractShadow,
        .upload_callback = flecsEngine_primitive_upload,
        .shadow_upload_callback = flecsEngine_primitive_uploadShadow,
        .callback = flecsEngine_primitive_render,
        .shadow_callback = flecsEngine_primitive_renderShadow,
        .ctx = flecsEngine_primitive_createCtx(
            world, mesh, true, component, scale_callback),
        .free_ctx = flecsEngine_primitive_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_box_getAsset(world), ecs_id(FlecsBox),
        flecsEngine_box_scale, ecs_id(FlecsBevel));
}

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_quad_getAsset(world), ecs_id(FlecsQuad),
        flecsEngine_quad_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_triangle_getAsset(world), ecs_id(FlecsTriangle),
        flecsEngine_triangle_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_rightTriangle_getAsset(world), ecs_id(FlecsRightTriangle),
        flecsEngine_right_triangle_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_trianglePrism_getAsset(world), ecs_id(FlecsTrianglePrism),
        flecsEngine_triangle_prism_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_rightTrianglePrism_getAsset(world), ecs_id(FlecsRightTrianglePrism),
        flecsEngine_right_triangle_prism_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_box_getAsset(world), ecs_id(FlecsBox),
        flecsEngine_box_scale, ecs_id(FlecsBevel));
}

ecs_entity_t flecsEngine_createBatch_quads_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_quad_getAsset(world), ecs_id(FlecsQuad),
        flecsEngine_quad_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_triangle_getAsset(world), ecs_id(FlecsTriangle),
        flecsEngine_triangle_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_rightTriangle_getAsset(world), ecs_id(FlecsRightTriangle),
        flecsEngine_right_triangle_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_trianglePrism_getAsset(world), ecs_id(FlecsTrianglePrism),
        flecsEngine_triangle_prism_scale, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_rightTrianglePrism_getAsset(world), ecs_id(FlecsRightTrianglePrism),
        flecsEngine_right_triangle_prism_scale, 0);
}
