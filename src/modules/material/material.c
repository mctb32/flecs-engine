#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);
ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
ECS_COMPONENT_DECLARE(FlecsEmissive);
ECS_COMPONENT_DECLARE(FlecsMaterialId);
ECS_COMPONENT_DECLARE(FlecsTexture);
ECS_COMPONENT_DECLARE(FlecsPbrTextures);

static void FlecsMaterialIdOnAdd(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsEngineImpl *impl = ecs_singleton_ensure(world, FlecsEngineImpl);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has(world, e, FlecsMaterialId)) {
            continue;
        }

        FlecsMaterialId *material_id = ecs_ensure(world, e, FlecsMaterialId);
        material_id->value = impl->materials.next_id;
        impl->materials.next_id ++;
        ecs_modified(world, e, FlecsMaterialId);
    }
}

/* FlecsTexture lifecycle */

static void FlecsTexture_fini(
    FlecsTexture *ptr)
{
    ecs_os_free((char*)ptr->path);
}

ECS_CTOR(FlecsTexture, ptr, {
    ecs_os_zeromem(ptr);
})

ECS_DTOR(FlecsTexture, ptr, {
    FlecsTexture_fini(ptr);
})

ECS_MOVE(FlecsTexture, dst, src, {
    FlecsTexture_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsTexture, dst, src, {
    FlecsTexture_fini(dst);
    dst->path = src->path ? ecs_os_strdup(src->path) : NULL;
})

ECS_CTOR(FlecsPbrTextures, ptr, {
    ecs_os_zeromem(ptr);
})

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsRgba);
    ecs_struct(world, {
        .entity = ecs_id(FlecsRgba),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsPbrMaterial);
    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsEmissive);
    ecs_struct(world, {
        .entity = ecs_id(FlecsEmissive),
        .members = {
            { .name = "strength", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(FlecsRgba) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsEmissive), EcsOnInstantiate, EcsInherit);
    ECS_COMPONENT_DEFINE(world, FlecsMaterialId);
    ecs_struct(world, {
        .entity = ecs_id(FlecsMaterialId),
        .members = {
            { .name = "value", .type = ecs_id(ecs_u32_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsMaterialId), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsTexture);
    ecs_struct(world, {
        .entity = ecs_id(FlecsTexture),
        .members = {
            { .name = "path", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_set_hooks(world, FlecsTexture, {
        .ctor = ecs_ctor(FlecsTexture),
        .dtor = ecs_dtor(FlecsTexture),
        .move = ecs_move(FlecsTexture),
        .copy = ecs_copy(FlecsTexture)
    });

    ECS_COMPONENT_DEFINE(world, FlecsPbrTextures);
    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrTextures),
        .members = {
            { .name = "albedo", .type = ecs_id(ecs_entity_t) },
            { .name = "emissive", .type = ecs_id(ecs_entity_t) },
            { .name = "roughness", .type = ecs_id(ecs_entity_t) },
            { .name = "normal", .type = ecs_id(ecs_entity_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsPbrTextures), EcsOnInstantiate, EcsInherit);

    ecs_set_hooks(world, FlecsPbrTextures, {
        .ctor = ecs_ctor(FlecsPbrTextures)
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .parent = ecs_lookup(world, "flecs.engine.material"),
            .name = "MaterialIdOnAdd"
        }),
        .query.terms = {
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnAdd},
        .yield_existing = true,
        .callback = FlecsMaterialIdOnAdd
    });
}
