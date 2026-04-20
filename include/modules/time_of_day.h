#ifndef FLECS_ENGINE_TIME_OF_DAY_H
#define FLECS_ENGINE_TIME_OF_DAY_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TIME_OF_DAY_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Drives a directional light (and optionally an atmosphere entity) from a
 * simulated time of day, day of year, and geographic latitude. The sun's
 * orientation is computed with a standard NOAA-style solar position
 * approximation and written to the light's Rotation3 each frame. If the
 * light has a FlecsDirectionalLight its intensity is scaled by the sun's
 * altitude (0 below horizon, max at zenith). */
ECS_STRUCT(FlecsTimeOfDay, {
    ecs_entity_t light;       /* directional light to drive (required) */
    ecs_entity_t moon_light;  /* directional light for the moon (optional) */
    ecs_entity_t atmosphere;  /* atmosphere entity to drive (optional) */

    float hour;               /* local solar time, 0..24 */
    float day_of_year;        /* 1..365 (82 ≈ equinox) */
    float latitude;           /* degrees, -90..90 (positive = north) */
    float north_offset;       /* degrees; world-yaw that points north.
                               * Rotates the compass so the sun rises from
                               * the right direction for your scene. */
    float time_scale;         /* simulated hours per real second; 0 = paused */

    float sun_intensity;      /* directional-light intensity at solar noon.
                               * The system multiplies this by an atmospheric
                               * transmittance factor (Kasten-Young air mass
                               * + Beer-Lambert) so full daylight sun is
                               * kept nearly at baseline until the sun drops
                               * toward the horizon. */

    float moon_intensity;     /* artistic multiplier on top of the physically
                               * derived moonlight (sun_intensity * tiny
                               * lunar/solar ratio * illuminated fraction).
                               * Defaults to 1.0; raise to brighten night
                               * scenes. */

    float moon_position_offset; /* hours added to the moon's hour angle. Shifts
                                 * the moon's path across the sky without
                                 * affecting the sun. */
});

#endif
