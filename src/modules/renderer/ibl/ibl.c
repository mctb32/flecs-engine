#include "../renderer.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsHdri);
ECS_COMPONENT_DECLARE(FlecsHdriImpl);

ECS_CTOR(FlecsHdri, ptr, {
    ptr->file = NULL;
    ptr->filter_sample_count = 0;
    ptr->lut_sample_count = 0;
})

ECS_MOVE(FlecsHdri, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    *dst = *src;
    src->file = NULL;
})

ECS_COPY(FlecsHdri, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    dst->file = src->file ? ecs_os_strdup(src->file) : NULL;
    dst->filter_sample_count = src->filter_sample_count;
    dst->lut_sample_count = src->lut_sample_count;
})

ECS_DTOR(FlecsHdri, ptr, {
    if (ptr->file) {
        ecs_os_free((char*)ptr->file);
        ptr->file = NULL;
    }
})

ECS_DTOR(FlecsHdriImpl, ptr, {
    flecsEngine_ibl_releaseRuntimeResources(ptr);
})

ECS_MOVE(FlecsHdriImpl, dst, src, {
    flecsEngine_ibl_releaseRuntimeResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl)
{
    if (!ibl) {
        return;
    }

    if (ibl->ibl_shadow_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_shadow_bind_group);
        ibl->ibl_shadow_bind_group = NULL;
    }

    if (ibl->ibl_sampler) {
        wgpuSamplerRelease(ibl->ibl_sampler);
        ibl->ibl_sampler = NULL;
    }

    if (ibl->ibl_prefiltered_cubemap_view) {
        wgpuTextureViewRelease(ibl->ibl_prefiltered_cubemap_view);
        ibl->ibl_prefiltered_cubemap_view = NULL;
    }

    if (ibl->ibl_prefiltered_cubemap) {
        wgpuTextureRelease(ibl->ibl_prefiltered_cubemap);
        ibl->ibl_prefiltered_cubemap = NULL;
    }

    if (ibl->ibl_irradiance_cubemap_view) {
        wgpuTextureViewRelease(ibl->ibl_irradiance_cubemap_view);
        ibl->ibl_irradiance_cubemap_view = NULL;
    }

    if (ibl->ibl_irradiance_cubemap) {
        wgpuTextureRelease(ibl->ibl_irradiance_cubemap);
        ibl->ibl_irradiance_cubemap = NULL;
    }

    if (ibl->ibl_equirect_texture_view) {
        wgpuTextureViewRelease(ibl->ibl_equirect_texture_view);
        ibl->ibl_equirect_texture_view = NULL;
    }

    if (ibl->ibl_equirect_texture) {
        wgpuTextureRelease(ibl->ibl_equirect_texture);
        ibl->ibl_equirect_texture = NULL;
    }

    ibl->ibl_prefilter_mip_count = 0;
}

static void FlecsIbl_on_set(
    ecs_iter_t *it)
{
    FlecsEngineImpl *engine = ecs_singleton_get_mut(
        it->world, FlecsEngineImpl);
    if (!engine) {
        ecs_err("failed to create IBL: engine not initialized");
        return;
    }

    if (!flecsEngine_globals_ensureBindLayout(engine)) {
        ecs_err("failed to create scene-globals bind layout");
        return;
    }

    FlecsHdri *hdri = ecs_field(it, FlecsHdri, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        FlecsHdriImpl *ibl_impl = ecs_ensure(
            it->world, it->entities[i], FlecsHdriImpl);
        flecsEngine_ibl_releaseRuntimeResources(ibl_impl);

        if (!flecsEngine_ibl_initResources(
            engine,
            ibl_impl,
            hdri[i].file,
            NULL, NULL, NULL, NULL,
            hdri[i].filter_sample_count,
            hdri[i].lut_sample_count))
        {
            ecs_err("failed to initialize IBL resources");
            continue;
        }

        ecs_modified(it->world, it->entities[i], FlecsHdriImpl);
    }
}

void flecsEngine_ibl_ensureSkyBackground(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const flecs_engine_background_t *background)
{
    if (!engine->sky_background_hdri) {
        return;
    }

    if (!memcmp(&engine->sky_bg_colors, background,
        sizeof(flecs_engine_background_t)))
    {
        return;
    }

    engine->sky_bg_colors = *background;

    FlecsHdriImpl *ibl = ecs_get_mut(
        world, engine->sky_background_hdri, FlecsHdriImpl);
    if (!ibl) {
        return;
    }

    const FlecsHdri *hdri = ecs_get(
        world, engine->sky_background_hdri, FlecsHdri);
    if (!hdri) {
        return;
    }

    flecsEngine_ibl_releaseRuntimeResources(ibl);
    flecsEngine_ibl_initResources(
        engine, ibl, NULL,
        &background->sky_color,
        &background->ground_color,
        &background->haze_color,
        &background->horizon_color,
        hdri->filter_sample_count,
        hdri->lut_sample_count);
}

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount)
{
    ecs_entity_t hdri = ecs_entity(world, {
        .parent = parent,
        .name = name
    });

    ecs_set(world, hdri, FlecsHdri, {
        .file = file,
        .filter_sample_count = filterSampleCount,
        .lut_sample_count = lutSampleCount
    });

    return hdri;
}

void flecsEngine_ibl_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsHdri);
    ECS_COMPONENT_DEFINE(world, FlecsHdriImpl);

    ecs_set_hooks(world, FlecsHdri, {
        .ctor = ecs_ctor(FlecsHdri),
        .move = ecs_move(FlecsHdri),
        .copy = ecs_copy(FlecsHdri),
        .dtor = ecs_dtor(FlecsHdri),
        .on_set = FlecsIbl_on_set
    });

    ecs_set_hooks(world, FlecsHdriImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsHdriImpl),
        .dtor = ecs_dtor(FlecsHdriImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHdri),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) },
            { .name = "filter_sample_count", .type = ecs_id(ecs_u32_t) },
            { .name = "lut_sample_count", .type = ecs_id(ecs_u32_t) }
        }
    });
}
