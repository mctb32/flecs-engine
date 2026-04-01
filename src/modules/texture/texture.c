#include "texture.h"

ECS_COMPONENT_DECLARE(FlecsTexture);

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

void FlecsEngineTextureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTexture);

    ECS_COMPONENT_DEFINE(world, FlecsTexture);

    ecs_set_hooks(world, FlecsTexture, {
        .ctor = ecs_ctor(FlecsTexture),
        .dtor = ecs_dtor(FlecsTexture),
        .move = ecs_move(FlecsTexture),
        .copy = ecs_copy(FlecsTexture)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTexture),
        .members = {
            { .name = "path", .type = ecs_id(ecs_string_t) }
        }
    });
}
