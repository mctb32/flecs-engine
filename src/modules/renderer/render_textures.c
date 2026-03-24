#include "renderer.h"
#include "flecs_engine.h"
#include "dds.h"

#include <stb_image.h>
#include <stdio.h>

static WGPUTexture flecsEngine_texture_createFromPixels(
    WGPUDevice device,
    WGPUQueue queue,
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create texture (%ux%u)", width, height);
        return NULL;
    }

    uint32_t bytes_per_pixel = 4;
    WGPUTexelCopyTextureInfo dst = {
        .texture = texture,
        .mipLevel = 0,
        .origin = { 0, 0, 0 },
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout src_layout = {
        .offset = 0,
        .bytesPerRow = width * bytes_per_pixel,
        .rowsPerImage = height
    };

    WGPUExtent3D write_size = { width, height, 1 };
    wgpuQueueWriteTexture(
        queue, &dst, pixels,
        (size_t)(width * height * bytes_per_pixel),
        &src_layout, &write_size);

    return texture;
}

WGPUTexture flecsEngine_texture_create1x1(
    WGPUDevice device,
    WGPUQueue queue,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t pixels[4] = { r, g, b, a };
    return flecsEngine_texture_createFromPixels(
        device, queue, pixels, 1, 1, WGPUTextureFormat_RGBA8Unorm);
}

static WGPUTexture flecsEngine_texture_loadDds(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ecs_err("failed to open DDS file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        ecs_err("invalid DDS file size: %s", path);
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = ecs_os_malloc((ecs_size_t)file_size);
    if (fread(file_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        ecs_err("failed to read DDS file: %s", path);
        ecs_os_free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    FlecsDdsInfo info;
    if (!flecs_dds_parse(file_data, (uint32_t)file_size, &info)) {
        ecs_err("failed to parse DDS: %s", path);
        ecs_os_free(file_data);
        return NULL;
    }

    int wgpu_format = flecs_dds_wgpu_format(info.dxgi_format);
    if (!wgpu_format) {
        ecs_err("unsupported DDS format %u: %s", info.dxgi_format, path);
        ecs_os_free(file_data);
        return NULL;
    }

    uint32_t block_size = flecs_dds_block_size(info.dxgi_format);
    uint32_t blocks_wide = (info.width + 3) / 4;
    uint32_t blocks_high = (info.height + 3) / 4;

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = info.width, .height = info.height,
                  .depthOrArrayLayers = 1 },
        .format = (WGPUTextureFormat)wgpu_format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create DDS texture: %s", path);
        ecs_os_free(file_data);
        return NULL;
    }

    uint32_t data_size = blocks_wide * blocks_high * block_size;

    WGPUTexelCopyTextureInfo dst = {
        .texture = texture,
        .mipLevel = 0,
        .origin = { 0, 0, 0 },
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout src_layout = {
        .offset = 0,
        .bytesPerRow = blocks_wide * block_size,
        .rowsPerImage = blocks_high
    };

    WGPUExtent3D write_size = { info.width, info.height, 1 };

    wgpuQueueWriteTexture(
        queue, &dst, info.pixel_data, data_size,
        &src_layout, &write_size);

    ecs_os_free(file_data);
    return texture;
}

static bool flecsEngine_pathEndsWith(
    const char *path,
    const char *ext)
{
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    return strcmp(path + plen - elen, ext) == 0;
}

WGPUTexture flecsEngine_texture_loadFile(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path)
{
    if (flecsEngine_pathEndsWith(path, ".dds")) {
        return flecsEngine_texture_loadDds(device, queue, path);
    }

    int w, h, channels;
    uint8_t *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        /* Try .dds fallback for .png paths */
        if (flecsEngine_pathEndsWith(path, ".png")) {
            size_t len = strlen(path);
            char *dds_path = ecs_os_malloc((ecs_size_t)(len + 1));
            memcpy(dds_path, path, len + 1);
            memcpy(dds_path + len - 4, ".dds", 4);
            WGPUTexture tex = flecsEngine_texture_loadDds(
                device, queue, dds_path);
            ecs_os_free(dds_path);
            if (tex) {
                return tex;
            }
        }
        ecs_err("failed to load texture: %s (%s)", path, stbi_failure_reason());
        return NULL;
    }

    WGPUTexture tex = flecsEngine_texture_createFromPixels(
        device, queue, pixels, (uint32_t)w, (uint32_t)h,
        WGPUTextureFormat_RGBA8Unorm);

    stbi_image_free(pixels);
    return tex;
}

WGPUBindGroupLayout flecsEngine_pbr_texture_ensureBindLayout(
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
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        { /* emissive */
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        { /* roughness/metallic */
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        { /* normal */
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
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

static void flecsEngine_pbr_texture_ensureFallbacks(
    FlecsEngineImpl *engine)
{
    if (engine->materials.fallback_white_tex) {
        return;
    }

    WGPUDevice device = engine->device;
    WGPUQueue queue = engine->queue;

    engine->materials.fallback_white_tex = flecsEngine_texture_create1x1(
        device, queue, 255, 255, 255, 255);
    engine->materials.fallback_white_view = wgpuTextureCreateView(
        engine->materials.fallback_white_tex, NULL);

    engine->materials.fallback_black_tex = flecsEngine_texture_create1x1(
        device, queue, 0, 0, 0, 255);
    engine->materials.fallback_black_view = wgpuTextureCreateView(
        engine->materials.fallback_black_tex, NULL);

    /* Default normal: (0.5, 0.5, 1.0) = flat surface pointing up */
    engine->materials.fallback_normal_tex = flecsEngine_texture_create1x1(
        device, queue, 128, 128, 255, 255);
    engine->materials.fallback_normal_view = wgpuTextureCreateView(
        engine->materials.fallback_normal_tex, NULL);
}

static WGPUSampler flecsEngine_pbr_texture_ensureSampler(
    FlecsEngineImpl *engine)
{
    if (engine->materials.pbr_sampler) {
        return engine->materials.pbr_sampler;
    }

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .maxAnisotropy = 1
    };

    engine->materials.pbr_sampler = wgpuDeviceCreateSampler(
        engine->device, &sampler_desc);
    return engine->materials.pbr_sampler;
}

bool flecsEngine_pbr_texture_createBindGroup(
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

    WGPUBindGroupLayout layout = flecsEngine_pbr_texture_ensureBindLayout(engine);
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

void flecsEngine_texture_onSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsTexture *tex = ecs_field(it, FlecsTexture, 0);

    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
    if (!engine) {
        return;
    }

    for (int i = 0; i < it->count; i++) {
        if (!tex[i].path) {
            continue;
        }

        WGPUTexture texture = flecsEngine_texture_loadFile(
            engine->device, engine->queue, tex[i].path);
        if (texture) {
            FlecsTextureImpl *tex_impl = ecs_ensure(
                world, it->entities[i], FlecsTextureImpl);
            tex_impl->texture = texture;
            tex_impl->view = wgpuTextureCreateView(texture, NULL);
        }
    }
}

void flecsEngine_pbrTextures_onSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsPbrTextures *tex = ecs_field(it, FlecsPbrTextures, 0);

    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
    if (!engine) {
        return;
    }

    flecsEngine_pbr_texture_ensureBindLayout(engine);

    for (int i = 0; i < it->count; i++) {
        WGPUTextureView albedo_view = NULL;
        WGPUTextureView emissive_view = NULL;
        WGPUTextureView roughness_view = NULL;
        WGPUTextureView normal_view = NULL;

        if (tex[i].albedo) {
            const FlecsTextureImpl *impl = ecs_get(
                world, tex[i].albedo, FlecsTextureImpl);
            if (impl) albedo_view = impl->view;
        }
        if (tex[i].emissive) {
            const FlecsTextureImpl *impl = ecs_get(
                world, tex[i].emissive, FlecsTextureImpl);
            if (impl) emissive_view = impl->view;
        }
        if (tex[i].roughness) {
            const FlecsTextureImpl *impl = ecs_get(
                world, tex[i].roughness, FlecsTextureImpl);
            if (impl) roughness_view = impl->view;
        }
        if (tex[i].normal) {
            const FlecsTextureImpl *impl = ecs_get(
                world, tex[i].normal, FlecsTextureImpl);
            if (impl) normal_view = impl->view;
        }

        WGPUBindGroup bind_group = NULL;
        if (flecsEngine_pbr_texture_createBindGroup(
            engine, albedo_view, emissive_view,
            roughness_view, normal_view, &bind_group))
        {
            tex[i]._bind_group = bind_group;
        }
    }
}
