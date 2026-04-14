# Windows Porting Implementation Plan (V3)

This document updates the previous versions after validating the current repository on Windows and checking the `wgpu-native` release packaging in detail.

## Scope and Goal

The goal is to build and run the native `flecs-engine` fork on **Windows x64 with MSVC** without requiring `pkg-config`, `cargo`, `clang`, or a local `wgpu-native` source build.

This port is not just a surface-creation change. The current codebase also needs native-platform separation in CMake, proper integration of the prebuilt `wgpu-native` Windows package, and a few runtime capability decisions.

Initial scope:
- Windows x64 MSVC native builds.
- Release configuration as the primary validation target.
- Existing Apple and Emscripten paths remain intact.

Out of scope for this pass:
- Linux native surface support.
- MinGW-specific Windows support.
- Broad compatibility for DDS/BC-heavy scenes on GPUs that do not expose BC compression.

Difficulty: **Moderate**. The GLFW/WebGPU glue is straightforward, but the build-system and runtime compatibility work is larger than a simple `HWND` surface patch.

## Key Findings

- The current Windows configure path fails before compilation because `CMakeLists.txt` hard-requires `PkgConfig`.
- Native code is currently hardcoded to Cocoa/Metal and `GLFW_EXPOSE_NATIVE_COCOA`.
- The prebuilt `wgpu-native` Windows release for `v27.0.4.0` is usable for this repo. The `wgpu-windows-x86_64-msvc-release.zip` archive contains:
  - `include/webgpu/webgpu.h`
  - `include/webgpu/wgpu.h`
  - `lib/wgpu_native.dll`
  - `lib/wgpu_native.dll.lib`
  - `lib/wgpu_native.lib`
  - `lib/wgpu_native.pdb`
- BC texture support cannot be treated as universally optional, because the renderer loads `.dds` textures and some bundled content relies heavily on BC-compressed DDS assets.

## Proposed Changes

### Build System (`CMakeLists.txt`)

- Do not call `find_package(PkgConfig REQUIRED)` on Windows. Restrict `pkg-config` probing to native non-Windows builds.
- Split native platform handling explicitly into `APPLE`, `WIN32`, and `else()` branches. Do not let non-Emscripten builds fall through Apple-specific logic.
- Restrict `.m` source inclusion to `APPLE` only.
- On Windows x64 MSVC, bypass the source-build path for `wgpu-native` and download the prebuilt `wgpu-windows-x86_64-msvc-release.zip` archive for tag `v27.0.4.0`.
- Use `FetchContent_Populate` or an equivalent download/extract flow, not `FetchContent_MakeAvailable`, because the archive is a binary distribution rather than a CMake project.
- Pin the archive with `URL_HASH SHA256=f14ca334b4d253881bde2605bd147f332178d705f56fbd74f81458797c77fce1`.
- Create an imported native target using the extracted Windows binaries:
  - `IMPORTED_LOCATION` or `IMPORTED_LOCATION_RELEASE` -> `wgpu_native.dll`
  - `IMPORTED_IMPLIB` or `IMPORTED_IMPLIB_RELEASE` -> `wgpu_native.dll.lib`
- Add the extracted include directory to the build and make header resolution explicit. Prefer updating the repo to include `webgpu/webgpu.h` on native builds rather than depending on a fragile include-path workaround.
- Define `GLFW_EXPOSE_NATIVE_WIN32` on Windows and keep `GLFW_EXPOSE_NATIVE_COCOA` only on Apple.
- Add a post-build step that copies `wgpu_native.dll` next to the executable. Optionally copy `wgpu_native.pdb` when present.
- Fail fast with a clear CMake error for unsupported native platforms or unsupported Windows architectures instead of silently using Apple-oriented defaults.

#### [MODIFY] `CMakeLists.txt`
#### [MODIFY] `src/vendor.h`

---

### Platform Abstraction (`platform.c`)

- Add `#if defined(_WIN32)` surface creation using `WGPUSurfaceSourceWindowsHWND`.
- Use `GetModuleHandle(NULL)` for `hinstance` and `glfwGetWin32Window(window)` for `hwnd`.
- Keep the existing Metal-layer path for Apple only.
- Keep the Emscripten canvas path unchanged.
- Replace the current hardcoded `WGPUPresentMode_Immediate` assumption with a capability-based selection step:
  - if vsync is enabled, use `WGPUPresentMode_Fifo`;
  - if vsync is disabled, request `WGPUPresentMode_Immediate` only if it appears in `surface_caps.presentModes`;
  - otherwise fall back to `WGPUPresentMode_Fifo`.
- Do not index `surface_caps.alphaModes[0]` blindly. Use `WGPUCompositeAlphaMode_Auto` or a guarded fallback instead.
- When requesting the device, inspect adapter features first and request `WGPUFeatureName_TextureCompressionBC` only when the adapter actually exposes it.
- If BC compression is unavailable, log that DDS/BC-backed content may not work rather than implying full scene compatibility.
- Centralize present-mode selection in a helper so the same logic is reused during initial surface configuration and during later reconfiguration.

#### [MODIFY] `src/platform.c`

---

### Window Reconfiguration Path (`window.c`)

- Remove the direct assignment of `WGPUPresentMode_Immediate` during runtime vsync changes.
- Route vsync changes through the same capability-aware present-mode selection used during initial surface setup.
- This avoids a Windows-only regression where initial startup is correct but toggling vsync later reintroduces an unsupported present mode.

#### [MODIFY] `src/modules/engine/surfaces/window/window.c`

---

### Asset and Feature Compatibility

- Treat BC texture support as a content-compatibility issue, not only a device-creation issue.
- The engine currently loads `.dds` textures directly, and DDS-heavy scenes should not be used as the first proof that the Windows port is correct.
- For basic Windows bring-up, use simple scenes such as `empty.flecs` or `cube.flecs`.
- Use Bistro or other DDS/BC-heavy scenes only as secondary validation on hardware that reports BC texture compression support.
- If broad compatibility for no-BC adapters is required later, that is a separate asset or transcoding project and not part of this Windows porting pass.

## Verification Plan

### Automated Verification
1. Configure the project on Windows:
   ```cmd
   cmake -S . -B build-win
   ```
2. Build the Release configuration:
   ```cmd
   cmake --build build-win --config Release
   ```
3. Verify:
   - CMake does not require `pkg-config`, `cargo`, or `clang` on Windows.
   - No Objective-C or Cocoa-specific sources are compiled on Windows.
   - The project links against the imported Windows `wgpu-native` package.
   - `wgpu_native.dll` is copied into the executable output directory.

### Manual Verification
1. Launch the Release executable from the output directory.
2. Validate a simple scene first:
   - the window opens successfully;
   - the first frame renders;
   - resize works;
   - toggling vsync does not break presentation.
3. Validate a DDS/BC-heavy scene only on hardware that reports BC compression support.
4. If the app starts correctly but fails only on DDS-heavy assets, treat that as a BC/content limitation rather than a basic Windows port failure.

## Expected Problems and Risks

- Multi-config generators may need explicit `RELEASE` imported-target properties if the first pass only wires up the release archive.
- Older or restricted GPUs may not expose `TextureCompressionBC`.
- Out-of-date Windows graphics drivers can still block adapter, device, or surface initialization even after the code port is correct.
- This plan targets Windows x64 with MSVC first. MinGW and ARM64 are follow-up work unless explicitly added.
