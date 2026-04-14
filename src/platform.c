#include "platform.h"
#include "webgpu_utils.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

/* ---- GPU initialisation ---- */

#ifdef __EMSCRIPTEN__

typedef struct {
    WGPUAdapter *adapter_out;
    bool done;
} FlecsAdapterRequest;

typedef struct {
    WGPUDevice *device_out;
    bool done;
} FlecsDeviceRequest;

static void flecsEngine_onRequestAdapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    char const *message,
    void *userdata)
{
    FlecsAdapterRequest *req = userdata;
    if (status == WGPURequestAdapterStatus_Success) {
        *req->adapter_out = adapter;
    } else {
        ecs_err("Adapter request failed: %s",
            message ? message : "unknown");
    }
    req->done = true;
}

static void flecsEngine_onRequestDevice(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    char const *message,
    void *userdata)
{
    FlecsDeviceRequest *req = userdata;
    if (status == WGPURequestDeviceStatus_Success) {
        *req->device_out = device;
    } else {
        ecs_err("Device request failed: %s",
            message ? message : "unknown");
    }
    req->done = true;
}

#endif /* __EMSCRIPTEN__ */

static void flecsEngine_onDeviceError(
    WGPUErrorType type,
    char const *message,
    void *userdata)
{
    (void)userdata;
    const char *type_str = "unknown";
    switch (type) {
    case WGPUErrorType_Validation: type_str = "validation"; break;
    case WGPUErrorType_OutOfMemory: type_str = "out of memory"; break;
    default: break;
    }
    ecs_err("[WebGPU] %s error: %s", type_str, message ? message : "");
}

#ifndef __EMSCRIPTEN__

static int flecsEngine_stringViewLength(
    WGPUStringView message)
{
    if (!message.data) {
        return 0;
    }

    if (message.length == WGPU_STRLEN) {
        size_t length = strlen(message.data);
        return length > INT_MAX ? INT_MAX : (int)length;
    }

    return message.length > INT_MAX ? INT_MAX : (int)message.length;
}

static void flecsEngine_onDeviceErrorNative(
    WGPUDevice const * device,
    WGPUErrorType type,
    WGPUStringView message,
    void* userdata1,
    void* userdata2)
{
    (void)device;
    (void)userdata1;
    (void)userdata2;
    const char *type_str = "unknown";
    switch (type) {
    case WGPUErrorType_Validation: type_str = "validation"; break;
    case WGPUErrorType_OutOfMemory: type_str = "out of memory"; break;
    default: break;
    }
    const char *msg = "";
    if (message.data) {
        msg = message.data;
    }
    int msg_len = flecsEngine_stringViewLength(message);
    ecs_err("[WebGPU] %s error: %.*s", type_str, msg_len, msg);
}

static void flecsEngine_waitForFuture(
    WGPUInstance instance,
    WGPUFuture future,
    bool *done)
{
    WGPUFutureWaitInfo wait_info = { .future = future };
    while (!*done) {
        wait_info.completed = false;
        wgpuInstanceWaitAny(instance, 1, &wait_info, 0);
    }
}

static void flecsEngine_onRequestAdapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    WGPUAdapter *adapter_out = userdata1;
    bool *future_cond = userdata2;

    if (status == WGPURequestAdapterStatus_Success) {
        *adapter_out = adapter;
    } else {
        if (message.data) {
            ecs_err("Adapter request failed: %.*s",
                flecsEngine_stringViewLength(message), message.data);
        } else {
            ecs_err("Adapter request failed: unknown");
        }
    }

    *future_cond = true;
}

static void flecsEngine_onRequestDevice(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    WGPUDevice *device_out = userdata1;
    bool *future_cond = userdata2;

    if (status == WGPURequestDeviceStatus_Success) {
        *device_out = device;
    } else {
        if (message.data) {
            ecs_err("Device request failed: %.*s",
                flecsEngine_stringViewLength(message), message.data);
        } else {
            ecs_err("Device request failed: unknown");
        }
    }

    *future_cond = true;
}

#endif /* __EMSCRIPTEN__ */

WGPUInstance flecsEngine_createInstance(void)
{
    WGPUInstanceDescriptor instance_desc = {0};

#ifndef __EMSCRIPTEN__
    WGPUInstanceExtras instance_extras = {
        .chain = {
            .sType = (WGPUSType)WGPUSType_InstanceExtras
        },
        .backends = WGPUInstanceBackend_Primary,
#ifdef _WIN32
        .dx12PresentationSystem = WGPUDx12SwapchainKind_DxgiFromHwnd
#endif
    };

    instance_desc.nextInChain = (WGPUChainedStruct*)&instance_extras;
#endif

    return wgpuCreateInstance(&instance_desc);
}

WGPUAdapter flecsEngine_requestAdapter(
    WGPUInstance instance,
    WGPUSurface surface)
{
    WGPUAdapter adapter = NULL;

    WGPURequestAdapterOptions options = {
        .compatibleSurface = surface
    };

#ifdef __EMSCRIPTEN__
    FlecsAdapterRequest req = {
        .adapter_out = &adapter,
        .done = false
    };

    wgpuInstanceRequestAdapter(
        instance, &options,
        flecsEngine_onRequestAdapter, &req);

    while (!req.done) {
        emscripten_sleep(1);
    }
#else
    bool done = false;

    WGPURequestAdapterCallbackInfo callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestAdapter,
        .userdata1 = &adapter,
        .userdata2 = &done
    };

    WGPUFuture future = wgpuInstanceRequestAdapter(
        instance, &options, callback);

    flecsEngine_waitForFuture(instance, future, &done);
#endif

    return adapter;
}

WGPUDevice flecsEngine_requestDevice(
    WGPUAdapter adapter,
    WGPUInstance instance)
{
    WGPUDevice device = NULL;

#ifndef __EMSCRIPTEN__
    WGPUDeviceDescriptor desc = {0};
    desc.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){
        .callback = flecsEngine_onDeviceErrorNative,
        .userdata1 = NULL,
        .userdata2 = NULL
    };
    WGPUFeatureName required_features[2];
    size_t required_feature_count = 0;

    if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TextureCompressionBC)) {
        required_features[required_feature_count ++] =
            WGPUFeatureName_TextureCompressionBC;
    } else {
        ecs_err("WebGPU adapter does not support TextureCompressionBC! BC-based textures will fail to load.");
    }

    if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TimestampQuery)) {
        required_features[required_feature_count ++] =
            WGPUFeatureName_TimestampQuery;
    }

    if (required_feature_count > 0) {
        desc.requiredFeatures = required_features;
        desc.requiredFeatureCount = required_feature_count;
    }
#else
    WGPUDeviceDescriptor desc = {0};
#endif

#ifdef __EMSCRIPTEN__
    (void)instance;

    FlecsDeviceRequest req = {
        .device_out = &device,
        .done = false
    };

    wgpuAdapterRequestDevice(
        adapter, &desc,
        flecsEngine_onRequestDevice, &req);

    while (!req.done) {
        emscripten_sleep(1);
    }
#else
    bool done = false;

    WGPURequestDeviceCallbackInfo callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestDevice,
        .userdata1 = &device,
        .userdata2 = &done
    };

    WGPUFuture future = wgpuAdapterRequestDevice(
        adapter, &desc, callback);

    flecsEngine_waitForFuture(instance, future, &done);
#endif

    return device;
}

void flecsEngine_setDeviceErrorCallback(
    WGPUDevice device)
{
#ifdef __EMSCRIPTEN__
    wgpuDeviceSetUncapturedErrorCallback(device,
        (WGPUErrorCallback)flecsEngine_onDeviceError, NULL);
#else
    (void)device;
#endif
}

/* ---- Async helpers ---- */

void flecsEngine_processEventsUntilDone(
    WGPUInstance instance,
    bool *done)
{
    while (!*done) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#else
        wgpuInstanceProcessEvents(instance);
#endif
    }
    (void)instance;
}

#ifdef __EMSCRIPTEN__

typedef struct {
    FlecsCompatBufferMapCallback callback;
    void *userdata;
} FlecsCompatBufferMapCtx;

static void flecsEngine_onBufferMap(
    WGPUBufferMapAsyncStatus status,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = userdata;
    FlecsCompatBufferMapCallback cb = ctx->callback;
    void *ud = ctx->userdata;
    ecs_os_free(ctx);
    cb(status, NULL, ud);
}

void flecsEngine_bufferMapAsync(
    WGPUBuffer buffer,
    WGPUMapMode mode,
    size_t offset,
    size_t size,
    FlecsCompatBufferMapCallback callback,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = ecs_os_malloc_t(FlecsCompatBufferMapCtx);
    ctx->callback = callback;
    ctx->userdata = userdata;

    wgpuBufferMapAsync(buffer, mode, offset, size,
        flecsEngine_onBufferMap, ctx);
}

#else /* native */

typedef struct {
    FlecsCompatBufferMapCallback callback;
    void *userdata;
} FlecsCompatBufferMapCtx;

static void flecsEngine_onBufferMap(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata2;

    FlecsCompatBufferMapCtx *ctx = userdata1;
    const char *msg = (message.data && message.length > 0) ? message.data : NULL;
    FlecsCompatBufferMapCallback cb = ctx->callback;
    void *ud = ctx->userdata;
    ecs_os_free(ctx);
    cb(status, msg, ud);
}

void flecsEngine_bufferMapAsync(
    WGPUBuffer buffer,
    WGPUMapMode mode,
    size_t offset,
    size_t size,
    FlecsCompatBufferMapCallback callback,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = ecs_os_malloc_t(FlecsCompatBufferMapCtx);
    ctx->callback = callback;
    ctx->userdata = userdata;

    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = flecsEngine_onBufferMap,
        .userdata1 = ctx,
        .userdata2 = NULL
    };

    wgpuBufferMapAsync(buffer, mode, offset, size, info);
}

#endif /* __EMSCRIPTEN__ */

/* ---- Surface / swap-chain ---- */

#ifdef __EMSCRIPTEN__
static WGPUSwapChain compat_swap_chain;
#else
extern void *flecs_create_metal_layer(void *ns_window);

static bool flecsEngine_isInvalidWgpuHandle(
    const void *handle)
{
    return !handle || (uintptr_t)handle == UINTPTR_MAX;
}

static WGPUPresentMode flecsEngine_selectPresentMode(
    const WGPUSurfaceCapabilities *surface_caps,
    bool vsync)
{
    WGPUPresentMode target_mode = vsync
        ? WGPUPresentMode_Fifo
        : WGPUPresentMode_Immediate;

    for (size_t i = 0; i < surface_caps->presentModeCount; ++i) {
        if (surface_caps->presentModes[i] == target_mode) {
            return target_mode;
        }
    }

    if (target_mode == WGPUPresentMode_Immediate) {
        return WGPUPresentMode_Fifo;
    }

    return WGPUPresentMode_Fifo;
}
#endif

WGPUSurface flecsEngine_createSurface(
    WGPUInstance instance,
    GLFWwindow *window)
{
    WGPUSurface surface = NULL;

#ifdef __EMSCRIPTEN__
    (void)window;

    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvas_desc = {
        .chain = {
            .sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector
        },
        .selector = "#canvas"
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&canvas_desc
    };
    surface = wgpuInstanceCreateSurface(instance, &surface_desc);
#elif defined(GLFW_EXPOSE_NATIVE_WIN32)
    void *hinstance = GetModuleHandle(NULL);
    void *hwnd = glfwGetWin32Window(window);

    if (!hinstance || !hwnd) {
        ecs_err("Failed to get native Win32 window handle");
        return NULL;
    }

    WGPUSurfaceSourceWindowsHWND windows_desc = {
        .chain = { .sType = WGPUSType_SurfaceSourceWindowsHWND },
        .hinstance = hinstance,
        .hwnd = hwnd
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&windows_desc
    };
    surface = wgpuInstanceCreateSurface(instance, &surface_desc);
#elif defined(GLFW_EXPOSE_NATIVE_COCOA)
    void *metal_layer = flecs_create_metal_layer(
        glfwGetCocoaWindow(window));

    WGPUSurfaceSourceMetalLayer metal_desc = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = metal_layer
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&metal_desc
    };
    surface = wgpuInstanceCreateSurface(instance, &surface_desc);
#else
    (void)instance;
    (void)window;
    ecs_err("Unsupported native platform for surface creation");
    return NULL;
#endif
#ifndef __EMSCRIPTEN__
    if (flecsEngine_isInvalidWgpuHandle(surface)) {
        ecs_err("Failed to create native wgpu surface");
        return NULL;
    }
#endif
    return surface;
}

WGPUPresentMode flecsEngine_choosePresentMode(
    WGPUSurface surface,
    WGPUAdapter adapter,
    bool vsync)
{
#ifdef __EMSCRIPTEN__
    (void)surface;
    (void)adapter;
    return vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
#else
    if (flecsEngine_isInvalidWgpuHandle(surface) ||
        flecsEngine_isInvalidWgpuHandle(adapter))
    {
        return vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
    }

    WGPUSurfaceCapabilities surface_caps = {0};
    WGPUPresentMode target_mode = vsync
        ? WGPUPresentMode_Fifo
        : WGPUPresentMode_Immediate;

    if (wgpuSurfaceGetCapabilities(surface, adapter, &surface_caps) == WGPUStatus_Success) {
        target_mode = flecsEngine_selectPresentMode(&surface_caps, vsync);
        wgpuSurfaceCapabilitiesFreeMembers(surface_caps);
    }
    return target_mode;
#endif
}

int flecsEngine_configureSurface(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *wnd)
{
    const FlecsSurface *surface = ecs_get(world, impl->surface, FlecsSurface);
    if (!surface) {
        ecs_err("Failed to get surface configuration");
        return -1;
    }

#ifdef __EMSCRIPTEN__
    WGPUPresentMode present_mode = flecsEngine_choosePresentMode(
        wnd->wgpu_surface, impl->adapter, wnd->vsync);

    WGPUTextureFormat preferred =
        wgpuSurfaceGetPreferredFormat(wnd->wgpu_surface, impl->adapter);

    wnd->surface_config = (WGPUSurfaceConfiguration){
        .device = impl->device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = preferred,
        .width = (uint32_t)surface->width,
        .height = (uint32_t)surface->height,
        .presentMode = present_mode,
    };

    WGPUSwapChainDescriptor sc_desc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = preferred,
        .width = (uint32_t)surface->width,
        .height = (uint32_t)surface->height,
        .presentMode = present_mode,
    };

    compat_swap_chain =
        wgpuDeviceCreateSwapChain(impl->device, wnd->wgpu_surface, &sc_desc);
    if (!compat_swap_chain) {
        ecs_err("Failed to create swap chain");
        return -1;
    }
#else
    if (flecsEngine_isInvalidWgpuHandle(wnd->wgpu_surface) ||
        flecsEngine_isInvalidWgpuHandle(impl->adapter))
    {
        ecs_err("Cannot configure invalid wgpu surface or adapter");
        return -1;
    }

    WGPUSurfaceCapabilities surface_caps = {0};
    if (wgpuSurfaceGetCapabilities(
        wnd->wgpu_surface,
        impl->adapter,
        &surface_caps) != WGPUStatus_Success ||
        surface_caps.formatCount == 0)
    {
        ecs_err("Failed to get surface capabilities");
        wgpuSurfaceCapabilitiesFreeMembers(surface_caps);
        return -1;
    }

    WGPUPresentMode present_mode =
        flecsEngine_selectPresentMode(&surface_caps, wnd->vsync);

    WGPUCompositeAlphaMode alpha_mode = WGPUCompositeAlphaMode_Auto;
    if (surface_caps.alphaModeCount > 0) {
        bool has_opaque = false;
        bool has_auto = false;
        for (size_t i = 0; i < surface_caps.alphaModeCount; ++i) {
            if (surface_caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) has_opaque = true;
            if (surface_caps.alphaModes[i] == WGPUCompositeAlphaMode_Auto) has_auto = true;
        }
        if (has_opaque) {
            alpha_mode = WGPUCompositeAlphaMode_Opaque;
        } else if (has_auto) {
            alpha_mode = WGPUCompositeAlphaMode_Auto;
        } else {
            alpha_mode = surface_caps.alphaModes[0];
        }
    }

    wnd->surface_config = (WGPUSurfaceConfiguration){
        .device = impl->device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = surface_caps.formats[0],
        .width = (uint32_t)surface->width,
        .height = (uint32_t)surface->height,
        .presentMode = present_mode,
        .alphaMode = alpha_mode
    };

    wgpuSurfaceConfigure(wnd->wgpu_surface, &wnd->surface_config);
    wgpuSurfaceCapabilitiesFreeMembers(surface_caps);
#endif

    return 0;
}

void flecsEngine_reconfigureSurface(
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *wnd)
{
#ifdef __EMSCRIPTEN__
    if (compat_swap_chain) {
        wgpuSwapChainRelease(compat_swap_chain);
    }

    WGPUSwapChainDescriptor sc_desc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = wnd->surface_config.format,
        .width = wnd->surface_config.width,
        .height = wnd->surface_config.height,
        .presentMode = wnd->surface_config.presentMode,
    };

    compat_swap_chain =
        wgpuDeviceCreateSwapChain(impl->device, wnd->wgpu_surface, &sc_desc);
#else
    (void)impl;
    wgpuSurfaceConfigure(wnd->wgpu_surface, &wnd->surface_config);
#endif
}

void flecsEngine_getFramebufferSize(
    GLFWwindow *window,
    int *width,
    int *height)
{
#ifdef __EMSCRIPTEN__
    (void)window;

    double css_w, css_h;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    *width = (int)(css_w * dpr);
    *height = (int)(css_h * dpr);
#else
    glfwGetFramebufferSize(window, width, height);
#endif
}

int flecsEngine_acquireFrame(
    FlecsEngineImpl *impl,
    FlecsSurfaceImpl *wnd,
    FlecsEngineSurface *target)
{
    (void)impl;
#ifdef __EMSCRIPTEN__
    (void)wnd;
    WGPUTextureView view =
        wgpuSwapChainGetCurrentTextureView(compat_swap_chain);
    if (!view) {
        ecs_err("Failed to get current swap chain texture view");
        return -1;
    }

    target->view_texture = view;
    target->owns_view_texture = true;
#else
    WGPUSurfaceTexture surface_frame = {0};
    wgpuSurfaceGetCurrentTexture(wnd->wgpu_surface, &surface_frame);

    target->surface_status = surface_frame.status;
    if (surface_frame.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_frame.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (surface_frame.texture) {
            wgpuTextureRelease(surface_frame.texture);
        }

        wgpuSurfaceConfigure(wnd->wgpu_surface, &wnd->surface_config);
        return 1;
    }

    target->surface_texture = surface_frame.texture;
    target->view_texture = wgpuTextureCreateView(target->surface_texture, NULL);
    target->owns_view_texture = true;
    if (!target->view_texture) {
        ecs_err("Failed to create color texture view");
        wgpuTextureRelease(target->surface_texture);
        target->surface_texture = NULL;
        return -1;
    }
#endif

    return 0;
}

void flecsEngine_presentSurface(
    WGPUSurface surface)
{
#ifdef __EMSCRIPTEN__
    (void)surface;
#else
    wgpuSurfacePresent(surface);
#endif
}

void flecsEngine_releaseSwapChain(void)
{
#ifdef __EMSCRIPTEN__
    FLECS_WGPU_RELEASE(compat_swap_chain, wgpuSwapChainRelease);
#endif
}

