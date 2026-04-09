#include "renderer.h"
#include "flecs_engine.h"
#include "dds.h"

#include <stb_image.h>
#include <stdio.h>
#include <string.h>

static uint32_t flecsEngine_computeMipCount(
    uint32_t width,
    uint32_t height)
{
    uint32_t count = 1;
    uint32_t dim = width > height ? width : height;
    while (dim > 1) {
        dim >>= 1;
        count++;
    }
    return count;
}

static WGPUTexture flecsEngine_texture_createFromPixels(
    WGPUDevice device,
    WGPUQueue queue,
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    uint32_t mip_count = flecsEngine_computeMipCount(width, height);
    uint32_t bytes_per_pixel = 4;

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
        .format = format,
        .mipLevelCount = mip_count,
        .sampleCount = 1
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create texture (%ux%u)", width, height);
        return NULL;
    }

    /* Upload mip level 0 */
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

    /* Generate and upload remaining mip levels via box filter */
    if (mip_count > 1) {
        uint32_t prev_w = width, prev_h = height;
        uint8_t *prev_pixels = ecs_os_malloc(
            (ecs_size_t)(width * height * bytes_per_pixel));
        memcpy(prev_pixels, pixels, width * height * bytes_per_pixel);

        for (uint32_t mip = 1; mip < mip_count; mip++) {
            uint32_t mip_w = prev_w > 1 ? prev_w / 2 : 1;
            uint32_t mip_h = prev_h > 1 ? prev_h / 2 : 1;
            uint8_t *mip_pixels = ecs_os_malloc(
                (ecs_size_t)(mip_w * mip_h * bytes_per_pixel));

            for (uint32_t y = 0; y < mip_h; y++) {
                for (uint32_t x = 0; x < mip_w; x++) {
                    uint32_t sx = x * 2, sy = y * 2;
                    uint32_t sx1 = sx + 1 < prev_w ? sx + 1 : sx;
                    uint32_t sy1 = sy + 1 < prev_h ? sy + 1 : sy;

                    const uint8_t *p00 = &prev_pixels[(sy  * prev_w + sx ) * 4];
                    const uint8_t *p10 = &prev_pixels[(sy  * prev_w + sx1) * 4];
                    const uint8_t *p01 = &prev_pixels[(sy1 * prev_w + sx ) * 4];
                    const uint8_t *p11 = &prev_pixels[(sy1 * prev_w + sx1) * 4];

                    uint8_t *out = &mip_pixels[(y * mip_w + x) * 4];
                    for (int c = 0; c < 4; c++) {
                        out[c] = (uint8_t)(
                            (p00[c] + p10[c] + p01[c] + p11[c] + 2) / 4);
                    }
                }
            }

            dst.mipLevel = mip;
            src_layout.bytesPerRow = mip_w * bytes_per_pixel;
            src_layout.rowsPerImage = mip_h;
            write_size = (WGPUExtent3D){ mip_w, mip_h, 1 };
            wgpuQueueWriteTexture(
                queue, &dst, mip_pixels,
                (size_t)(mip_w * mip_h * bytes_per_pixel),
                &src_layout, &write_size);

            ecs_os_free(prev_pixels);
            prev_pixels = mip_pixels;
            prev_w = mip_w;
            prev_h = mip_h;
        }

        ecs_os_free(prev_pixels);
    }

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

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = info.width, .height = info.height,
                  .depthOrArrayLayers = 1 },
        .format = (WGPUTextureFormat)wgpu_format,
        .mipLevelCount = info.mip_count,
        .sampleCount = 1
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create DDS texture: %s", path);
        ecs_os_free(file_data);
        return NULL;
    }

    /* Upload all mip levels from the DDS file */
    const uint8_t *data_ptr = info.pixel_data;
    uint32_t mip_w = info.width, mip_h = info.height;

    for (uint32_t mip = 0; mip < info.mip_count; mip++) {
        uint32_t bw = (mip_w + 3) / 4;
        uint32_t bh = (mip_h + 3) / 4;
        uint32_t mip_data_size = bw * bh * block_size;

        if (data_ptr + mip_data_size > info.pixel_data + info.pixel_data_size) {
            break;
        }

        WGPUTexelCopyTextureInfo dst = {
            .texture = texture,
            .mipLevel = mip,
            .origin = { 0, 0, 0 },
            .aspect = WGPUTextureAspect_All
        };

        WGPUTexelCopyBufferLayout src_layout = {
            .offset = 0,
            .bytesPerRow = bw * block_size,
            .rowsPerImage = bh
        };

        /* Use physical (block-aligned) size for compressed texture copies */
        WGPUExtent3D write_size = { bw * 4, bh * 4, 1 };
        wgpuQueueWriteTexture(
            queue, &dst, data_ptr, mip_data_size,
            &src_layout, &write_size);

        data_ptr += mip_data_size;
        mip_w = mip_w > 1 ? mip_w / 2 : 1;
        mip_h = mip_h > 1 ? mip_h / 2 : 1;
    }

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

static WGPUTextureView flecsEngine_texture_createArrayView(
    WGPUTexture texture)
{
    WGPUTextureViewDescriptor vd = {
        .format = wgpuTextureGetFormat(texture),
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = wgpuTextureGetMipLevelCount(texture),
        .baseArrayLayer = 0,
        .arrayLayerCount = wgpuTextureGetDepthOrArrayLayers(texture)
    };
    return wgpuTextureCreateView(texture, &vd);
}

void flecsEngine_pbr_texture_ensureFallbacks(
    FlecsEngineImpl *engine)
{
    if (engine->materials.fallback_white_tex) {
        return;
    }

    WGPUDevice device = engine->device;
    WGPUQueue queue = engine->queue;

    engine->materials.fallback_white_tex = flecsEngine_texture_create1x1(
        device, queue, 255, 255, 255, 255);
    engine->materials.fallback_white_view =
        flecsEngine_texture_createArrayView(
            engine->materials.fallback_white_tex);

    engine->materials.fallback_black_tex = flecsEngine_texture_create1x1(
        device, queue, 0, 0, 0, 255);
    engine->materials.fallback_black_view =
        flecsEngine_texture_createArrayView(
            engine->materials.fallback_black_tex);

    /* Default normal: (0.5, 0.5, 1.0) = flat surface pointing up */
    engine->materials.fallback_normal_tex = flecsEngine_texture_create1x1(
        device, queue, 128, 128, 255, 255);
    engine->materials.fallback_normal_view =
        flecsEngine_texture_createArrayView(
            engine->materials.fallback_normal_tex);
}

WGPUSampler flecsEngine_pbr_texture_ensureSampler(
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
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 16
    };

    engine->materials.pbr_sampler = wgpuDeviceCreateSampler(
        engine->device, &sampler_desc);
    return engine->materials.pbr_sampler;
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
            tex_impl->view =
                flecsEngine_texture_createArrayView(texture);
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

    flecsEngine_textures_ensureBindLayout(engine);

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
        if (flecsEngine_textures_createBindGroup(
            engine, albedo_view, emissive_view,
            roughness_view, normal_view, &bind_group))
        {
            FlecsPbrTexturesImpl *tex_impl = ecs_ensure(
                world, it->entities[i], FlecsPbrTexturesImpl);
            tex_impl->bind_group = bind_group;
        }
    }
}
