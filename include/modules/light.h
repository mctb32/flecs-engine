#ifndef FLECS_ENGINE_LIGHT_H
#define FLECS_ENGINE_LIGHT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_CAMERA_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_DECLARE(FlecsCameraController);

ECS_STRUCT(FlecsDirectionalLight, {
    float intensity;
});

#endif
