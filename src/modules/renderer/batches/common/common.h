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

typedef void (*flecsEngine_primitive_scale_t)(
    const void *value,
    float *out);

typedef struct {
    FlecsInstanceTransform *cpu_transforms;
    FlecsRgba *cpu_colors;
    FlecsPbrMaterial *cpu_pbr_materials;
    FlecsEmissive *cpu_emissives;
    FlecsTransmission *cpu_transmissions;
    FlecsMaterialId *cpu_material_ids;
    FlecsGpuMaterial *cpu_gpu_materials;
    int32_t count;
    int32_t capacity;

    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_pbr;
    WGPUBuffer instance_emissive;
    WGPUBuffer instance_transmission;
    WGPUBuffer instance_material_id;
    WGPUBuffer material_storage;
    WGPUBindGroup material_bind_group;

    WGPUBuffer shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    FlecsInstanceTransform *cpu_shadow_transforms[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_capacity;

    bool owns_material_data;
    bool owns_transmission_data;
    bool use_material_storage;
} flecsEngine_batch_buffers_t;

typedef struct {
    flecsEngine_batch_buffers_t *buffers;
    int32_t count;
    int32_t offset;
    FlecsMesh3Impl mesh;
    WGPUBuffer vertex_buffer;

    ecs_entity_t component;
    ecs_size_t component_size;
    flecsEngine_primitive_scale_t scale_callback;

    uint64_t group_id;

    int32_t shadow_count[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int32_t shadow_offset[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
} flecsEngine_batch_t;

typedef enum {
    FLECS_BATCH_BUFFERS_DEFAULT = 0,
    FLECS_BATCH_BUFFERS_OWNS_MATERIAL = 1 << 0,
    FLECS_BATCH_BUFFERS_OWNS_TRANSMISSION = 1 << 1,
    FLECS_BATCH_BUFFERS_STORAGE = 1 << 2
} flecsEngine_batch_buffers_flags_t;

void flecsEngine_batch_buffers_init(
    flecsEngine_batch_buffers_t *buf,
    flecsEngine_batch_buffers_flags_t flags);

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

void flecsEngine_batch_extractInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx);

void flecsEngine_batch_extractShadowInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx);

void flecsEngine_batch_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

void flecsEngine_batch_drawShadow(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

FlecsGpuMaterial flecsEngine_batch_packGpuMaterial(
    const FlecsEngineImpl *engine,
    const FlecsRgba *color,
    const FlecsPbrMaterial *pbr,
    const FlecsEmissive *emissive);

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_buffers_t *buf);

void flecsEngine_batch_init(
    flecsEngine_batch_t* batch,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ptr);

void flecsEngine_batch_delete(
    void *ptr);

void flecsEngine_batch_buffers_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf);

void flecsEngine_batch_buffers_uploadShadow(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_buffers_t *buf);

void flecsEngine_batch_transformInstance(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

void flecsEngine_batch_extractSingleInstance(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *batch,
    const FlecsWorldTransform3 *transform,
    const FlecsRgba *color,
    float scale_x,
    float scale_y,
    float scale_z);


/* -- Meshes -- */

flecsEngine_batch_buffers_t* flecsEngine_mesh_createCtx(
    flecsEngine_batch_buffers_flags_t flags);

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
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_extractShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_uploadShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

#endif
