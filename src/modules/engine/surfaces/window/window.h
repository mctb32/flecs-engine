#include "../../../../types.h"
#include "../../engine.h"

#ifndef FLECS_ENGINE_WINDOW_IMPL_H
#define FLECS_ENGINE_WINDOW_IMPL_H

typedef struct {
    GLFWwindow *window;
} FlecsEngineWindowOutputConfig;

extern const FlecsEngineSurfaceInterface flecsEngineWindowOutputOps;

void FlecsEngineWindowImport(
    ecs_world_t *world);

#endif
