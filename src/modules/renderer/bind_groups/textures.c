#include "../renderer.h"
#include "flecs_engine.h"

/* PBR material textures bind group (group 1).
 *
 * Holds 12 PBR texture arrays — 4 channels (albedo, emissive, roughness,
 * normal) × 3 size buckets (512², 1024², 2048²) — plus a single filtering
 * sampler. The shader picks the right bucket per fragment via a `bucket`
 * field on the material struct.
 *
 * A single bind group is built per scene by render_materials.c when the
 * material array build succeeds. The per-prefab fallback in render_textures.c
 * builds an additional bind group with the same layout shape, plugging its
 * source textures into the bucket-1 (1024) slots and using fallback views
 * for the other 8 slots.
 *
 * Binding layout:
 *    0..2   albedo   2DArray  (bucket 0, 1, 2)
 *    3..5   emissive 2DArray  (bucket 0, 1, 2)
 *    6..8   roughness 2DArray (bucket 0, 1, 2)
 *    9..11  normal   2DArray  (bucket 0, 1, 2)
 *   12      filtering sampler
 */

WGPUBindGroupLayout flecsEngine_textures_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->materials.pbr_texture_bind_layout) {
        return impl->materials.pbr_texture_bind_layout;
    }

    WGPUBindGroupLayoutEntry entries[13] = {0};
    for (uint32_t i = 0; i < 12; i++) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2DArray;
        entries[i].texture.multisampled = false;
    }
    entries[12].binding = 12;
    entries[12].visibility = WGPUShaderStage_Fragment;
    entries[12].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entries = entries,
        .entryCount = 13
    };

    impl->materials.pbr_texture_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &layout_desc);

    return impl->materials.pbr_texture_bind_layout;
}

bool flecsEngine_textures_createBindGroup(
    FlecsEngineImpl *engine,
    WGPUTextureView albedo_view,
    WGPUTextureView emissive_view,
    WGPUTextureView roughness_view,
    WGPUTextureView normal_view,
    WGPUBindGroup *out_bind_group)
{
    flecsEngine_pbr_texture_ensureFallbacks(engine);

    if (!albedo_view) {
        albedo_view = engine->materials.fallback_white_view;
    }
    if (!emissive_view) {
        emissive_view = engine->materials.fallback_white_view;
    }
    if (!roughness_view) {
        roughness_view = engine->materials.fallback_white_view;
    }
    if (!normal_view) {
        normal_view = engine->materials.fallback_normal_view;
    }

    WGPUSampler sampler = flecsEngine_pbr_texture_ensureSampler(engine);

    WGPUBindGroupLayout layout = flecsEngine_textures_ensureBindLayout(engine);
    if (!layout) {
        return false;
    }

    /* The fallback path uses the bucket-1 (1024²) slots for its source
     * textures and dummies elsewhere. All materials served by this bind
     * group must have texture_bucket = 1 in their GpuMaterial entry. */
    WGPUTextureView white  = engine->materials.fallback_white_view;
    WGPUTextureView normal = engine->materials.fallback_normal_view;

    WGPUTextureView views[12] = {
        /* albedo    0  1  2 */ white,    albedo_view,    white,
        /* emissive  3  4  5 */ white,    emissive_view,  white,
        /* roughness 6  7  8 */ white,    roughness_view, white,
        /* normal    9 10 11 */ normal,   normal_view,    normal,
    };

    WGPUBindGroupEntry entries[13];
    for (int i = 0; i < 12; i++) {
        entries[i] = (WGPUBindGroupEntry){
            .binding = (uint32_t)i,
            .textureView = views[i]
        };
    }
    entries[12] = (WGPUBindGroupEntry){
        .binding = 12,
        .sampler = sampler
    };

    WGPUBindGroupDescriptor bg_desc = {
        .layout = layout,
        .entries = entries,
        .entryCount = 13
    };

    *out_bind_group = wgpuDeviceCreateBindGroup(engine->device, &bg_desc);
    return *out_bind_group != NULL;
}
