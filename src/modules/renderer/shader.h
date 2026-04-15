#ifndef FLECS_ENGINE_SHADER_H
#define FLECS_ENGINE_SHADER_H

void flecsEngine_shader_register(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader);

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity);

WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source);

WGPURenderPipeline flecsEngine_createFullscreenPipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const WGPUColorTargetState *color_target,
    const WGPUDepthStencilState *depth_stencil);

#endif
