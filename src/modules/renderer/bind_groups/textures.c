#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_textures_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->textures.pbr_bind_layout) {
        return impl->textures.pbr_bind_layout;
    }

    WGPUBindGroupLayoutEntry entries[14] = {0};
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
    entries[13].binding = 13;
    entries[13].visibility = WGPUShaderStage_Fragment;
    entries[13].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entries = entries,
        .entryCount = 14
    };

    impl->textures.pbr_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &layout_desc);

    return impl->textures.pbr_bind_layout;
}
