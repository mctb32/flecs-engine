#ifndef FLECS_ENGINE_TYPES_COMPONENTS_H
#define FLECS_ENGINE_TYPES_COMPONENTS_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "engine_state.h"

typedef struct {
    WGPUTexture ibl_equirect_texture;
    WGPUTextureView ibl_equirect_texture_view;
    WGPUTexture ibl_prefiltered_cubemap;
    WGPUTextureView ibl_prefiltered_cubemap_view;
    WGPUTexture ibl_irradiance_cubemap;
    WGPUTextureView ibl_irradiance_cubemap_view;
    WGPUSampler ibl_sampler;
    WGPUBindGroup ibl_shadow_bind_group;
    uint32_t scene_bind_version;
    uint32_t ibl_prefilter_mip_count;
} FlecsHdriImpl;

extern ECS_COMPONENT_DECLARE(FlecsHdriImpl);

typedef struct {
    WGPUBuffer vertex_buffer;    /* vec<FlecsLitVertex> */
    WGPUBuffer vertex_uv_buffer; /* vec<FlecsLitVertexUv> (only if mesh has UVs) */
    WGPUBuffer index_buffer;     /* vec<uint32_t> */
    int32_t vertex_count;
    int32_t index_count;
    bool has_uvs;
    float aabb_min[3];           /* local-space AABB minimum */
    float aabb_max[3];           /* local-space AABB maximum */
} FlecsMesh3Impl;

extern ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

typedef struct {
    WGPUTexture texture;
    WGPUTextureView view;
} FlecsTextureImpl;

extern ECS_COMPONENT_DECLARE(FlecsTextureImpl);

typedef struct {
    WGPUShaderModule shader_module;
    bool uses_ibl;
    bool uses_shadow;
    bool uses_cluster;
    bool uses_textures;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUTexture *effect_target_textures;
    WGPUTextureView *effect_target_views;
    int32_t effect_target_count;
    uint32_t effect_target_width;
    uint32_t effect_target_height;
    WGPUTextureFormat effect_target_format;
    WGPUBindGroup passthrough_bind_group;
} FlecsRenderViewImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

typedef struct {
    WGPURenderPipeline pipeline_hdr;
    WGPURenderPipeline pipeline_shadow;
    bool uses_material;
    bool uses_ibl;
    bool uses_shadow;
    bool uses_cluster;
    bool uses_textures;
} FlecsRenderBatchImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPURenderPipeline pipeline_surface;
    WGPURenderPipeline pipeline_hdr;
    WGPUSampler input_sampler;
} FlecsRenderEffectImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);

typedef struct {
    WGPUBuffer uniform_buffer;
} FlecsHeightFogImpl;

extern ECS_COMPONENT_DECLARE(FlecsHeightFogImpl);

typedef struct {
    WGPUBuffer uniform_buffer;
    WGPUTexture blur_intermediate_texture;
    WGPUTextureView blur_intermediate_view;
    uint32_t blur_texture_width;
    uint32_t blur_texture_height;
    WGPUBindGroupLayout blur_bind_layout;
    WGPURenderPipeline blur_pipeline_surface;
    WGPURenderPipeline blur_pipeline_hdr;
} FlecsSSAOImpl;

extern ECS_COMPONENT_DECLARE(FlecsSSAOImpl);

typedef struct {
    WGPUTexture tony_lut_texture;
    WGPUTextureView tony_lut_texture_view;
    WGPUSampler tony_lut_sampler;
} FlecsTonyImpl;

extern ECS_COMPONENT_DECLARE(FlecsTonyImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPURenderPipeline downsample_first_pipeline;
    WGPURenderPipeline downsample_pipeline;
    WGPURenderPipeline upsample_pipeline;
    WGPURenderPipeline upsample_final_surface_pipeline;
    WGPURenderPipeline upsample_final_hdr_pipeline;
    WGPUSampler sampler;
    WGPUBuffer uniform_buffer;
    WGPUTexture texture;
    WGPUTextureView *mip_views;
    uint32_t mip_count;
    uint32_t texture_width;
    uint32_t texture_height;
    WGPUTextureFormat texture_format;
} FlecsBloomImpl;

extern ECS_COMPONENT_DECLARE(FlecsBloomImpl);

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 mvp;
} FlecsCameraImpl;

extern ECS_COMPONENT_DECLARE(FlecsCameraImpl);

#endif
