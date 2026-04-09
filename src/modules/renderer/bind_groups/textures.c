#include "../renderer.h"
#include "flecs_engine.h"

/* PBR material textures bind group (group 1).
 *
 * Holds 12 PBR texture arrays — 4 channels (albedo, emissive, roughness,
 * normal) × 3 size buckets (512², 1024², 2048²) — plus a single filtering
 * sampler. The shader picks the right bucket per fragment via a `bucket`
 * field on the material struct.
 *
 * A single bind group is built per scene by render_materials.c. Both the
 * opaque and transparent textured-mesh batches bind it once at @group(1)
 * for every draw call.
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

