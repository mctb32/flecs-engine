#include "../../private.h"

#ifndef FLECS_ENGINE_IMPL
#define FLECS_ENGINE_IMPL

int flecsEngine_init(
    ecs_world_t *world,
    const FlecsEngineOutputDesc *output);

bool flecsEngine_surfaceInterface_isValid(
    const FlecsEngineSurfaceInterface *ops);

int flecsEngine_surfaceInterface_initInstance(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    const void *config);

int flecsEngine_surfaceInterface_configureTarget(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine);

int flecsEngine_surfaceInterface_prepareFrame(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine);

int flecsEngine_surfaceInterface_acquireFrame(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    FlecsEngineSurface *target);

int flecsEngine_surfaceInterface_encodeFrame(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target);

int flecsEngine_surfaceInterface_submitFrame(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsEngineSurface *target);

void flecsEngine_surfaceInterface_onFrameFailed(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_surfaceInterface_cleanup(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    bool terminate_runtime);

#endif
