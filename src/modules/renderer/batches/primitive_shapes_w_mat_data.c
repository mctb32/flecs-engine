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
    flecsEngine_batch_buffers_init(&ctx->buffers, owns_material_data);
    flecsEngine_batch_init(&ctx->group, world, mesh, 0, owns_material_data,
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
    ecs_entity_t shader = flecsEngine_shader_pbrColoredMaterialIndex(world);

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
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_primitive_extract,
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
        .extract_callback = flecsEngine_primitive_extract,
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
