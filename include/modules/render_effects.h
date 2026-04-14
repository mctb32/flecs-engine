#ifndef FLECS_ENGINE_RENDER_EFFECTS_H
#define FLECS_ENGINE_RENDER_EFFECTS_H

typedef struct {
    float threshold;
    float threshold_softness;
} FlecsBloomPrefilter;

typedef struct {
    float intensity;
    float low_frequency_boost;
    float low_frequency_boost_curvature;
    float high_pass_frequency;
    FlecsBloomPrefilter prefilter;
    uint32_t mip_count;
    float scale_x;
    float scale_y;
} FlecsBloom;

extern ECS_COMPONENT_DECLARE(FlecsBloom);

ECS_STRUCT(FlecsHeightFog, {
    float density;
    float falloff;
    float base_height;
    float max_opacity;
    flecs_rgba_t color;
});

extern ECS_COMPONENT_DECLARE(FlecsHeightFog);

ECS_STRUCT(FlecsSSAO, {
    float radius;
    float bias;
    float intensity;
    int32_t blur;
});

extern ECS_COMPONENT_DECLARE(FlecsSSAO);

/* Physically-based sky & aerial-perspective effect (Hillaire 2020).
 * Builds 4 LUTs per frame (transmittance / multi-scattering / sky-view /
 * aerial perspective) and composites them with the scene in a final pass. */
ECS_STRUCT(FlecsAtmosphere, {
    float sun_intensity;              /* multiplies the directional light intensity */
    float sun_disk_intensity;         /* 0 disables the disk */
    float sun_disk_angular_radius;    /* radians; real sun ~0.00465 (~0.27 deg) */
    float aerial_perspective_distance_km; /* depth range stored in the 3D LUT;
                                           * set close to your scene's max depth */
    float aerial_perspective_intensity;   /* scalar multiplier on aerial haze;
                                           * 1 = physical, higher = artistic push */

    float sea_level_y;                /* world-y of sea level */
    float world_units_per_km;         /* engine unit scale; 1000 = 1 unit is 1 m */
    float ground_altitude_km;         /* altitude at sea_level_y */

    float planet_radius_km;           /* Earth: 6360 */
    float atmosphere_thickness_km;    /* Earth: 100 */
    float rayleigh_scale_height_km;   /* Earth: 8 */
    float mie_scale_height_km;        /* Earth: 1.2 */
    float mie_anisotropy;             /* Henyey-Greenstein g; Earth: 0.8 */

    flecs_rgba_t ground_albedo;       /* for multi-scattering */
});

extern ECS_COMPONENT_DECLARE(FlecsAtmosphere);

ecs_entity_t flecsEngine_createEffect_tonyMcMapFace(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

ecs_entity_t flecsEngine_createEffect_invert(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

FlecsBloom flecsEngine_bloomSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_bloom(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsBloom *settings);

FlecsHeightFog flecsEngine_heightFogSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_heightFog(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsHeightFog *settings);

FlecsSSAO flecsEngine_ssaoSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_ssao(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsSSAO *settings);

ecs_entity_t flecsEngine_createEffect_gammaCorrect(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

FlecsAtmosphere flecsEngine_atmosphereSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_atmosphere(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsAtmosphere *settings);

#endif
