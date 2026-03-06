#include "../engine.h"

bool flecsEngine_surfaceInterface_isValid(
    const FlecsEngineSurfaceInterface *ops)
{
    return ops != NULL &&
        ops->prepare_frame != NULL &&
        ops->acquire_frame != NULL &&
        ops->encode_frame != NULL &&
        ops->submit_frame != NULL &&
        ops->on_frame_failed != NULL &&
        ops->cleanup != NULL;
}

int flecsEngine_surfaceInterface_initInstance(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    const void *config)
{
    if (!impl) {
        return 0;
    }

    if (!impl->init_instance) {
        return 0;
    }

    return impl->init_instance(engine, config);
}

int flecsEngine_surfaceInterface_configureTarget(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine)
{
    if (!impl) {
        return 0;
    }

    if (!impl->configure_target) {
        return 0;
    }

    return impl->configure_target(engine);
}

int flecsEngine_surfaceInterface_prepareFrame(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    if (!impl) {
        return -1;
    }

    if (!impl->prepare_frame) {
        return -1;
    }

    return impl->prepare_frame(world, engine);
}

int flecsEngine_surfaceInterface_acquireFrame(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    FlecsEngineSurface *target)
{
    if (!impl) {
        return -1;
    }

    if (!impl->acquire_frame) {
        return -1;
    }

    return impl->acquire_frame(engine, target);
}

int flecsEngine_surfaceInterface_encodeFrame(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    if (!impl) {
        return -1;
    }

    if (!impl->encode_frame) {
        return -1;
    }

    return impl->encode_frame(engine, encoder, target);
}

int flecsEngine_surfaceInterface_submitFrame(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsEngineSurface *target)
{
    if (!impl) {
        return -1;
    }

    if (!impl->submit_frame) {
        return -1;
    }

    return impl->submit_frame(world, engine, target);
}

void flecsEngine_surfaceInterface_onFrameFailed(
    const FlecsEngineSurfaceInterface *impl,
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    if (!impl) {
        return;
    }

    if (!impl->on_frame_failed) {
        return;
    }

    impl->on_frame_failed(world, engine);
}

void flecsEngine_surfaceInterface_cleanup(
    const FlecsEngineSurfaceInterface *impl,
    FlecsEngineImpl *engine,
    bool terminate_runtime)
{
    if (!impl) {
        return;
    }

    if (!impl->cleanup) {
        return;
    }

    impl->cleanup(engine, terminate_runtime);
}
