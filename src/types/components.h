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
    WGPUBuffer vertex_uv_buffer; /* vec<FlecsLitVertexUv>, always built
                                  * (zero UVs/tangents if the source had none) */
    WGPUBuffer index_buffer;     /* vec<uint32_t> */
    int32_t vertex_count;
    int32_t index_count;
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
    bool uses_material_buffer;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUTexture *effect_target_textures;
    WGPUTextureView *effect_target_views;
    int32_t effect_target_count;
    uint32_t effect_target_width;
    uint32_t effect_target_height;
    WGPUTextureFormat effect_target_format;
    /* When view.atmosphere is set we need a pre-compose scene texture; batch
     * rendering writes here and atmosphere compose reads it. When atmosphere
     * is not set this is NULL and the scene writes directly into
     * effect_target_views[0]. */
    WGPUTexture scene_target_texture;
    WGPUTextureView scene_target_view;
    WGPUBindGroup passthrough_bind_group;
} FlecsRenderViewImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

typedef struct {
    WGPURenderPipeline pipeline_hdr;
    WGPURenderPipeline pipeline_shadow;
    bool uses_ibl;
    bool uses_shadow;
    bool uses_cluster;
    bool uses_textures;
    bool uses_material_buffer;
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
    WGPUTexture fallback_texture;
    WGPUTextureView fallback_view;
} FlecsHeightFogImpl;

extern ECS_COMPONENT_DECLARE(FlecsHeightFogImpl);

typedef struct {
    WGPUBuffer uniform_buffer;
} FlecsSunShaftsImpl;

extern ECS_COMPONENT_DECLARE(FlecsSunShaftsImpl);

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

typedef struct {
    WGPUBuffer uniform_buffer;

    /* Transmittance LUT (256x64, RGBA16F). */
    WGPUTexture trans_texture;
    WGPUTextureView trans_view;

    /* Multi-scattering LUT (32x32, RGBA16F). */
    WGPUTexture ms_texture;
    WGPUTextureView ms_view;

    /* Sky-view LUT (192x108, RGBA16F). */
    WGPUTexture skyview_texture;
    WGPUTextureView skyview_view;

    /* Aerial perspective LUT (32x32x32 as 2D array RGBA16F, populated by a
     * single compute dispatch per frame; manual lerp along the depth axis
     * in the compose shader). */
    WGPUTexture aerial_texture;
    WGPUTextureView aerial_view;

    WGPUSampler clamp_sampler;

    WGPUBindGroupLayout trans_layout;
    WGPUBindGroupLayout ms_layout;
    WGPUBindGroupLayout skyview_layout;
    WGPUBindGroupLayout compose_layout;

    WGPURenderPipeline trans_pipeline;
    WGPURenderPipeline ms_pipeline;
    WGPURenderPipeline skyview_pipeline;
    WGPURenderPipeline compose_pipeline_hdr;
    WGPURenderPipeline compose_pipeline_surface;

    /* Atmosphere IBL cubemap: populated each frame by a compute shader that
     * samples the sky-view LUT per cube face (mip 0), then a series of 2x
     * box-downsample compute dispatches generates the rest of the mip chain.
     * GGX-free because atmosphere content has no high-frequency detail. The
     * same texture is bound to both the prefilter slot (full mip chain) and
     * the irradiance slot (smallest mip only). */
    WGPUBindGroupLayout cube_face_layout;      /* compute: sky -> cube mip 0 */
    WGPUComputePipeline cube_face_pipeline;
    WGPUBindGroupLayout cube_ds_layout;        /* compute: mip[k-1] -> mip[k] */
    WGPUComputePipeline cube_ds_pipeline;
    WGPUBindGroup cube_face_bind_group;        /* single cached bind group */
    WGPUBindGroup cube_ds_bind_groups[8];      /* per-mip (mip >= 1) */
    /* Per-mip storage-write 2D-array views into the cubemap (one per mip,
     * each covering all 6 faces). Index m writes mip level m. */
    WGPUTextureView cube_mip_storage_views[8];
    /* Per-mip 2D-array read views (for sampling the previous mip during
     * downsample). Index m reads mip level m. */
    WGPUTextureView cube_mip_read_views[8];
    uint32_t cube_mip_count;

    /* Cached bind groups for the LUT fragment passes. */
    WGPUBindGroup trans_bind_group;
    WGPUBindGroup ms_bind_group;
    WGPUBindGroup skyview_bind_group;

    /* Aerial LUT compute pipeline — one dispatch writes all 32x32x32 voxels. */
    WGPUBindGroupLayout aerial_compute_layout;
    WGPUComputePipeline aerial_compute_pipeline;
    WGPUBindGroup aerial_compute_bind_group;
    WGPUTextureView aerial_storage_view;        /* storage-write view */
} FlecsAtmosphereImpl;

extern ECS_COMPONENT_DECLARE(FlecsAtmosphereImpl);

#endif
