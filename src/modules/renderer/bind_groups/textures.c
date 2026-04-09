#include "../renderer.h"
#include "flecs_engine.h"

/* PBR material textures bind group (group 1).
 *
 * Holds the four PBR texture arrays (albedo / emissive / roughness-metallic /
 * normal) plus the filtering sampler. A bind group is created per
 * FlecsPbrTextures entity (specific texture set) and once more for the
 * global texture-array path in render_materials.c, both against the same
 * shared layout.
 *
 * Fallback texture views and the sampler itself are owned by render_textures.c
 * (see flecsEngine_pbr_texture_ensureFallbacks / ensureSampler) — this file
 * only assembles them into the bind group.
 *
 * Binding layout:
 *   0: Albedo 2DArray
 *   1: Emissive 2DArray
 *   2: Roughness / metallic 2DArray
 *   3: Normal 2DArray
 *   4: Filtering sampler
 */

WGPUBindGroupLayout flecsEngine_textures_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->materials.pbr_texture_bind_layout) {
        return impl->materials.pbr_texture_bind_layout;
    }

    WGPUBindGroupLayoutEntry entries[5] = {
        { /* albedo */
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* emissive */
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* roughness/metallic */
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* normal */
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* sampler */
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        }
    };

    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entries = entries,
        .entryCount = 5
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

    WGPUBindGroupEntry entries[5] = {
        { .binding = 0, .textureView = albedo_view },
        { .binding = 1, .textureView = emissive_view },
        { .binding = 2, .textureView = roughness_view },
        { .binding = 3, .textureView = normal_view },
        { .binding = 4, .sampler = sampler }
    };

    WGPUBindGroupDescriptor bg_desc = {
        .layout = layout,
        .entries = entries,
        .entryCount = 5
    };

    *out_bind_group = wgpuDeviceCreateBindGroup(engine->device, &bg_desc);
    return *out_bind_group != NULL;
}
