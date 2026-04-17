#ifndef FLECS_ENGINE_RENDER_BATCH_COMMON_H
#define FLECS_ENGINE_RENDER_BATCH_COMMON_H

#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../../../../tracy_hooks.h"
#include "../../frustum_cull.h"
#include "../batches.h"
#include "flecs_engine.h"

/* -- Storing data from ECS into CPU & GPU buffers -- */

typedef enum {
    FLECS_BATCH_DEFAULT = 0,
    FLECS_BATCH_OWNS_MATERIAL = 1 << 0,
    FLECS_BATCH_OWNS_TRANSMISSION = 1 << 1,
} flecsEngine_batch_flags_t;

typedef void (*flecsEngine_primitive_scale_t)(
    const void *value,
    float *out);

typedef void (*flecsEngine_primitive_scale_aabb_t)(
    FlecsAABB *aabb,
    const void *scale_data,
    int32_t count);

/* Set of CPU/GPU buffers that store extracted data from the ECS for a batch. */
typedef struct {
    FlecsGpuTransform *cpu_transforms;
    FlecsMaterialId *cpu_material_ids;
    FlecsGpuMaterial *cpu_materials;
    FlecsGpuTransform *cpu_shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];

    WGPUBuffer gpu_transforms;
    WGPUBuffer gpu_material_ids;
    WGPUBuffer gpu_materials;
    WGPUBindGroup gpu_material_bind_group;
    WGPUBuffer gpu_shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    
    int32_t count;
    int32_t capacity;

    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_capacity;
} flecsEngine_batch_buffers_t;

/* Batch: render data for all entities matching the batch query. */
typedef struct {
    flecsEngine_batch_buffers_t buffers;
    ecs_flags32_t flags;
} flecsEngine_batch_t;

/* View on batch buffers */
typedef struct {
    int32_t count;
    int32_t offset;

    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_offset[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
} flecsEngine_batch_view_t;

/* Batch group: each batch can have multiple batch groups, where a group is
 * associated with a single mesh. */
typedef struct {
    flecsEngine_batch_t *batch;
    flecsEngine_batch_view_t view;

    uint64_t group_id;
    FlecsMesh3Impl mesh;
} flecsEngine_batch_group_t;

/* Primitive batch group: like a batch group for buitin primitive meshes. This
 * type has additional functionality for applying the scale from a primitive
 * component. For example, if an entity has "FlecsBox: {10, 10, 10}", the 
 * scale_callback() function will apply a {10, 10, 10} scale to the builtin
 * Box mesh. This scale is applied on top of an optional FlecsScale component. */
typedef struct {
    flecsEngine_batch_group_t group;
    ecs_size_t component_size;
    flecsEngine_primitive_scale_t scale_callback;
    flecsEngine_primitive_scale_aabb_t scale_aabb;
} flecsEngine_primitive_batch_group_t;

void flecsEngine_batch_init(
    flecsEngine_batch_t *buf,
    flecsEngine_batch_flags_t flags);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *buf);

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count);

void flecsEngine_batch_ensureShadowCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count);

void flecsEngine_batch_group_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t component_size);

void flecsEngine_batch_group_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    ecs_size_t scale_size);

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_group_init(
    flecsEngine_batch_group_t* batch,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id);

flecsEngine_batch_group_t* flecsEngine_batch_group_create(
    const FlecsMesh3Impl *mesh,
    uint64_t group_id);

void flecsEngine_batch_group_fini(
    flecsEngine_batch_group_t *ptr);

void flecsEngine_batch_group_delete(
    void *ptr);

void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_uploadShadow(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_transformInstance(
    FlecsGpuTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

/* -- Meshes -- */

flecsEngine_batch_t* flecsEngine_mesh_createCtx(
    flecsEngine_batch_flags_t flags);

void flecsEngine_mesh_deleteCtx(void *ptr);

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

void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_uploadShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

#endif
