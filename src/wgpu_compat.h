/* Compatibility header bridging wgpu-native v27 API and emscripten's WebGPU. */
#ifndef FLECS_ENGINE_WGPU_COMPAT_H
#define FLECS_ENGINE_WGPU_COMPAT_H

/* WGPURenderPassColorAttachment.depthSlice — not present in emscripten's
   WebGPU header.  Place this macro inside a designated initializer to
   emit the field on native and nothing on emscripten. */
#ifdef __EMSCRIPTEN__
#define WGPU_DEPTH_SLICE
#else
#define WGPU_DEPTH_SLICE .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
#endif

/* WGPUTextureViewDescriptor.usage — not present in emscripten's header.
   Usage: WGPU_COMPAT_TEXTURE_VIEW_USAGE(WGPUTextureUsage_TextureBinding) */
#ifdef __EMSCRIPTEN__
#define WGPU_TEXTURE_VIEW_USAGE(u)
#else
#define WGPU_TEXTURE_VIEW_USAGE(u) .usage = (u),
#endif

#ifdef __EMSCRIPTEN__

/* ---- Type renames (wgpu-native v27 name → emscripten name) ---- */

/* Shader source */
typedef WGPUShaderModuleWGSLDescriptor WGPUShaderSourceWGSL;
#define WGPUSType_ShaderSourceWGSL WGPUSType_ShaderModuleWGSLDescriptor

/* Texel copy (renamed in newer spec) */
typedef WGPUImageCopyTexture WGPUTexelCopyTextureInfo;
typedef WGPUImageCopyBuffer  WGPUTexelCopyBufferInfo;
typedef WGPUTextureDataLayout WGPUTexelCopyBufferLayout;

/* Buffer map status */
typedef WGPUBufferMapAsyncStatus WGPUMapAsyncStatus;
#define WGPUMapAsyncStatus_Success WGPUBufferMapAsyncStatus_Success
#define WGPUMapAsyncStatus_Unknown WGPUBufferMapAsyncStatus_Unknown

/* ---- WGPUStringView compat ---- */

/* wgpu-native v27 uses WGPUStringView for entryPoint / shader code.
   Emscripten uses plain const char *. We define WGPUStringView so the
   struct literal compiles, and WGPU_STR() so call sites compile on both
   platforms.  The struct is never passed to emscripten APIs directly —
   only .data is used via the macros below. */
typedef struct WGPUStringView {
    char const *data;
    size_t length;
} WGPUStringView;
#define WGPU_STRLEN SIZE_MAX

/* Macro to initialise an entryPoint / code field.
   On wgpu-native the field IS a WGPUStringView.
   On emscripten the field is const char *, so we emit just the pointer. */
#define WGPU_STR(s) (s)

/* Shader source code field — emscripten struct uses `const char *code` */
#define WGPU_SHADER_CODE(s) (s)

/* ---- Multisample mask compat ---- */

/* wgpu-native defaults WGPUMultisampleState.mask to 0xFFFFFFFF when
   zero-initialized via designated initializers. Emscripten's webgpu.h
   does not apply defaults, so mask=0 disables all sample writes.
   Provide a macro so `.multisample = WGPU_MULTISAMPLE_DEFAULT` works. */
#define WGPU_MULTISAMPLE_DEFAULT { .count = 1, .mask = 0xFFFFFFFF }
#define WGPU_MULTISAMPLE(n) { .count = (n), .mask = 0xFFFFFFFF }

/* ---- depthSlice compat ---- */

/* Emscripten's WGPURenderPassColorAttachment does not have depthSlice.
   We define the constant so references compile, but the field itself
   must be guarded with #ifndef __EMSCRIPTEN__ at each call site. */
#define WGPU_DEPTH_SLICE_UNDEFINED 0

/* ---- WGPUOptionalBool compat ---- */

/* Emscripten's DepthStencilState uses plain bool for depthWriteEnabled,
   while wgpu-native v27 uses WGPUOptionalBool. */
typedef enum WGPUOptionalBool {
    WGPUOptionalBool_False = 0,
    WGPUOptionalBool_True = 1,
    WGPUOptionalBool_Undefined = 2,
} WGPUOptionalBool;

/* ---- Surface / SwapChain compat ---- */

/* Emscripten doesn't have the new Surface configure / present API.
   The window surface implementation handles this with #ifdef blocks,
   but the FlecsEngineSurface struct references this enum. */
typedef enum WGPUSurfaceGetCurrentTextureStatus {
    WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal = 0,
    WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal = 1,
    WGPUSurfaceGetCurrentTextureStatus_Error = 2,
} WGPUSurfaceGetCurrentTextureStatus;

/* WGPUSurfaceConfiguration is used in FlecsEngineImpl even on emscripten
   (to store width/height/format). Define it so the struct compiles. */
typedef struct WGPUSurfaceConfiguration {
    WGPUDevice device;
    WGPUTextureUsageFlags usage;
    WGPUTextureFormat format;
    uint32_t width;
    uint32_t height;
    WGPUPresentMode presentMode;
    /* alphaMode not available in emscripten SwapChain API */
} WGPUSurfaceConfiguration;

#else /* native / wgpu-native v27 */

#define WGPU_MULTISAMPLE_DEFAULT { .count = 1 }
#define WGPU_MULTISAMPLE(n) { .count = (n) }

/* On native, WGPUStringView already exists. These macros build the
   struct literal that wgpu-native expects. */
#define WGPU_STR(s) \
    (WGPUStringView){ .data = (s), .length = WGPU_STRLEN }

/* Shader code — native uses WGPUStringView for .code as well */
#define WGPU_SHADER_CODE(s) \
    (WGPUStringView){ .data = (s), .length = WGPU_STRLEN }

#endif /* __EMSCRIPTEN__ */

#endif /* FLECS_ENGINE_WGPU_COMPAT_H */
