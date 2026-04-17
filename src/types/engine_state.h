#ifndef FLECS_ENGINE_TYPES_ENGINE_STATE_H
#define FLECS_ENGINE_TYPES_ENGINE_STATE_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "gpu_types.h"

typedef struct {
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout pass_bind_layout;
    WGPUSampler sampler;
} flecsEngine_shadow_t;

typedef struct {
    FlecsGpuLight *cpu_lights;
    int32_t light_count;
    int32_t light_capacity;
    WGPUBuffer light_buffer;

    ecs_query_t *point_light_query;
    ecs_query_t *spot_light_query;
} flecsEngine_lighting_t;

#define FLECS_ENGINE_TEXTURE_BUCKET_COUNT 3

typedef struct {
    WGPUTexture texture_arrays[4];       /* albedo, emissive, roughness, normal */
    WGPUTextureView texture_array_views[4];
    uint32_t layer_counts[4];
    uint32_t mip_count;
    uint32_t width;
    uint32_t height;
    bool is_bc7;
} flecsEngine_texture_bucket_t;

typedef struct {
    WGPUBuffer buffer;
    FlecsGpuMaterial *cpu_materials;
    uint32_t buffer_capacity;
    uint32_t count;
    ecs_query_t *query;
    uint32_t next_id;

    uint32_t dirty_version;
    uint32_t uploaded_version;

    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    uint32_t bind_version;

    WGPUBuffer id_identity_buffer;
    int32_t id_identity_capacity;
} flecsEngine_materials_t;

typedef struct {
    ecs_query_t *query;

    flecsEngine_texture_bucket_t buckets[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    WGPUBindGroup array_bind_group;

    WGPUBindGroupLayout pbr_bind_layout;
    WGPUSampler pbr_sampler;

    WGPUTexture fallback_white_tex;
    WGPUTextureView fallback_white_array_view;
    WGPUTextureView fallback_white_view;
    WGPUTexture fallback_normal_tex;
    WGPUTextureView fallback_normal_array_view;

    WGPURenderPipeline blit_pipeline;
    WGPUBindGroupLayout blit_bind_layout;
    WGPUSampler blit_sampler;
    WGPUComputePipeline mipgen_pipeline;
    WGPUBindGroupLayout mipgen_bind_layout;
} flecsEngine_textures_t;

typedef struct {
    WGPURenderPipeline passthrough_pipeline;
    WGPUBindGroupLayout passthrough_bind_layout;
    WGPUSampler passthrough_sampler;

    WGPURenderPipeline depth_resolve_pipeline;
    WGPUBindGroupLayout depth_resolve_bind_layout;

    WGPURenderPipeline opaque_snapshot_downsample_pipeline;
    WGPUBindGroupLayout opaque_snapshot_downsample_layout;
    WGPUSampler opaque_snapshot_sampler;
} flecsEngine_pipelines_t;

typedef struct {
    FlecsRgba *color;
    ecs_size_t color_count;
    FlecsPbrMaterial *material;
    ecs_size_t material_count;
    FlecsEmissive *emissive;
    ecs_size_t emissive_count;
} flecsEngine_default_attr_cache_t;

typedef struct {
    ecs_entity_t surface;
    ecs_entity_t fallback_hdri;
    ecs_query_t *view_query;

    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTextureFormat target_format;
    WGPUTextureFormat hdr_color_format;
    
    WGPUBindGroupLayout scene_bind_layout;
    uint32_t scene_bind_version;

    WGPUBindGroupLayout no_texture_bind_layout;
    WGPUBindGroup no_texture_bind_group;

    WGPUBindGroupLayout instance_bind_layout;

    flecsEngine_shadow_t shadow;
    flecsEngine_lighting_t lighting;
    flecsEngine_materials_t materials;
    flecsEngine_textures_t textures;
    flecsEngine_pipelines_t pipelines;

    flecsEngine_default_attr_cache_t *default_attr_cache;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

#endif
