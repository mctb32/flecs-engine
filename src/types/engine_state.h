#ifndef FLECS_ENGINE_TYPES_ENGINE_STATE_H
#define FLECS_ENGINE_TYPES_ENGINE_STATE_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "gpu_types.h"

#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

struct FlecsEngineSurfaceInterface;

typedef struct {
    WGPUTexture texture;
    WGPUTextureView texture_view;
    WGPUTextureView layer_views[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    uint32_t map_size;
    WGPUShaderModule shader_module;
    WGPUBuffer vp_buffers[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    WGPUBindGroupLayout pass_bind_layout;
    WGPUBindGroup pass_bind_groups[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int current_cascade;
    WGPUSampler sampler;
    mat4 current_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float cascade_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    bool in_pass;
} flecs_engine_shadow_t;

typedef struct {
    FlecsGpuLight *cpu_lights;
    int32_t light_count;
    int32_t light_capacity;
    WGPUBuffer light_buffer;

    uint32_t *cpu_cluster_indices;
    int32_t cluster_index_capacity;
    WGPUBuffer cluster_info_buffer;
    WGPUBuffer cluster_grid_buffer;
    WGPUBuffer cluster_index_buffer;
    bool cluster_bind_group_dirty;

    ecs_query_t *point_light_query;
    ecs_query_t *spot_light_query;
} flecs_engine_lighting_t;

/* PBR texture buckets: textures are normalized into one of 3 size buckets
 * (512², 1024², 2048²) so the shader can pick the right binding via a
 * per-material `texture_bucket` field. Each bucket holds 4 channel arrays
 * (albedo, emissive, roughness, normal), all at the bucket's dimensions. */
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
    WGPUTexture fallback_black_tex;
    WGPUTextureView fallback_black_view;
    WGPUTexture fallback_normal_tex;
    WGPUTextureView fallback_normal_view;
    uint32_t next_id;
    uint32_t last_id;

    /* Bumped by OnSet observers on material components (FlecsRgba,
     * FlecsPbrMaterial, FlecsEmissive, FlecsTransmission) and on new
     * material entity creation. The upload path re-uploads whenever
     * `dirty_version != uploaded_version`. This lets in-place material
     * mutations propagate to the GPU without needing ecs_query_changed. */
    uint32_t dirty_version;
    uint32_t uploaded_version;

    ecs_query_t *texture_query;

    /* Per-bucket texture arrays. The shader picks a bucket per fragment
     * via the `texture_bucket` field on FlecsGpuMaterial. */
    flecs_engine_texture_bucket_t buckets[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    WGPUBindGroup texture_array_bind_group;

    /* Cached pipelines for the texture-array build pass. The blit
     * pipeline normalizes arbitrary source textures to one slice of a
     * bucket array; the mip-gen compute pipeline downsamples mip N to
     * mip N+1 across all slices in one dispatch. */
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
    flecs_engine_background_t sky_bg_colors;
    WGPUBindGroupLayout ibl_shadow_bind_layout;
    WGPUBindGroupLayout empty_bind_layout;
    WGPUBindGroup empty_bind_group;
    uint32_t scene_bind_version;

    /* Per-frame / per-view shader uniforms. Single global buffer bound at
     * group(0) binding(0), written by render_view before each view's
     * batches are encoded. */
    WGPUBuffer frame_uniform_buffer;

    ecs_query_t *view_query;
    WGPURenderPipeline last_pipeline;
    float camera_pos[3];

    /* Opaque scene snapshot for transmission rendering. Captured after
     * opaques render, sampled by transmissive objects at @group(0) @binding(3).
     *
     * Two-texture pipeline:
     * 1. `opaque_snapshot_source` holds a gaussian-like pyramid built with a
     *    Jimenez 13-tap downsample: mip 0 is the raw opaque color target,
     *    mips 1..N are successive blurs. Used as the sample source for the
     *    GGX prefilter pass below.
     * 2. `opaque_snapshot` holds the GGX-prefiltered pyramid: mip 0 is the
     *    raw color target (copied from source mip 0 unchanged), mips 1..N
     *    are each pre-integrated with a GGX lobe at a specific roughness
     *    (alpha = i / (N-1)), sampled from the source pyramid with
     *    Hammersley-importance-sampled taps.
     *
     * The transmission shader samples `opaque_snapshot` at
     * LOD = roughness * (mip_count - 1) and relies on the per-mip GGX
     * pre-integration for correct rough refraction. */
    WGPUTexture opaque_snapshot;
    WGPUTextureView opaque_snapshot_view;
    WGPUTextureView *opaque_snapshot_mip_views; /* per-mip render attachments */
    uint32_t opaque_snapshot_mip_count;
    uint32_t opaque_snapshot_width;
    uint32_t opaque_snapshot_height;

    /* Source pyramid for the GGX prefilter (Jimenez gaussian pyramid). */
    WGPUTexture opaque_snapshot_source;
    WGPUTextureView opaque_snapshot_source_view; /* full mip chain as SRV */
    WGPUTextureView *opaque_snapshot_source_mip_views; /* per-mip RT */

    /* Downsample (Jimenez) — builds the source pyramid. */
    WGPURenderPipeline opaque_snapshot_downsample_pipeline;
    WGPUBindGroupLayout opaque_snapshot_downsample_layout;
    WGPUSampler opaque_snapshot_sampler;

    /* GGX prefilter — writes each mip of opaque_snapshot. */
    WGPURenderPipeline opaque_snapshot_prefilter_pipeline;
    WGPUBindGroupLayout opaque_snapshot_prefilter_layout;
    WGPUBuffer opaque_snapshot_prefilter_uniforms; /* N * 256 bytes */
    WGPUBindGroup opaque_snapshot_prefilter_bind_group; /* rebuilt on resize */

    flecs_engine_shadow_t shadow;
    flecs_engine_lighting_t lighting;
    flecs_engine_materials_t materials;
    flecs_engine_depth_t depth;

    FlecsDefaultAttrCache *default_attr_cache;

    /* Frustum culling state (computed once per frame during extract) */
    float frustum_planes[6][4];
    float shadow_frustum_planes[6][4];
    bool frustum_valid;
    bool shadow_frustum_valid;

    /* Per-cascade light frustum planes (computed during extract from
     * cascade light VP matrices for per-cascade shadow culling) */
    float cascade_frustum_planes[FLECS_ENGINE_SHADOW_CASCADE_COUNT][6][4];
    bool cascade_frustum_valid;

    /* Screen-size culling state (computed once per frame during extract).
     * Objects whose projected screen area is below the threshold are culled.
     * screen_cull_factor = (viewport_height / tan(fov/2))^2 */
    float screen_cull_factor;
    float screen_cull_threshold;
    bool screen_cull_valid;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

#endif
