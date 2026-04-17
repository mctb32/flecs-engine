#include "common.h"

void flecsEngine_batch_group_cullIdentity(
    flecsEngine_batch_group_t *ctx)
{
    flecsEngine_batch_t *buf = ctx->batch;
    int32_t src_base = ctx->view.offset;
    int32_t src_end = src_base + ctx->view.count;
    int32_t dst_base = ctx->view.visible_offset;
    uint32_t *visible = buf->buffers.cpu_visible_slots;

    for (int32_t slot = src_base; slot < src_end; slot ++) {
        visible[dst_base + (slot - src_base)] = (uint32_t)slot;
    }
    ctx->view.visible_count = ctx->view.count;
}

void flecsEngine_batch_group_cull(
    const FlecsRenderViewImpl *view_impl,
    flecsEngine_batch_group_t *ctx)
{
    FLECS_TRACY_ZONE_BEGIN("CullGroup");

    flecsEngine_batch_t *buf = ctx->batch;
    int32_t src_base = ctx->view.offset;
    int32_t src_end = src_base + ctx->view.count;
    int32_t dst_base = ctx->view.visible_offset;
    int32_t dst = dst_base;

    if (!view_impl->frustum_valid) {
        ctx->view.visible_count = 0;
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsAABB *aabbs = buf->buffers.cpu_aabb;
    uint32_t *visible = buf->buffers.cpu_visible_slots;
    bool do_screen_cull = view_impl->screen_cull_valid;

    for (int32_t slot = src_base; slot < src_end; slot ++) {
        const FlecsAABB *a = &aabbs[slot];
        if (!flecsEngine_testAABBFrustum(
                view_impl->frustum_planes, a->min, a->max))
        {
            continue;
        }

        if (do_screen_cull && !flecsEngine_testScreenSize(
                view_impl->camera_pos,
                a->min, a->max,
                view_impl->screen_cull_factor,
                view_impl->screen_cull_threshold))
        {
            continue;
        }

        visible[dst ++] = (uint32_t)slot;
    }

    ctx->view.visible_count = dst - dst_base;
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_group_cullShadow(
    const FlecsRenderViewImpl *view_impl,
    flecsEngine_batch_group_t *ctx)
{
    FLECS_TRACY_ZONE_BEGIN("CullGroupShadow");

    flecsEngine_batch_t *buf = ctx->batch;
    int32_t src_base = ctx->view.offset;
    int32_t src_end = src_base + ctx->view.count;

    const FlecsAABB *aabbs = buf->buffers.cpu_aabb;

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        uint32_t *visible = buf->buffers.cpu_shadow_visible_slots[c];
        int32_t dst_base = ctx->view.shadow_visible_offset[c];
        int32_t dst = dst_base;

        for (int32_t slot = src_base; slot < src_end; slot ++) {
            const FlecsAABB *a = &aabbs[slot];
            if (!flecsEngine_testAABBFrustum(
                    view_impl->cascade_frustum_planes[c], a->min, a->max))
            {
                continue;
            }

            visible[dst ++] = (uint32_t)slot;
        }

        ctx->view.shadow_visible_count[c] = dst - dst_base;
    }

    FLECS_TRACY_ZONE_END;
}
