#ifndef FLECS_ENGINE_RENDER_BATCH_H
#define FLECS_ENGINE_RENDER_BATCH_H

#include "renderer.h"

typedef void (*flecsEngine_primitive_scale_t)(
    const void *value,
    float *out);

/* Shared GPU+CPU instance buffers. One per batch, shared across all groups. */
typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_pbr;
    WGPUBuffer instance_emissive;
    WGPUBuffer instance_material_id;
    FlecsInstanceTransform *cpu_transforms;
    FlecsRgba *cpu_colors;
    FlecsPbrMaterial *cpu_pbr_materials;
    FlecsEmissive *cpu_emissives;
    FlecsMaterialId *cpu_material_ids;
    int32_t count;
    int32_t capacity;
    bool owns_material_data;

    /* Per-cascade shadow transform buffers (only transforms needed) */
    WGPUBuffer shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    FlecsInstanceTransform *cpu_shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_capacity;
} flecsEngine_batch_buffers_t;

/* Per-group lightweight descriptor. Points into shared buffers at `offset`. */
typedef struct {
    flecsEngine_batch_buffers_t *buffers;
    int32_t count;
    int32_t offset;
    FlecsMesh3Impl mesh;
    WGPUBuffer vertex_buffer; /* vertex buffer used for drawing (set by caller) */

    ecs_entity_t component;
    ecs_size_t component_size;
    flecsEngine_primitive_scale_t scale_callback;

    uint64_t group_id;
    bool owns_material_data;

    /* Per-cascade shadow instance counts and offsets */
    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_offset[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
} flecsEngine_batch_t;

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_buffers_init(
    flecsEngine_batch_buffers_t *buf,
    bool owns_material_data);

void flecsEngine_batch_buffers_fini(
    flecsEngine_batch_buffers_t *buf);

void flecsEngine_batch_buffers_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t count);

void flecsEngine_batch_buffers_ensureShadowCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *buf,
    int32_t count);

void flecsEngine_batch_buffers_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf);

void flecsEngine_batch_buffers_uploadShadow(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf);

/* --- Per-group batch lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t* batch,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ptr);

void flecsEngine_batch_delete(
    void *ptr);

/* Extract visible instances for a single group into shared buffers at
 * ctx->offset. Tests against the camera frustum only. Does NOT upload —
 * caller must call flecsEngine_batch_buffers_upload after all groups are
 * extracted. */
void flecsEngine_batch_extractInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx);

/* Extract shadow instances for a single group into per-cascade shadow
 * buffers at ctx->shadow_offset. Tests against each cascade frustum.
 * Does NOT upload — caller must call flecsEngine_batch_buffers_uploadShadow
 * after all groups are extracted. */
void flecsEngine_batch_extractShadowInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx);

/* Draw a single group using the shared buffers at ctx->offset.
 * Uses ctx->vertex_buffer for vertex slot 0. */
void flecsEngine_batch_draw(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

/* Draw a single group for a specific shadow cascade using per-cascade
 * shadow transform buffers populated during extraction. */
void flecsEngine_batch_drawShadow(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx,
    int cascade);

void flecsEngine_batch_transformInstance(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

void flecsEngine_box_scale(
    const void *ptr,
    float *scale);

void flecsEngine_quad_scale(
    const void *ptr,
    float *scale);

void flecsEngine_triangle_scale(
    const void *ptr,
    float *scale);

void flecsEngine_right_triangle_scale(
    const void *ptr,
    float *scale);

void flecsEngine_triangle_prism_scale(
    const void *ptr,
    float *scale);

void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale);

void flecsEngine_primitive_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const struct FlecsRenderBatch *batch);

void flecsEngine_primitive_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const struct FlecsRenderBatch *batch);

void flecsEngine_primitive_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

void flecsEngine_primitive_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

void flecsEngine_batch_extractSingleInstance(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *batch,
    const FlecsWorldTransform3 *transform,
    const FlecsRgba *color,
    float scale_x,
    float scale_y,
    float scale_z);

ecs_entity_t flecsEngine_createBatch_mesh_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_quads_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_bevel_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_bevel_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

/* Shared mesh batch infrastructure (used by textured_mesh too) */
uint64_t flecsEngine_mesh_groupByMesh(
    ecs_world_t *world,
    ecs_table_t *table,
    ecs_id_t id,
    void *ctx);

void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr);

void flecsEngine_mesh_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr);

void flecsEngine_mesh_extractGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    uint64_t group_id,
    flecsEngine_batch_buffers_t *shared);

void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

#endif
