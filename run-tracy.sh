#!/bin/bash
cmake -S . -B build -DFLECS_ENGINE_TRACY=ON
cmake --build build
TRACY_DPI_SCALE=${TRACY_DPI_SCALE:-1.0} tracy-profiler &
./build/flecs_engine "$@"
