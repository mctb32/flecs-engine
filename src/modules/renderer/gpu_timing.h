#ifndef FLECS_ENGINE_GPU_TIMING_H
#define FLECS_ENGINE_GPU_TIMING_H

#include "../../types.h"

int flecsEngine_gpuTiming_init(FlecsEngineImpl *engine);

void flecsEngine_gpuTiming_fini(FlecsEngineImpl *engine);

bool flecsEngine_gpuTiming_beginFrame(FlecsEngineImpl *engine);

int flecsEngine_gpuTiming_allocPair(
    FlecsEngineImpl *engine,
    const char *name);

void flecsEngine_gpuTiming_computePassTimestamps(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPUComputePassTimestampWrites *out);

void flecsEngine_gpuTiming_renderPassTimestamps(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPURenderPassTimestampWrites *out);

void flecsEngine_gpuTiming_endFrame(
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder);

void flecsEngine_gpuTiming_afterSubmit(FlecsEngineImpl *engine);

void flecsEngine_gpuTiming_logIfReady(FlecsEngineImpl *engine);

#endif
