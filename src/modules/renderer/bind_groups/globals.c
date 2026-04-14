#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_globals_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->ibl_shadow_bind_layout) {
        return impl->ibl_shadow_bind_layout;
    }

    flecsEngine_material_ensureBuffer(impl);

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
        { /* 3: Opaque scene snapshot (for transmission rendering) */
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
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

    /* Ensure fallback textures exist before we reference them */
    flecsEngine_pbr_texture_ensureFallbacks((FlecsEngineImpl*)engine);

    if (!engine->shadow.texture_view || !engine->shadow.sampler) {
        return false;
    }

    if (!engine->lighting.cluster_info_buffer || !engine->lighting.cluster_grid_buffer ||
        !engine->lighting.cluster_index_buffer || !engine->lighting.light_buffer)
    {
        return false;
    }

    if (!engine->frame_uniform_buffer) {
        return false;
    }

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
                {   /* Opaque snapshot for transmission. Uses the snapshot
                     * view if available, otherwise the 2D white fallback. */
                    .binding = 3,
                    .textureView = engine->opaque_snapshot_view
                        ? engine->opaque_snapshot_view
                        : engine->materials.fallback_white_2d_view
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
                }
            }
        });

    ibl->scene_bind_version = engine->scene_bind_version;

    return ibl->ibl_shadow_bind_group != NULL;
}
