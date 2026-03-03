#ifndef FLECS_ENGINE_TYPES_H
#define FLECS_ENGINE_TYPES_H

#include "flecs_engine.h"
#include "vendor.h"

#define FLECS_ENGINE_UNIFORMS_MAX (8)
#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

struct FlecsEngineSurfaceInterface;

typedef struct {
    GLFWwindow *window;
    int32_t width;
    int32_t height;
    const struct FlecsEngineSurfaceInterface *surface_impl;
    bool output_done;
    const char *frame_output_path;
    flecs_rgba_t clear_color;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTexture frame_output_texture;
    WGPUTextureView frame_output_texture_view;

    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    WGPUSurfaceConfiguration surface_config;
    WGPUTextureFormat hdr_color_format;

    /* Reusable intermediate color targets for post-processing passes. */
    WGPUTexture *effect_target_textures;
    WGPUTextureView *effect_target_views;
    int32_t effect_target_count;
    uint32_t effect_target_width;
    uint32_t effect_target_height;
    WGPUTextureFormat effect_target_format;

    ecs_query_t *view_query;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

typedef struct {
    WGPUBuffer vertex_buffer; /* vec<FlecsLitVertex> */
    WGPUBuffer index_buffer;  /* vec<uint16_t> */
    int32_t vertex_count;
    int32_t index_count;
} FlecsMesh3Impl;

extern ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

typedef struct {
    WGPUShaderModule shader_module;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPURenderPipeline pipeline_surface;
    WGPURenderPipeline pipeline_hdr;
    WGPUBuffer uniform_buffers[FLECS_ENGINE_UNIFORMS_MAX];
    uint8_t uniform_count;
} FlecsRenderBatchImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPURenderPipeline pipeline_surface;
    WGPURenderPipeline pipeline_hdr;
    WGPUSampler input_sampler;
    WGPUTexture tony_lut_texture;
    WGPUTextureView tony_lut_texture_view;
    WGPUSampler tony_lut_sampler;
    WGPUBindGroupLayout bloom_bind_layout;
    WGPURenderPipeline bloom_downsample_first_pipeline;
    WGPURenderPipeline bloom_downsample_pipeline;
    WGPURenderPipeline bloom_upsample_pipeline;
    WGPURenderPipeline bloom_upsample_final_surface_pipeline;
    WGPURenderPipeline bloom_upsample_final_hdr_pipeline;
    WGPUSampler bloom_sampler;
    WGPUBuffer bloom_uniform_buffer;
    WGPUTexture bloom_texture;
    WGPUTextureView *bloom_mip_views;
    uint32_t bloom_mip_count;
    uint32_t bloom_texture_width;
    uint32_t bloom_texture_height;
    WGPUTextureFormat bloom_texture_format;
    FlecsBloomSettings bloom_settings;
} FlecsRenderEffectImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 mvp;
} FlecsCameraImpl;

extern ECS_COMPONENT_DECLARE(FlecsCameraImpl);

#endif
