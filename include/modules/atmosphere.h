#ifndef FLECS_ENGINE_ATMOSPHERE_H
#define FLECS_ENGINE_ATMOSPHERE_H

typedef struct {
    float scale_height_km;
    float scattering_scale;
} FlecsAtmosphereRayleigh;

typedef struct {
    float scale_height_km;
    float anisotropy;
    float scattering_scale;
    flecs_rgba_t tint;
} FlecsAtmosphereMie;

ECS_STRUCT(FlecsAtmosphere, {
    float sun_intensity;
    float sun_disk_intensity;
    float sun_disk_angular_radius;
    float aerial_perspective_distance_km;

    float aerial_perspective_intensity;

    float sea_level_y;
    float world_units_per_km;
    float ground_altitude_km;

    float planet_radius_km;
    float atmosphere_thickness_km;

    FlecsAtmosphereRayleigh rayleigh;
    FlecsAtmosphereMie mie;

    float ozone_scale;
    float stratospheric_aerosol_scale;

    flecs_rgba_t ground_albedo;
});

extern ECS_COMPONENT_DECLARE(FlecsAtmosphere);

FlecsAtmosphere flecsEngine_atmosphereSettingsDefault(void);

void flecsEngine_atmos_sunTransmittance(
    const FlecsAtmosphere *atm,
    float sun_cos_zenith,
    float out_rgb[3]);

void FlecsEngineAtmosphereImport(
    ecs_world_t *world);

#endif
