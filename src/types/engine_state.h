#ifndef FLECS_ENGINE_TYPES_ENGINE_STATE_H
#define FLECS_ENGINE_TYPES_ENGINE_STATE_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "gpu_types.h"

#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

struct FlecsEngineSurfaceInterface;
typedef struct FlecsRenderViewImpl FlecsRenderViewImpl;

/* Scene-shared shadow resources. Per-view shadow state lives on
 * FlecsRenderViewImpl (shadow texture, cascade VPs, pass bind groups). */
typedef struct {
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout pass_bind_layout;
    WGPUSampler sampler;
} flecs_engine_shadow_t;

typedef struct {
    FlecsGpuLight *cpu_lights;
    int32_t light_count;
    int32_t light_capacity;
    WGPUBuffer light_buffer;

    ecs_query_t *point_light_query;
    ecs_query_t *spot_light_query;
} flecs_engine_lighting_t;

#define FLECS_ENGINE_TEXTURE_BUCKET_COUNT 3

typedef struct {
    WGPUTexture texture_arrays[4];       /* albedo, emissive, roughness, normal */
    WGPUTextureView texture_array_views[4];
    /* Per-channel layer count. A channel with 0 layers is not allocated;
     * its binding slot is plugged with a 1x1 fallback view. */
    uint32_t layer_counts[4];
    uint32_t mip_count;
    uint32_t width;
    uint32_t height;
    bool is_bc7;
} flecs_engine_texture_bucket_t;

typedef struct {
    WGPUBuffer buffer;
    FlecsGpuMaterial *cpu_materials;
    uint32_t buffer_capacity;
    uint32_t count;
    ecs_query_t *query;

    WGPUBindGroupLayout pbr_texture_bind_layout;
    WGPUSampler pbr_sampler;
    WGPUTexture fallback_white_tex;
    WGPUTextureView fallback_white_view;
    WGPUTextureView fallback_white_2d_view;  /* 2D view for transmission fallback */
    WGPUTexture fallback_normal_tex;
    WGPUTextureView fallback_normal_view;
    uint32_t next_id;

    uint32_t dirty_version;
    uint32_t uploaded_version;

    ecs_query_t *texture_query;

    /* Per-bucket texture arrays. The shader picks a bucket per fragment
     * via the `texture_bucket` field on FlecsGpuMaterial. */
    flecs_engine_texture_bucket_t buckets[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    WGPUBindGroup texture_array_bind_group;

    WGPURenderPipeline blit_pipeline;
    WGPUBindGroupLayout blit_bind_layout;
    WGPUSampler blit_sampler;
    WGPUComputePipeline mipgen_pipeline;
    WGPUBindGroupLayout mipgen_bind_layout;
} flecs_engine_materials_t;

typedef struct {
    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    WGPUTexture msaa_color_texture;
    WGPUTextureView msaa_color_texture_view;
    WGPUTexture msaa_depth_texture;
    WGPUTextureView msaa_depth_texture_view;
    uint32_t msaa_texture_width;
    uint32_t msaa_texture_height;
    int32_t msaa_texture_sample_count;
    WGPUTextureFormat msaa_color_format;

    /* Passthrough effect for blitting batch output to screen when no
     * effects are enabled. Built directly during renderer init. */
    WGPURenderPipeline passthrough_pipeline;
    WGPUBindGroupLayout passthrough_bind_layout;
    WGPUSampler passthrough_sampler;

    /* Depth resolve pass: resolves MSAA depth into the 1-sample depth
     * texture so that post-process effects (SSAO, fog, …) can read it. */
    WGPURenderPipeline depth_resolve_pipeline;
    WGPUBindGroupLayout depth_resolve_bind_layout;
} flecs_engine_depth_t;

typedef struct {
    GLFWwindow *window;
    int32_t width;
    int32_t height;
    int32_t actual_width;
    int32_t actual_height;
    int32_t resolution_scale;
    int32_t sample_count; /* 1 or 4 (derived from msaa bool) */
    bool vsync;
    const struct FlecsEngineSurfaceInterface *surface_impl;
    bool output_done;
    const char *frame_output_path;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTexture frame_output_texture;
    WGPUTextureView frame_output_texture_view;

    WGPUSurfaceConfiguration surface_config;
    WGPUTextureFormat hdr_color_format;

    ecs_entity_t sky_background_hdri;
    ecs_entity_t black_hdri;
    WGPUBindGroupLayout ibl_shadow_bind_layout;
    WGPUBindGroupLayout empty_bind_layout;
    WGPUBindGroup empty_bind_group;
    WGPUBindGroupLayout material_bind_layout;
    WGPUBindGroup scene_material_bind_group;
    uint32_t scene_material_bind_version;
    uint32_t scene_bind_version;

    ecs_query_t *view_query;

    /* Shared pipeline/layout/sampler for the gaussian blur pyramid used to
     * produce the opaque snapshot. The snapshot texture itself is per-view
     * (FlecsRenderViewImpl::opaque_snapshot). */
    WGPURenderPipeline opaque_snapshot_downsample_pipeline;
    WGPUBindGroupLayout opaque_snapshot_downsample_layout;
    WGPUSampler opaque_snapshot_sampler;

    flecs_engine_shadow_t shadow;     /* scene-shared shadow resources */
    flecs_engine_lighting_t lighting; /* scene-wide lights (cluster is per-view) */
    flecs_engine_materials_t materials;
    flecs_engine_depth_t depth;

    FlecsDefaultAttrCache *default_attr_cache;

    /* Currently active view (set during extract/render of each view). Provides
     * batch callbacks with access to per-view state without changing their
     * signatures. */
    FlecsRenderViewImpl *current_view_impl;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

#endif
