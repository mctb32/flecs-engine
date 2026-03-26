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
