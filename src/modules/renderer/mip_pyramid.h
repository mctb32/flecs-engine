#ifndef FLECS_ENGINE_MIP_PYRAMID_H
#define FLECS_ENGINE_MIP_PYRAMID_H

#include <stdint.h>
#include <stdbool.h>

#include "../../vendor.h"

uint32_t flecsEngine_mipPyramid_maxMips(
    uint32_t width,
    uint32_t height);

bool flecsEngine_mipPyramid_create(
    WGPUDevice device,
    uint32_t width,
    uint32_t height,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTextureUsage usage,
    WGPUTexture *out_texture,
    WGPUTextureView **out_mip_views);

void flecsEngine_mipPyramid_release(
    WGPUTexture *texture,
    WGPUTextureView **mip_views,
    uint32_t mip_count);

/* Sampler for reading from a mip pyramid: clamp-to-edge, linear min/mag/mip,
 * no anisotropy. Matches both bloom's and transmission's sampler config. */
WGPUSampler flecsEngine_mipPyramid_createFilteredSampler(
    WGPUDevice device);

#endif
