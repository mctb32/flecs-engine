#ifndef FLECS_ENGINE_SHADOW_H
#define FLECS_ENGINE_SHADOW_H

#define FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT 4096

int flecsEngine_shadow_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_cleanup(
    FlecsEngineImpl *impl);

int flecsEngine_shadow_ensureSize(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_computeCascades(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    uint32_t shadow_map_size,
    float max_range,
    mat4 out_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float out_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT]);

#endif
