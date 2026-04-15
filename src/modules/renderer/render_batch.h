#ifndef FLECS_ENGINE_RENDER_BATCH_H
#define FLECS_ENGINE_RENDER_BATCH_H

struct FlecsRenderBatch;

typedef void (*flecs_render_batch_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

typedef void (*flecs_render_batch_extract_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const struct FlecsRenderBatch *batch);

// Render entities matching a query with specified shader
ECS_STRUCT(FlecsRenderBatch, {
    ecs_entity_t shader;
    ecs_query_t *query;
    ecs_entity_t vertex_type;
    ecs_entity_t instance_types[FLECS_ENGINE_INSTANCE_TYPES_MAX];
    WGPUCompareFunction depth_test;
    WGPUCullMode cull_mode;
    WGPUBlendState blend;
    bool depth_write;
ECS_PRIVATE
    flecs_render_batch_extract_callback extract_callback;
    flecs_render_batch_extract_callback shadow_extract_callback;
    flecs_render_batch_extract_callback upload_callback;
    flecs_render_batch_extract_callback shadow_upload_callback;
    flecs_render_batch_callback callback;
    flecs_render_batch_callback shadow_callback;
    void *ctx;
    void (*free_ctx)(void *ctx);
    bool render_after_snapshot;
    bool needs_transmission;
});

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_extractShadow(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_upload(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_uploadShadow(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_register(
    ecs_world_t *world);

void flecsEngine_renderBatchSet_register(
    ecs_world_t *world);

bool flecsEngine_renderBatchSet_hasTransmission(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_extractShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_upload(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_uploadShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    int phase);

void flecsEngine_renderBatchSet_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass);

#endif
