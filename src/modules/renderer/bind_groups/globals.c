#include "../renderer.h"
#include "flecs_engine.h"

/* Scene-globals bind group (group 0).
 *
 * This bind group bundles every per-frame / scene-stable resource that the
 * PBR shaders sample from: IBL probes, the cascaded shadow map, the clustered
 * light data, and the materials storage buffer. It lives on FlecsHdriImpl
 * because the IBL textures are HDRI-specific; switching HDRIs switches the
 * whole bind group. Non-HDRI state (shadow, cluster, materials) is taken from
 * the shared engine state.
 *
 * Binding layout:
 *    0: IBL prefiltered env cubemap
 *    1: IBL sampler
 *    2: IBL BRDF LUT
 *    3: Shadow depth texture array
 *    4: Shadow comparison sampler
 *    5: Cluster info uniform
 *    6: Cluster grid storage
 *    7: Light indices storage
 *    8: Lights storage (unified point + spot)
 *    9: IBL irradiance (Lambertian-convolved) env cubemap
 *   10: Materials storage (indexed by FlecsMaterialId)
 */

WGPUBindGroupLayout flecsEngine_globals_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->ibl_shadow_bind_layout) {
        return impl->ibl_shadow_bind_layout;
    }

    /* The scene-globals bind group references the materials storage
     * buffer. Allocate a dummy one up-front so HDRI init (which calls
     * flecsEngine_globals_createBindGroup) can complete even before
     * any material has been defined by the scene. */
    flecsEngine_material_ensureBuffer(impl);

    WGPUBindGroupLayoutEntry layout_entries[11] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Comparison
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsClusterInfo)
            }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsClusterEntry)
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(uint32_t)
            }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuLight)
            }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuMaterial)
            }
        }
    };

    impl->ibl_shadow_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 11,
            .entries = layout_entries
        });

    return impl->ibl_shadow_bind_layout;
}

bool flecsEngine_globals_createBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl)
{
    if (ibl->ibl_shadow_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_shadow_bind_group);
        ibl->ibl_shadow_bind_group = NULL;
    }

    WGPUBindGroupLayout bind_layout = engine->ibl_shadow_bind_layout;
    if (!bind_layout) {
        return false;
    }

    if (!engine->shadow.texture_view || !engine->shadow.sampler) {
        return false;
    }

    if (!engine->lighting.cluster_info_buffer || !engine->lighting.cluster_grid_buffer ||
        !engine->lighting.cluster_index_buffer || !engine->lighting.light_buffer)
    {
        return false;
    }

    if (!engine->materials.buffer) {
        return false;
    }

    uint64_t material_buffer_size =
        (uint64_t)engine->materials.buffer_capacity * sizeof(FlecsGpuMaterial);

    ibl->ibl_shadow_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = bind_layout,
            .entryCount = 11,
            .entries = (WGPUBindGroupEntry[11]){
                {
                    .binding = 0,
                    .textureView = ibl->ibl_prefiltered_cubemap_view
                },
                {
                    .binding = 1,
                    .sampler = ibl->ibl_sampler
                },
                {
                    .binding = 2,
                    .textureView = ibl->ibl_brdf_lut_texture_view
                },
                {
                    .binding = 3,
                    .textureView = engine->shadow.texture_view
                },
                {
                    .binding = 4,
                    .sampler = engine->shadow.sampler
                },
                {
                    .binding = 5,
                    .buffer = engine->lighting.cluster_info_buffer,
                    .size = sizeof(FlecsClusterInfo)
                },
                {
                    .binding = 6,
                    .buffer = engine->lighting.cluster_grid_buffer,
                    .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
                        sizeof(FlecsClusterEntry)
                },
                {
                    .binding = 7,
                    .buffer = engine->lighting.cluster_index_buffer,
                    .size = (uint64_t)engine->lighting.cluster_index_capacity *
                        sizeof(uint32_t)
                },
                {
                    .binding = 8,
                    .buffer = engine->lighting.light_buffer,
                    .size = (uint64_t)engine->lighting.light_capacity *
                        sizeof(FlecsGpuLight)
                },
                {
                    .binding = 9,
                    .textureView = ibl->ibl_irradiance_cubemap_view
                },
                {
                    .binding = 10,
                    .buffer = engine->materials.buffer,
                    .size = material_buffer_size
                }
            }
        });

    ibl->scene_bind_version = engine->scene_bind_version;

    return ibl->ibl_shadow_bind_group != NULL;
}
