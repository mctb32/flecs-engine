#ifndef FLECS_ENGINE_DEPTH_PREPASS_H
#define FLECS_ENGINE_DEPTH_PREPASS_H

#include "../../types.h"

int flecsEngine_depthPrepass_init(
    FlecsEngineImpl *engine);

void flecsEngine_depthPrepass_fini(
    FlecsEngineImpl *engine);

WGPURenderPipeline flecsEngine_depthPrepass_createPipeline(
    const FlecsEngineImpl *engine,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
    uint32_t sample_count);

#endif
