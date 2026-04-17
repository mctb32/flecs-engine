#ifndef FLECS_ENGINE_TYPES_COMPONENTS_H
#define FLECS_ENGINE_TYPES_COMPONENTS_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "engine_state.h"

struct FlecsSurfaceImpl {
    const struct FlecsEngineSurfaceInterface *interface;

    GLFWwindow *window;
    WGPUSurface wgpu_surface;
    WGPUSurfaceConfiguration surface_config;
    bool vsync;

    char *path;
    WGPUTexture offscreen_texture;
    WGPUTextureView offscreen_view;
    bool done;

    /* Previous sample count, used to detect MSAA changes and trigger batch
     * pipeline rebuilds when the surface's sample_count changes. */
    int32_t prev_sample_count;
};

extern ECS_COMPONENT_DECLARE(FlecsSurfaceImpl);

typedef struct {
    WGPUTexture ibl_equirect_texture;
    WGPUTextureView ibl_equirect_texture_view;
    WGPUTexture ibl_prefiltered_cubemap;
    WGPUTextureView ibl_prefiltered_cubemap_view;
    WGPUTexture ibl_irradiance_cubemap;
    WGPUTextureView ibl_irradiance_cubemap_view;
    WGPUSampler ibl_sampler;
    uint32_t ibl_prefilter_mip_count;
} FlecsHdriImpl;

extern ECS_COMPONENT_DECLARE(FlecsHdriImpl);

typedef struct {
    WGPUBuffer vertex_buffer;    /* vec<FlecsGpuVertex> — position only, used
                                  * by the shadow depth pass */
    WGPUBuffer vertex_uv_buffer; /* vec<FlecsGpuVertexLitUv>, always built
                                  * (zero UVs/tangents if the source had none) */
    WGPUBuffer index_buffer;     /* vec<uint32_t> */
    int32_t vertex_count;
    int32_t index_count;
    FlecsAABB aabb;
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
    bool uses_instance_buffer;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

/* Per-view shadow state. The shared shader_module/pass_bind_layout/sampler
 * live on FlecsEngineImpl::shadow. */
typedef struct {
    WGPUTexture texture;
    WGPUTextureView texture_view;
    WGPUTextureView layer_views[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    uint32_t map_size;
    WGPUBuffer vp_buffers[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    WGPUBindGroup pass_bind_groups[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int current_cascade;
    mat4 current_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float cascade_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
} flecs_view_shadow_t;

typedef struct {
    uint32_t *cpu_cluster_indices;
    int32_t cluster_index_capacity;
    WGPUBuffer cluster_info_buffer;
    WGPUBuffer cluster_grid_buffer;
    WGPUBuffer cluster_index_buffer;
} flecs_view_cluster_t;

typedef struct {
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUTextureView *mip_views;
    uint32_t mip_count;
    uint32_t width;
    uint32_t height;
} flecs_view_opaque_snapshot_t;

struct FlecsRenderViewImpl {
    /* Per-view depth target. Written by the main batch pass and read by
     * SSAO/fog/sunshafts/atmosphere compose. */
    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    /* Per-view MSAA color + depth targets. The main color is resolved into
     * effect_target_views[0]; the MSAA depth is resolved into depth_texture
     * via the depth_resolve pipeline. */
    WGPUTexture msaa_color_texture;
    WGPUTextureView msaa_color_texture_view;
    WGPUTexture msaa_depth_texture;
    WGPUTextureView msaa_depth_texture_view;
    uint32_t msaa_texture_width;
    uint32_t msaa_texture_height;
    int32_t msaa_texture_sample_count;
    WGPUTextureFormat msaa_color_format;

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

    /* Per-view frame uniform buffer (contains camera MVP, light VP,
     * cascade splits, camera position, etc.). */
    WGPUBuffer frame_uniform_buffer;

    /* Pipeline-state tracker reset at the start of each pass. */
    WGPURenderPipeline last_pipeline;

    /* World-space camera position, latched from the uniform write each frame
     * so extract/render code can use it without re-reading the camera. */
    float camera_pos[3];

    /* Frustum culling state, recomputed per frame during extract. */
    float frustum_planes[6][4];
    float shadow_frustum_planes[6][4];
    bool frustum_valid;
    bool shadow_frustum_valid;
    float cascade_frustum_planes[FLECS_ENGINE_SHADOW_CASCADE_COUNT][6][4];
    bool cascade_frustum_valid;

    /* Screen-size culling state. */
    float screen_cull_factor;
    float screen_cull_threshold;
    bool screen_cull_valid;

    flecs_view_shadow_t shadow;
    flecs_view_cluster_t cluster;
    flecs_view_opaque_snapshot_t opaque_snapshot;

    /* Group 0 (scene globals) bind group, rebuilt when any of its inputs
     * change. Keyed by HDRI so switching HDRIs rebuilds it. */
    WGPUBindGroup scene_bind_group;
    ecs_entity_t scene_bind_hdri;
    uint32_t scene_bind_version;
};
typedef struct FlecsRenderViewImpl FlecsRenderViewImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

typedef struct {
    WGPURenderPipeline pipeline_hdr;
    WGPURenderPipeline pipeline_shadow;
    bool uses_ibl;
    bool uses_shadow;
    bool uses_cluster;
    bool uses_textures;
    bool uses_material_buffer;
    bool uses_instance_buffer;
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
