#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
    flecsEngine_batch_buffers_t buffers;
    FlecsWorldTransform3 transform;
    FlecsRgba color;
} flecs_engine_skybox_ctx_t;

static flecs_engine_skybox_ctx_t* flecsEngine_skybox_createCtx(
    ecs_world_t *world)
{
    flecs_engine_skybox_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_skybox_ctx_t);
    flecsEngine_batch_buffers_init(&result->buffers, true);
    flecsEngine_batch_init(
        &result->batch, world, flecsEngine_quad_getAsset(world), 0, true, 0, NULL);
    result->batch.buffers = &result->buffers;
    glm_mat4_identity(result->transform.m);
    result->color = (FlecsRgba){255, 255, 255, 255};
    return result;
}

static void flecsEngine_skybox_deleteCtx(
    void *arg)
{
    flecs_engine_skybox_ctx_t *ctx = arg;
    flecsEngine_batch_fini(&ctx->batch);
    flecsEngine_batch_buffers_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_skybox_extract(
    const FlecsEngineImpl *engine,
    flecs_engine_skybox_ctx_t *ctx)
{
    flecsEngine_batch_extractSingleInstance(
        engine, &ctx->batch, &ctx->transform, &ctx->color,
        2.0f, 2.0f, 1.0f);
}

static void flecsEngine_skybox_extractCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecs_engine_skybox_ctx_t *ctx = batch->ctx;
    flecsEngine_skybox_extract(engine, ctx);
}

static void flecsEngine_skybox_renderCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecs_engine_skybox_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_draw(engine, pass, &ctx->batch);
}

void FlecsOnAddSkyBoxBatch(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t batch = it->entities[i];
        ecs_entity_t shader = flecsEngine_shader_skybox(it->world);

        ecs_set(it->world, batch, FlecsRenderBatch, {
            .shader = shader,
            .vertex_type = ecs_id(FlecsLitVertex),
            .instance_types = {
                ecs_id(FlecsInstanceTransform),
                ecs_id(FlecsRgba)
            },
            .depth_test = WGPUCompareFunction_LessEqual,
            .cull_mode = WGPUCullMode_None,
            .depth_write = false,
            .extract_callback = flecsEngine_skybox_extractCallback,
            .callback = flecsEngine_skybox_renderCallback,
            .ctx = flecsEngine_skybox_createCtx((ecs_world_t*)it->world),
            .free_ctx = flecsEngine_skybox_deleteCtx
        });
    }
}
