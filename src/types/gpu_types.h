#ifndef FLECS_ENGINE_TYPES_GPU_TYPES_H
#define FLECS_ENGINE_TYPES_GPU_TYPES_H

#include "flecs_engine.h"
#include "../vendor.h"

typedef struct {
    flecs_rgba_t color;
    float metallic;
    float roughness;
    float emissive_strength;
    flecs_rgba_t emissive_color;
    uint32_t texture_bucket;
    uint32_t layer_albedo;
    uint32_t layer_emissive;
    uint32_t layer_mr;
    uint32_t layer_normal;
    /* Transmission (KHR_materials_transmission + volume + ior) */
    float transmission_factor;
    float ior;
    float thickness_factor;
    float attenuation_distance;
    uint32_t attenuation_color;  /* packed RGBA8 */
    /* UV transform (KHR_texture_transform) */
    float uv_scale_x;
    float uv_scale_y;
    float uv_offset_x;
    float uv_offset_y;
} FlecsGpuMaterial;

typedef struct {
    FlecsRgba *color;
    ecs_size_t color_count;
    FlecsPbrMaterial *material;
    ecs_size_t material_count;
    FlecsEmissive *emissive;
    ecs_size_t emissive_count;

    /* Identity MaterialId GPU buffer: [0, 1, 2, ..., N-1]. Used as the
     * MaterialId VBO for owns-material-data batches, where each instance
     * indexes its own private material storage buffer at its instance
     * position. */
    WGPUBuffer material_id_identity_buffer;
    int32_t material_id_identity_capacity;
} FlecsDefaultAttrCache;

#endif
