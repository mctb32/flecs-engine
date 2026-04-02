#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);
ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
ECS_COMPONENT_DECLARE(FlecsEmissive);
ECS_COMPONENT_DECLARE(FlecsMaterialId);
ECS_COMPONENT_DECLARE(FlecsPbrTextures);
ECS_TAG_DECLARE(FlecsAlphaBlend);

ECS_DTOR(FlecsPbrTextures, ptr, {
    if (ptr->_bind_group) {
        wgpuBindGroupRelease(ptr->_bind_group);
    }
})

static void FlecsMaterialIdInit(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsEngineImpl *impl = ecs_singleton_ensure(world, FlecsEngineImpl);
    FlecsMaterialId *material_id = ecs_field(it, FlecsMaterialId, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        material_id[i].value = impl->materials.next_id;
        impl->materials.next_id ++;
    }
}

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMaterialId);
    ECS_COMPONENT_DEFINE(world, FlecsPbrMaterial);
    ECS_COMPONENT_DEFINE(world, FlecsPbrTextures);
    ECS_COMPONENT_DEFINE(world, FlecsRgba);
    ECS_COMPONENT_DEFINE(world, FlecsEmissive);
    ECS_TAG_DEFINE(world, FlecsAlphaBlend);

    ecs_set_hooks(world, FlecsPbrTextures, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsPbrTextures)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsMaterialId),
        .members = {
            { .name = "value", .type = ecs_id(ecs_u32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrTextures),
        .members = {
            { .name = "albedo", .type = ecs_id(ecs_entity_t) },
            { .name = "emissive", .type = ecs_id(ecs_entity_t) },
            { .name = "roughness", .type = ecs_id(ecs_entity_t) },
            { .name = "normal", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRgba),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsEmissive),
        .members = {
            { .name = "strength", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(FlecsRgba) }
        }
    });

    ecs_add_pair(world, ecs_id(FlecsMaterialId), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsPbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsEmissive), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, FlecsAlphaBlend, EcsOnInstantiate, EcsInherit);

    ecs_add_pair(world, ecs_id(FlecsRgba), EcsWith, ecs_id(FlecsMaterialId));
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsWith, ecs_id(FlecsMaterialId));
    ecs_add_pair(world, ecs_id(FlecsPbrTextures), EcsWith, ecs_id(FlecsMaterialId));

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .parent = ecs_lookup(world, "flecs.engine.material"),
            .name = "InitMaterial"
        }),
        .query.terms = {
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnAdd},
        .yield_existing = true,
        .callback = FlecsMaterialIdInit
    });
}
