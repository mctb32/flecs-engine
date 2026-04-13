#ifndef FLECS_ENGINE_MIP_PYRAMID_H
#define FLECS_ENGINE_MIP_PYRAMID_H

#include <stdint.h>
#include <stdbool.h>

#include "../../vendor.h"

/* Largest mip count for a w x h 2D image, where mip (N-1) has both dimensions
 * >= 1. For 8x8 returns 4 (mips are 8,4,2,1). Callers apply their own
 * clamping policy on top (e.g. transmission drops the final 1x1 mip, bloom
 * clamps to a user-requested count). */
uint32_t flecsEngine_mipPyramid_maxMips(
    uint32_t width,
    uint32_t height);

/* Create a 2D texture with the given dimensions + mip count + format + usage,
 * and allocate + populate an array of single-mip 2D views (one per level).
 * On success, caller owns both *out_texture and *out_mip_views and must
 * release them via flecsEngine_mipPyramid_release. On failure, all partial
 * allocations are cleaned up and the out pointers are NULL. */
bool flecsEngine_mipPyramid_create(
    WGPUDevice device,
    uint32_t width,
    uint32_t height,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTextureUsage usage,
    WGPUTexture *out_texture,
    WGPUTextureView **out_mip_views);

/* Release a texture and its mip view array (as created by
 * flecsEngine_mipPyramid_create). Safe on NULL/already-released pointers;
 * zeroes out the caller's storage. */
void flecsEngine_mipPyramid_release(
    WGPUTexture *texture,
    WGPUTextureView **mip_views,
    uint32_t mip_count);

/* Sampler for reading from a mip pyramid: clamp-to-edge, linear min/mag/mip,
 * no anisotropy. Matches both bloom's and transmission's sampler config. */
WGPUSampler flecsEngine_mipPyramid_createFilteredSampler(
    WGPUDevice device);

#endif
