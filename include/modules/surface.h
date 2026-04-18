#ifndef FLECS_ENGINE_SURFACE_H
#define FLECS_ENGINE_SURFACE_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SURFACE_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsSurface, {
    int32_t width;
    int32_t height;
    int32_t resolution_scale;
    int32_t actual_width;
    int32_t actual_height;
    bool msaa;
    bool vsync;
    const char *title;
    const char *write_to_file;
});

#endif
