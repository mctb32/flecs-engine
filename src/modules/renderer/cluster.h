#ifndef FLECS_ENGINE_CLUSTER_H
#define FLECS_ENGINE_CLUSTER_H

int flecsEngine_cluster_init(
    FlecsEngineImpl *impl);

bool flecsEngine_cluster_ensureLights(
    FlecsEngineImpl *engine, int32_t needed);

void flecsEngine_cluster_build(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view);

#endif
