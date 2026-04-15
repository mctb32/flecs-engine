#define FLECS_ENGINE_TIME_OF_DAY_IMPL
#include "time_of_day.h"

#include <math.h>
#include "../../tracy_hooks.h"

static float flecsEngine_smoothstep01(float t) {
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* Direct-beam atmospheric transmittance at the given sun altitude,
 * normalized so the zenith sun returns 1.0. This is the physical
 * directional-sun attenuation and matches measured direct-normal
 * irradiance from clear-sky reference tables (ASHRAE / NREL):
 *
 *   altitude 45° → 0.96   altitude 30° → 0.90   altitude 15° → 0.75
 *   altitude  5° → 0.35   altitude  2° → 0.12   altitude  1° → 0.074
 *   altitude  0° → 0.025
 *
 * Model:
 *   - Kasten-Young relative air mass (1989): accounts for the increasing
 *     atmospheric path length as the sun approaches the horizon,
 *     including the near-horizon geometric correction that prevents the
 *     pure 1/sin(alt) formulation from blowing up.
 *   - Beer-Lambert extinction with τ = 0.1, the broadband optical depth
 *     of a clear atmosphere (≈ Linke turbidity 2.5, i.e. a typical clear
 *     day, not a hazy one). τ is applied to (AM - 1) so the zenith case
 *     comes out at 1.0 and `sun_intensity` on the component represents
 *     the noon brightness you want.
 *
 * Below the horizon the direct beam is geometrically zero — any warmth
 * you still see in the world at twilight comes from the atmospheric IBL
 * and scattered sky luminance, not from this light source. */
static float flecsEngine_sunTransmittance(float altitude_rad) {
    if (altitude_rad <= 0.0f) {
        return 0.0f;
    }

    float altitude_deg = altitude_rad * (180.0f / GLM_PIf);
    float s = sinf(altitude_rad);
    float ky = 0.50572f *
        powf(fmaxf(altitude_deg + 6.07995f, 0.01f), -1.6364f);
    float air_mass = 1.0f / fmaxf(s + ky, 1e-4f);

    float trans = expf(-0.1f * (air_mass - 1.0f));
    if (trans > 1.0f) trans = 1.0f;
    if (trans < 0.0f) trans = 0.0f;
    return trans;
}

/* Tint the direct sun by altitude to approximate Rayleigh extinction of
 * short wavelengths when the beam travels through a lot of atmosphere.
 * Endpoints are hand-picked to look right (not spectral), but they sit in
 * the plausible range:
 *   high sun (>~20°): ~5500 K, slightly warm white
 *   horizon         : ~2000-2500 K, deep orange */
static void flecsEngine_sunColor(float altitude_rad, float *r, float *g, float *b) {
    float altitude_deg = altitude_rad * (180.0f / GLM_PIf);
    float t = flecsEngine_smoothstep01((altitude_deg - 2.0f) / 18.0f);

    const float low[3]  = { 1.00f, 0.55f, 0.28f };
    const float high[3] = { 1.00f, 0.97f, 0.92f };

    *r = low[0] * (1.0f - t) + high[0] * t;
    *g = low[1] * (1.0f - t) + high[1] * t;
    *b = low[2] * (1.0f - t) + high[2] * t;
}

static float flecsEngine_wrap24(float h) {
    while (h >= 24.0f) h -= 24.0f;
    while (h < 0.0f) h += 24.0f;
    return h;
}

/* Returns sun altitude and azimuth in radians.
 * Altitude: angle above horizon (negative = below).
 * Azimuth: measured clockwise from north (0 = north, π/2 = east). */
static void flecsEngine_solarPosition(
    float hour,
    float day_of_year,
    float latitude_deg,
    float *out_altitude,
    float *out_azimuth)
{
    float day = day_of_year < 1.0f ? 1.0f : day_of_year;

    float decl_rad = glm_rad(23.44f) *
        sinf((GLM_PIf * 2.0f / 365.0f) * (day - 81.0f));
    float hour_rad = glm_rad(15.0f * (hour - 12.0f));
    float lat_rad = glm_rad(latitude_deg);

    float sin_lat = sinf(lat_rad);
    float cos_lat = cosf(lat_rad);
    float sin_decl = sinf(decl_rad);
    float cos_decl = cosf(decl_rad);

    float sin_alt = sin_lat * sin_decl + cos_lat * cos_decl * cosf(hour_rad);
    if (sin_alt > 1.0f) sin_alt = 1.0f;
    if (sin_alt < -1.0f) sin_alt = -1.0f;
    float altitude = asinf(sin_alt);
    float cos_alt = cosf(altitude);

    float azimuth;
    if (cos_alt > 1e-4f && cos_lat > 1e-4f) {
        float sin_az = -cos_decl * sinf(hour_rad) / cos_alt;
        float cos_az = (sin_decl - sin_lat * sin_alt) / (cos_lat * cos_alt);
        azimuth = atan2f(sin_az, cos_az);
    } else {
        azimuth = 0.0f;
    }

    *out_altitude = altitude;
    *out_azimuth = azimuth;
}

static void FlecsAdvanceTimeOfDay(ecs_iter_t *it) {
    FLECS_TRACY_ZONE_BEGIN("AdvanceTimeOfDay");
    FlecsTimeOfDay *tod = ecs_field(it, FlecsTimeOfDay, 0);
    float dt = it->delta_time;
    ecs_world_t *world = it->world;

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsTimeOfDay *t = &tod[i];

        if (t->time_scale != 0.0f) {
            t->hour = flecsEngine_wrap24(t->hour + t->time_scale * dt);
        }

        float altitude, azimuth;
        flecsEngine_solarPosition(
            t->hour, t->day_of_year, t->latitude, &altitude, &azimuth);

        /* Sun altitude above horizon → pitch angle of the light ray (which
         * points from the sun toward the scene, so pitch = -altitude).
         * Azimuth is the compass bearing of the sun; the ray travels in the
         * opposite horizontal direction (+π). north_offset lets the scene
         * pick which world-yaw points north. */
        float north_rad = glm_rad(t->north_offset);
        float pitch = -altitude;
        float yaw = azimuth + north_rad + GLM_PIf;

        float transmittance = flecsEngine_sunTransmittance(altitude);

        if (t->light) {
            FlecsRotation3 *rot = ecs_get_mut(
                world, t->light, FlecsRotation3);
            if (rot) {
                rot->x = pitch;
                rot->y = yaw;
                rot->z = 0.0f;
                ecs_modified(world, t->light, FlecsRotation3);
            }

            FlecsDirectionalLight *dl = ecs_get_mut(
                world, t->light, FlecsDirectionalLight);
            if (dl) {
                dl->intensity = t->sun_intensity * transmittance;
                ecs_modified(world, t->light, FlecsDirectionalLight);
            }

            /* Write warm/cool tint via FlecsRgba. The renderer multiplies
             * this into light_color each frame, so updating it drives the
             * sunrise/sunset color shift of the direct beam. */
            FlecsRgba *rgba = ecs_get_mut(world, t->light, FlecsRgba);
            if (rgba) {
                float r, g, b;
                flecsEngine_sunColor(altitude, &r, &g, &b);
                rgba->r = (uint8_t)(glm_clamp(r, 0.0f, 1.0f) * 255.0f);
                rgba->g = (uint8_t)(glm_clamp(g, 0.0f, 1.0f) * 255.0f);
                rgba->b = (uint8_t)(glm_clamp(b, 0.0f, 1.0f) * 255.0f);
                rgba->a = 255;
                ecs_modified(world, t->light, FlecsRgba);
            }
        }

        /* The atmosphere shader already models sun-angle-dependent
         * extinction and scattering, so the sun_intensity on
         * FlecsAtmosphere stays at its configured value and is not
         * modulated here. The atmosphere field on the component is kept
         * so users can wire the scene up front without needing a separate
         * reference. */
        (void)t->atmosphere;
    }
    FLECS_TRACY_ZONE_END;
}

void FlecsEngineTime_of_dayImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTime_of_day);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsTimeOfDay);

    /* Runs in PreStore so it wins over Transform3's RotationFromLookAt
     * (PostUpdate): any LookAt left on the driven light is recomputed
     * first, then time_of_day rewrites Rotation3 from the solar position. */
    ECS_SYSTEM(world, FlecsAdvanceTimeOfDay, EcsPreStore, [inout] TimeOfDay);
}
