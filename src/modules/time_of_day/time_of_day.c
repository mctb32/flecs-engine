#define FLECS_ENGINE_TIME_OF_DAY_IMPL
#include "time_of_day.h"

#include <math.h>
#include "../../tracy_hooks.h"

static float flecsEngine_smoothstep01(float t) {
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

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

        float north_rad = glm_rad(t->north_offset);
        float pitch = -altitude;
        float yaw = azimuth + north_rad + GLM_PIf;

        float atm_trans_rgb[3] = { 0.0f, 0.0f, 0.0f };
        const FlecsAtmosphere *atm_settings = NULL;

        if (t->atmosphere) {
            atm_settings = ecs_get(
                world, t->atmosphere, FlecsAtmosphere);
            if (atm_settings) {
                flecsEngine_atmos_sunTransmittance(
                    atm_settings, sinf(altitude), atm_trans_rgb);
            }
        }

        if (t->light) {
            FlecsRotation3 *rot = ecs_get_mut(
                world, t->light, FlecsRotation3);
            if (rot) {
                rot->x = pitch;
                rot->y = yaw;
                rot->z = 0.0f;
                ecs_modified(world, t->light, FlecsRotation3);
            }

            float light_intensity;
            float light_r, light_g, light_b;
            if (atm_settings) {
                float m = atm_trans_rgb[0];
                if (atm_trans_rgb[1] > m) m = atm_trans_rgb[1];
                if (atm_trans_rgb[2] > m) m = atm_trans_rgb[2];
                light_intensity = t->sun_intensity * m;
                if (m > 1e-6f) {
                    light_r = atm_trans_rgb[0] / m;
                    light_g = atm_trans_rgb[1] / m;
                    light_b = atm_trans_rgb[2] / m;
                } else {
                    light_r = light_g = light_b = 0.0f;
                }
            } else {
                float transmittance = flecsEngine_sunTransmittance(altitude);
                light_intensity = t->sun_intensity * transmittance;
                flecsEngine_sunColor(altitude, &light_r, &light_g, &light_b);
            }

            FlecsDirectionalLight *dl = ecs_get_mut(
                world, t->light, FlecsDirectionalLight);
            if (dl) {
                dl->intensity = light_intensity;
                ecs_modified(world, t->light, FlecsDirectionalLight);
            }

            FlecsRgba *rgba = ecs_get_mut(world, t->light, FlecsRgba);
            if (rgba) {
                rgba->r = (uint8_t)(glm_clamp(light_r, 0.0f, 1.0f) * 255.0f);
                rgba->g = (uint8_t)(glm_clamp(light_g, 0.0f, 1.0f) * 255.0f);
                rgba->b = (uint8_t)(glm_clamp(light_b, 0.0f, 1.0f) * 255.0f);
                rgba->a = 255;
                ecs_modified(world, t->light, FlecsRgba);
            }
        }
    }
}

void FlecsEngineTime_of_dayImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTime_of_day);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsTimeOfDay);
    ECS_SYSTEM(world, FlecsAdvanceTimeOfDay, EcsPreStore, [inout] TimeOfDay);
}
