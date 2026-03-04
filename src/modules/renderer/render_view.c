#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsHdri);

ECS_CTOR(FlecsHdri, ptr, {
    ptr->file = NULL;
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
})

ECS_DTOR(FlecsHdri, ptr, {
    if (ptr->file) {
        ecs_os_free((char*)ptr->file);
        ptr->file = NULL;
    }
})

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->effects, ecs_entity_t, 0);
    ptr->camera = 0;
    ptr->light = 0;
    ptr->hdri = 0;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    dst->camera = src->camera;
    dst->light = src->light;
    dst->hdri = src->hdri;
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, ecs_entity_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->effects, ecs_entity_t);
})

static void flecsEngineRenderBatchSet(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngineRenderBatchSet(
                world,
                engine,
                pass,
                view,
                nested_batch_set);
            continue;
        }

        flecsEngineRenderBatch(world, engine, pass, view, batch_entity);
    }
}

void flecsEngineRenderView(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t view_entity,
    const FlecsRenderView *view)
{
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    /* Always set pipeline/uniforms for first batch in view */
    engine->last_pipeline = NULL;

    flecsEngineRenderBatchSet(
        world,
        engine,
        pass,
        view,
        batch_set);
}

void flecsEngine_renderView_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsHdri);

    ecs_set_hooks(world, FlecsHdri, {
        .ctor = ecs_ctor(FlecsHdri),
        .move = ecs_move(FlecsHdri),
        .copy = ecs_copy(FlecsHdri),
        .dtor = ecs_dtor(FlecsHdri)
    });

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHdri),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t entity_vector_t = ecs_vector(world, {
        .type = ecs_id(ecs_entity_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderView),
        .members = {
            { .name = "camera", .type = ecs_id(ecs_entity_t) },
            { .name = "light", .type = ecs_id(ecs_entity_t) },
            { .name = "hdri", .type = ecs_id(ecs_entity_t) },
            { .name = "effects", .type = entity_vector_t }
        }
    });
}

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file)
{
    if (!file || !file[0]) {
        ecs_err("cannot create hdri entity: missing file path");
        return 0;
    }

    ecs_entity_t hdri = ecs_entity(world, {
        .parent = parent,
        .name = name
    });

    ecs_set(world, hdri, FlecsHdri, {
        .file = file
    });

    return hdri;
}
