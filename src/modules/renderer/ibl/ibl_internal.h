#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl);

void flecsEngie_ibl_releaseRuntimeResources(
    FlecHdriImpl *ibl);

#endif
