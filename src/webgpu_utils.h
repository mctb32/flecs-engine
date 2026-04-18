#ifndef FLECS_ENGINE_WEBGPU_UTILS_H
#define FLECS_ENGINE_WEBGPU_UTILS_H

#include "types.h"

WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source);

WGPURenderPipeline flecsEngine_createFullscreenPipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *vertex_entry,   /* NULL => "vs_main" */
    const char *fragment_entry, /* NULL => "fs_main" */
    const WGPUColorTargetState *color_target,
    const WGPUDepthStencilState *depth_stencil);

WGPURenderPassEncoder flecsEngine_beginColorPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value);

bool flecsEngine_fullscreenPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group);

WGPUComputePipeline flecsEngine_createComputePipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *entry);

WGPUSampler flecsEngine_createLinearClampSampler(
    WGPUDevice device);

WGPUBuffer flecsEngine_createUniformBuffer(
    WGPUDevice device,
    uint64_t size);

#define FLECS_WGPU_RELEASE(field, releaseFn) \
    do { if ((field)) { (releaseFn)(field); (field) = NULL; } } while (0)

#endif
