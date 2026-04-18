#ifndef FLECS_ENGINE_VENDOR_H
#define FLECS_ENGINE_VENDOR_H

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
  #include <emscripten/html5.h>
  #include <GLFW/glfw3.h>
  #include <webgpu/webgpu.h>
#else
  #include <GLFW/glfw3.h>
  #include <GLFW/glfw3native.h>
  #ifdef _WIN32
    #include <webgpu/webgpu.h>
    #include <webgpu/wgpu.h>
  #else
    #include <webgpu.h>
  #endif
#ifdef _WIN32
  #ifdef interface
    #undef interface
  #endif
  #undef near
  #undef far
#endif

#endif

#include <flecs.h>
#include "wgpu_compat.h"

#endif
