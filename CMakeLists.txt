cmake_minimum_required(VERSION 3.20)
project(flecs_engine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FLECS_ENGINE_TRACY "Enable Tracy profiler integration" ON)
set(FLECS_ENGINE_TRACY_TAG "v0.13.1" CACHE STRING
  "Tracy git tag to fetch when profiler integration is enabled")

# Release build: highest optimization + link-time optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG" CACHE STRING "" FORCE)
  else()
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
  endif()

  if(NOT EMSCRIPTEN)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

include(FetchContent)

if(NOT EMSCRIPTEN)
  include(ExternalProject)
  if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WGPU QUIET wgpu-native)
  endif()
endif()

# -- GLFW (native only; emscripten uses -sUSE_GLFW=3) --
if(NOT EMSCRIPTEN)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.9
  )
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()

# -- cglm --
FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm.git
  GIT_TAG v0.9.2
)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cglm)

# -- flecs --
FetchContent_Declare(
  flecs
  GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
  GIT_TAG script_include
)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(flecs)
target_compile_definitions(flecs_static PUBLIC FLECS_SCRIPT_MATH)

# -- stb (header-only, no CMakeLists.txt) --
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 28d546d5eb77d4585506a20480f4de2e706dff4c
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(stb)

# -- tinyexr (we compile it ourselves) --
FetchContent_Declare(
  tinyexr
  GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
  GIT_TAG v1.0.9
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tinyexr)

# -- cgltf (header-only) --
FetchContent_Declare(
  cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG v1.15
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(cgltf)

# -- Tracy (optional profiling client) --
if(FLECS_ENGINE_TRACY AND NOT EMSCRIPTEN)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG ${FLECS_ENGINE_TRACY_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(tracy)
endif()

# -- Sources --
if(EMSCRIPTEN)
  # Exclude Objective-C (.m) files on emscripten
  file(GLOB_RECURSE FLECS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  )
elseif(APPLE)
  file(GLOB_RECURSE FLECS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
  )
else()
  file(GLOB_RECURSE FLECS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  )
endif()

# -- tinyexr implementation + miniz --
list(APPEND FLECS_ENGINE_SOURCES
  ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/deps.cc
)

add_executable(flecs_engine ${FLECS_ENGINE_SOURCES})
set_property(TARGET flecs_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

if(NOT EMSCRIPTEN)
  set_target_properties(flecs_engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set_target_properties(flecs_engine PROPERTIES
      DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()

  if(APPLE)
    target_compile_definitions(flecs_engine PRIVATE GLFW_EXPOSE_NATIVE_COCOA)
  elseif(WIN32)
    target_compile_definitions(flecs_engine PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
  else()
    message(FATAL_ERROR "Unsupported native platform. Only APPLE and WIN32 are currently supported for native builds.")
  endif()
endif()

# -- wgpu (native only; emscripten uses -sUSE_WEBGPU=1) --
if(NOT EMSCRIPTEN)
  if(WIN32)
    FetchContent_Declare(
      wgpu_native_prebuilt
      URL https://github.com/gfx-rs/wgpu-native/releases/download/v27.0.4.0/wgpu-windows-x86_64-msvc-release.zip
      URL_HASH SHA256=f14ca334b4d253881bde2605bd147f332178d705f56fbd74f81458797c77fce1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_SUBDIR __no_cmake_subdir
    )
    FetchContent_MakeAvailable(wgpu_native_prebuilt)

    set(WGPU_NATIVE_DIR ${wgpu_native_prebuilt_SOURCE_DIR})

    add_library(wgpu_native_lib SHARED IMPORTED)
    set_target_properties(wgpu_native_lib PROPERTIES
      IMPORTED_LOCATION ${WGPU_NATIVE_DIR}/lib/wgpu_native.dll
      IMPORTED_IMPLIB ${WGPU_NATIVE_DIR}/lib/wgpu_native.dll.lib
      IMPORTED_LOCATION_RELEASE ${WGPU_NATIVE_DIR}/lib/wgpu_native.dll
      IMPORTED_IMPLIB_RELEASE ${WGPU_NATIVE_DIR}/lib/wgpu_native.dll.lib
    )

    target_include_directories(flecs_engine PRIVATE ${WGPU_NATIVE_DIR}/include)
    target_link_libraries(flecs_engine PRIVATE wgpu_native_lib)

    add_custom_command(TARGET flecs_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WGPU_NATIVE_DIR}/lib/wgpu_native.dll"
        "$<TARGET_FILE_DIR:flecs_engine>"
    )
    if(EXISTS "${WGPU_NATIVE_DIR}/lib/wgpu_native.pdb")
      add_custom_command(TARGET flecs_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WGPU_NATIVE_DIR}/lib/wgpu_native.pdb"
        "$<TARGET_FILE_DIR:flecs_engine>"
      )
    endif()
  elseif(WGPU_FOUND)
    target_include_directories(flecs_engine PRIVATE ${WGPU_INCLUDE_DIRS})
    target_link_directories(flecs_engine PRIVATE ${WGPU_LIBRARY_DIRS})
    target_link_libraries(flecs_engine PRIVATE ${WGPU_LIBRARIES})
  else()
    set(WGPU_NATIVE_DIR ${CMAKE_BINARY_DIR}/_deps/wgpu_native-src)
    set(WGPU_NATIVE_LIB ${WGPU_NATIVE_DIR}/target/release/libwgpu_native.dylib)

    ExternalProject_Add(
      wgpu_native_ep
      GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native.git
      GIT_TAG v27.0.4.0
      UPDATE_COMMAND ""
      CONFIGURE_COMMAND ""
      BUILD_COMMAND cargo build --release
      BUILD_IN_SOURCE 1
      INSTALL_COMMAND ""
      SOURCE_DIR ${WGPU_NATIVE_DIR}
      BUILD_BYPRODUCTS ${WGPU_NATIVE_LIB}
    )

    add_library(wgpu_native_lib SHARED IMPORTED)
    set_target_properties(wgpu_native_lib PROPERTIES IMPORTED_LOCATION ${WGPU_NATIVE_LIB})
    add_dependencies(wgpu_native_lib wgpu_native_ep)

    target_include_directories(flecs_engine PRIVATE
      ${WGPU_NATIVE_DIR}/ffi
      ${WGPU_NATIVE_DIR}/ffi/webgpu-headers
    )
    target_link_libraries(flecs_engine PRIVATE wgpu_native_lib)
  endif()
endif()

target_include_directories(flecs_engine PUBLIC
  ${flecs_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(flecs_engine PRIVATE
  ${stb_SOURCE_DIR}
  ${tinyexr_SOURCE_DIR}
  ${tinyexr_SOURCE_DIR}/deps/miniz
  ${cgltf_SOURCE_DIR}
)

if(FLECS_ENGINE_TRACY AND NOT EMSCRIPTEN)
  target_link_libraries(flecs_engine PRIVATE Tracy::TracyClient)
  target_compile_definitions(flecs_engine PRIVATE TRACY_ENABLE FLECS_ENGINE_TRACY)
endif()

if(EMSCRIPTEN)
  target_link_libraries(flecs_engine PRIVATE cglm flecs_static)

  target_link_options(flecs_engine PRIVATE
    -sUSE_GLFW=3
    -sUSE_WEBGPU=1
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/etc/assets/scenes/cube.flecs@etc/assets/scenes/cube.flecs"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/etc/assets/scenes/empty.flecs@etc/assets/scenes/empty.flecs"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
  )

  set_target_properties(flecs_engine PROPERTIES
    SUFFIX ".html"
  )
else()
  target_link_libraries(flecs_engine PRIVATE glfw cglm flecs_static)

  if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(flecs_engine PRIVATE
      ${COCOA_FRAMEWORK}
      ${QUARTZCORE_FRAMEWORK}
    )
  endif()
endif()
