#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_materialBind_ensureLayout(
    FlecsEngineImpl *impl)
{
    if (impl->material_bind_layout) {
        return impl->material_bind_layout;
    }

    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsGpuMaterial)
        }
    };

    impl->material_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 1,
            .entries = &entry
        });

    return impl->material_bind_layout;
}

WGPUBindGroup flecsEngine_materialBind_createGroup(
    const FlecsEngineImpl *engine,
    WGPUBuffer buffer,
    uint64_t size)
{
    if (!engine->material_bind_layout || !buffer || !size) {
        return NULL;
    }

    return wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = engine->material_bind_layout,
            .entryCount = 1,
            .entries = (WGPUBindGroupEntry[1]){
                {
                    .binding = 0,
                    .buffer = buffer,
                    .size = size
                }
            }
        });
}

WGPUBindGroup flecsEngine_materialBind_ensureScene(
    FlecsEngineImpl *impl)
{
    flecsEngine_material_ensureBuffer(impl);
    if (!impl->materials.buffer) {
        return NULL;
    }

    flecsEngine_materialBind_ensureLayout(impl);
    if (!impl->material_bind_layout) {
        return NULL;
    }

    if (impl->scene_material_bind_group &&
        impl->scene_material_bind_version == impl->scene_bind_version)
    {
        return impl->scene_material_bind_group;
    }

    if (impl->scene_material_bind_group) {
        wgpuBindGroupRelease(impl->scene_material_bind_group);
        impl->scene_material_bind_group = NULL;
    }

    uint64_t size =
        (uint64_t)impl->materials.buffer_capacity * sizeof(FlecsGpuMaterial);

    impl->scene_material_bind_group = flecsEngine_materialBind_createGroup(
        impl, impl->materials.buffer, size);
    impl->scene_material_bind_version = impl->scene_bind_version;

    return impl->scene_material_bind_group;
}

void flecsEngine_materialBind_releaseScene(
    FlecsEngineImpl *impl)
{
    if (impl->scene_material_bind_group) {
        wgpuBindGroupRelease(impl->scene_material_bind_group);
        impl->scene_material_bind_group = NULL;
    }
    if (impl->material_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->material_bind_layout);
        impl->material_bind_layout = NULL;
    }
    impl->scene_material_bind_version = 0;
}
