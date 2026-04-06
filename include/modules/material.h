#ifndef FLECS_ENGINE_MATERIAL_H
#define FLECS_ENGINE_MATERIAL_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MATERIAL_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_rgba_t FlecsRgba;

extern ECS_COMPONENT_DECLARE(FlecsRgba);

ECS_STRUCT(FlecsPbrMaterial, {
    float metallic;
    float roughness;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);

ECS_STRUCT(FlecsEmissive, {
    float strength;
    flecs_rgba_t color;
});

typedef FlecsEmissive EcsEmissive;

extern ECS_COMPONENT_DECLARE(FlecsEmissive);

ECS_STRUCT(FlecsMaterialId, {
    uint32_t value;
});

extern ECS_COMPONENT_DECLARE(FlecsMaterialId);

ECS_STRUCT(FlecsTexture, {
    const char *path;
});

extern ECS_COMPONENT_DECLARE(FlecsTexture);

ECS_STRUCT(FlecsPbrTextures, {
    ecs_entity_t albedo;
    ecs_entity_t emissive;
    ecs_entity_t roughness;
    ecs_entity_t normal;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrTextures);

extern ECS_TAG_DECLARE(FlecsAlphaBlend);

#endif
