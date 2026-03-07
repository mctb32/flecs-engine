# Flecs engine
A fast, portable, low footprint, opinionated (but hackable), flecs native game engine.

The project is still WIP and currently only works on MacOS.

## Usage
Build & run the engine:
```sh
cmake -S . -B build
cmake --build build
./build/flecs_engine
```

## Why should I use this?
You should probably not use this, unless:
- you want to quickly prototype ideas
- you want to build an engine but not start from 0
- you're OK with the limitations of the engine

## Features
- Primitive shapes
- Meshes
- Movement systems
- Instancing
- PBR materials
- Directional light
- Image based lighting
- Skybox
- Bloom
- Height based fog
- Tony McMapFace tone mapping
- Infinite grid
- Configurable engine setup
- Rendering to image
