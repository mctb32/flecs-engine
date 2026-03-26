#ifndef FLECS_ENGINE_RENDER_BATCHES_H
#define FLECS_ENGINE_RENDER_BATCHES_H

ecs_entity_t flecsEngine_createBatch_infiniteGrid(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_skybox(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_infinitePlane(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatchSet_geometry(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

#endif
