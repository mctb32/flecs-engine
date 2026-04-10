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
    /* Per-channel layer indices into the bucket's channel arrays.
     * Layer 0 of each (bucket, channel) is reserved as a "neutral slot"
     * pre-filled with the channel's fallback colour. Materials without a
     * texture for a given channel leave the layer at 0, so the shader
     * samples the neutral fill there. Contributors get 1..N. */
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
} FlecsGpuMaterial;

typedef struct {
    FlecsRgba *color;
    ecs_size_t color_count;
    FlecsPbrMaterial *material;
    ecs_size_t material_count;
    FlecsEmissive *emissive;
    ecs_size_t emissive_count;
} FlecsDefaultAttrCache;

#endif
