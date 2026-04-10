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
 *    0: FlecsUniform (camera / view / shadow info — frame data)
 *    1: IBL prefiltered env cubemap
 *    2: IBL sampler
 *    3: IBL BRDF LUT
 *    4: IBL irradiance (Lambertian-convolved) env cubemap
 *    5: Shadow depth texture array
 *    6: Shadow comparison sampler
 *    7: Cluster info uniform
 *    8: Cluster grid storage
 *    9: Light indices storage
 *   10: Lights storage (unified point + spot)
 *   11: Materials storage (indexed by FlecsMaterialId)
 */

WGPUBindGroupLayout flecsEngine_globals_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->ibl_shadow_bind_layout) {
        return impl->ibl_shadow_bind_layout;
    }

    /* The scene-globals bind group references the materials storage
     * buffer and a frame uniform buffer. Allocate a dummy materials buffer
     * up-front so HDRI init (which calls flecsEngine_globals_createBindGroup)
     * can complete even before any material has been defined by the scene. */
    flecsEngine_material_ensureBuffer(impl);

    /* Allocate the single engine-global frame uniform buffer. Contents are
     * written once per view during rendering; the buffer handle itself is
     * stable so the bind group never needs rebuilding for this binding. */
    if (!impl->frame_uniform_buffer) {
        impl->frame_uniform_buffer = wgpuDeviceCreateBuffer(
            impl->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                .size = sizeof(FlecsUniform)
            });
        if (!impl->frame_uniform_buffer) {
            return NULL;
        }
    }

    WGPUBindGroupLayoutEntry layout_entries[11] = {
        { /* 0: Frame uniform (FlecsUniform) */
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsUniform)
            }
        },
        { /* 1: IBL prefiltered env cubemap */
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        { /* 2: IBL sampler */
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        { /* 4: IBL irradiance cubemap */
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        { /* 5: Shadow depth texture array */
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* 6: Shadow comparison sampler */
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Comparison
            }
        },
        { /* 7: Cluster info */
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsClusterInfo)
            }
        },
        { /* 8: Cluster grid */
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsClusterEntry)
            }
        },
        { /* 9: Light indices */
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(uint32_t)
            }
        },
        { /* 10: Lights (point + spot) */
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuLight)
            }
        },
        { /* 11: Materials storage */
            .binding = 11,
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

    if (!engine->frame_uniform_buffer) {
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
                    .buffer = engine->frame_uniform_buffer,
                    .size = sizeof(FlecsUniform)
                },
                {
                    .binding = 1,
                    .textureView = ibl->ibl_prefiltered_cubemap_view
                },
                {
                    .binding = 2,
                    .sampler = ibl->ibl_sampler
                },
                {
                    .binding = 4,
                    .textureView = ibl->ibl_irradiance_cubemap_view
                },
                {
                    .binding = 5,
                    .textureView = engine->shadow.texture_view
                },
                {
                    .binding = 6,
                    .sampler = engine->shadow.sampler
                },
                {
                    .binding = 7,
                    .buffer = engine->lighting.cluster_info_buffer,
                    .size = sizeof(FlecsClusterInfo)
                },
                {
                    .binding = 8,
                    .buffer = engine->lighting.cluster_grid_buffer,
                    .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
                        sizeof(FlecsClusterEntry)
                },
                {
                    .binding = 9,
                    .buffer = engine->lighting.cluster_index_buffer,
                    .size = (uint64_t)engine->lighting.cluster_index_capacity *
                        sizeof(uint32_t)
                },
                {
                    .binding = 10,
                    .buffer = engine->lighting.light_buffer,
                    .size = (uint64_t)engine->lighting.light_capacity *
                        sizeof(FlecsGpuLight)
                },
                {
                    .binding = 11,
                    .buffer = engine->materials.buffer,
                    .size = material_buffer_size
                }
            }
        });

    ibl->scene_bind_version = engine->scene_bind_version;

    return ibl->ibl_shadow_bind_group != NULL;
}
