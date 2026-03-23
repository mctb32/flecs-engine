#include <math.h>
#include <stdlib.h>

#include "../renderer.h"
#include "hdri_loader.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

bool flecsIblBuildDefaultImage(
    const FlecsEngineImpl *engine,
    FlecsHdriImage *image)
{
    /* Build a 1×N equirectangular fallback that mimics a simple outdoor
     * environment:
     *  - Sky gradient: deeper at zenith, lighter near the horizon
     *  - Ground gradient: configured color at horizon, darker at nadir
     *  - Horizon glow: subtle bright band at the sky/ground boundary */
    const int fallback_h = 1024;
    image->width = 1;
    image->height = fallback_h;
    image->pixels_rgba32f = malloc(sizeof(float) * 4u * (size_t)fallback_h);
    if (!image->pixels_rgba32f) {
        return false;
    }

    float sky_r = flecsEngine_colorChannelToFloat(engine->sky_color.r);
    float sky_g = flecsEngine_colorChannelToFloat(engine->sky_color.g);
    float sky_b = flecsEngine_colorChannelToFloat(engine->sky_color.b);
    float gnd_r = flecsEngine_colorChannelToFloat(engine->ground_color.r);
    float gnd_g = flecsEngine_colorChannelToFloat(engine->ground_color.g);
    float gnd_b = flecsEngine_colorChannelToFloat(engine->ground_color.b);

    /* Luminance of sky color, used to scale the horizon glow so it stays
     * proportional to the overall brightness of the scene. */
    float sky_lum = sky_r * 0.2126f + sky_g * 0.7152f + sky_b * 0.0722f;

    for (int row = 0; row < fallback_h; row++) {
        /* v goes from 0 (zenith) to 1 (nadir), 0.5 = horizon */
        float v = ((float)row + 0.5f) / (float)fallback_h;
        float *px = &image->pixels_rgba32f[row * 4];

        if (v < 0.5f) {
            /* Sky: lerp toward white near horizon for atmospheric haze.
             * haze_start: fraction of the sky half where haze begins
             *   0.0 = haze across entire sky, 1.0 = no haze at all
             *   0.7 = pure sky color in the top 70%, haze in bottom 30% */
            float haze_start = 0.7f;
            float t = v / 0.5f;
            float haze = (t > haze_start)
                ? (t - haze_start) / (1.0f - haze_start) : 0.0f;
            haze = haze * haze;           /* ease-in within the haze zone */
            float haze_strength = 0.35f;  /* how far toward white */
            float h = haze * haze_strength;
            px[0] = sky_r + (1.0f - sky_r) * h;
            px[1] = sky_g + (1.0f - sky_g) * h;
            px[2] = sky_b + (1.0f - sky_b) * h;
        } else {
            /* Ground: configured color at the horizon, fading darker
             * toward the nadir.  t=0 at horizon, t=1 at nadir. */
            float t = (v - 0.5f) / 0.5f;
            float darken = t * t;            /* ease-in: gradual near horizon */
            float darken_strength = 0.55f;   /* how much darker at nadir */
            float d = 1.0f - darken * darken_strength;
            px[0] = gnd_r * d;
            px[1] = gnd_g * d;
            px[2] = gnd_b * d;
        }

        /* Horizon glow: bright band at v≈0.5, gaussian-ish falloff */
        float dist = (v - 0.5f) * (float)fallback_h; /* pixels from horizon */
        float glow_width = 3.0f;
        float glow = expf(-(dist * dist) / (2.0f * glow_width * glow_width));
        float glow_intensity = 0.3f * sky_lum;
        px[0] += glow * glow_intensity;
        px[1] += glow * glow_intensity;
        px[2] += glow * glow_intensity;

        px[3] = 1.0f;
    }

    return true;
}
