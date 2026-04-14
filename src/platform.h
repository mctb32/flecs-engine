#ifndef FLECS_ENGINE_PLATFORM_H
#define FLECS_ENGINE_PLATFORM_H

#include "types.h"

/* ---- GPU initialisation ---- */

/* Create a WebGPU instance with platform-specific native options. */
WGPUInstance flecsEngine_createInstance(void);

/* Request an adapter, blocking until the request completes.
   Returns NULL on failure. */
WGPUAdapter flecsEngine_requestAdapter(
    WGPUInstance instance,
    WGPUSurface surface);

/* Request a device, blocking until the request completes.
   Returns NULL on failure. */
WGPUDevice flecsEngine_requestDevice(
    WGPUAdapter adapter,
    WGPUInstance instance);

/* Install an uncaptured-error callback on the device.
   On native wgpu this is a no-op (errors surface through validation). */
void flecsEngine_setDeviceErrorCallback(
    WGPUDevice device);

/* ---- Async helpers ---- */

/* Spin until *done becomes true, pumping the appropriate event mechanism
   (emscripten_sleep on WASM, wgpuInstanceProcessEvents on native). */
void flecsEngine_processEventsUntilDone(
    WGPUInstance instance,
    bool *done);

/* Platform-agnostic buffer-map callback signature. */
typedef void (*FlecsCompatBufferMapCallback)(
    WGPUMapAsyncStatus status,
    const char *message,   /* may be NULL */
    void *userdata);

void flecsEngine_bufferMapAsync(
    WGPUBuffer buffer,
    WGPUMapMode mode,
    size_t offset,
    size_t size,
    FlecsCompatBufferMapCallback callback,
    void *userdata);

/* ---- Surface / swap-chain ---- */

WGPUSurface flecsEngine_createSurface(
    WGPUInstance instance,
    GLFWwindow *window);

/* Query and populate surf->surface_config, then configure the surface
   (native) or create a swap chain (emscripten).  Returns 0 on success. */
int flecsEngine_configureSurface(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *surf);

/* Choose a supported present mode, checking capabilities to see if Immediate is available when vsync is false. */
WGPUPresentMode flecsEngine_choosePresentMode(
    WGPUSurface surface,
    WGPUAdapter adapter,
    bool vsync);

/* Re-configure the surface / recreate the swap chain after a resize.
    Caller must have already updated surf->surface_config with the new
   width and height. */
void flecsEngine_reconfigureSurface(
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *surf);

/* Get the current framebuffer size in pixels.
   On emscripten this reads the canvas CSS size × devicePixelRatio. */
void flecsEngine_getFramebufferSize(
    GLFWwindow *window,
    int *width,
    int *height);

int flecsEngine_acquireFrame(
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *surf,
    FlecsEngineSurface *target);

/* Present the surface.  No-op on emscripten. */
void flecsEngine_presentSurface(
    WGPUSurface surface);

/* Release swap-chain resources.  No-op on native. */
void flecsEngine_releaseSwapChain(void);

#endif /* FLECS_ENGINE_PLATFORM_H */
