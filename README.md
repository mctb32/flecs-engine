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

### Geometry
- Primitive shapes:
  - Quad
  - Box
  - Triangle
  - TrianglePrism
  - RightTriangle
  - RightTrianglePrism
- Primitive meshes (parameterized mesh cache):
  - Cone
  - Cylinder
  - Sphere
  - IcoSphere
  - HemiSphere
  - NGon
- Meshes
- Instanced rendering

### Assets
- glTF loader
- PNG loader
- DDS loader
- HDR / EXR loader (for HDRI)

### Materials
- Metallic
- Roughness
- Cubemap based reflections
- Emissive
- Transmissive (rough/smooth objects)
- PBR textures
- Per-instance materials
- Shared materials

### Lighting
- Directional light
- Point lights
- Spot lights
- Clustered light rendering
- Cascading shadow maps
- Image based lighting

### Atmosphere
- Dynamic atmosphere
- Dynamic atmosphere IBL

### Effects
- Bloom
- SSAO
- Height fog (flat color & from atmosphere)
- Screen space sun shafts
- Tony McMapFace tone mapping

### Misc
- Input handling
- Camera controller
- Time of day system
- Movement systems
- Tracy profiling
- Image-based rendering regression testing
- MSAA
- CPU frustum culling
- Render to image

## Dependencies
- cglm
- cgltf
- glfw
- stb_image
- tinyexr
- tracy

## Assets used
- [Kronos sample assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
- [Niagra bistro](https://github.com/zeux/niagara_bistro)
- [Kenney](https://kenney.nl/)

## Screenshots
<img width="1392" height="940" alt="Screenshot 2026-03-07 at 2 00 34 PM" src="https://github.com/user-attachments/assets/4cf79f25-273c-4c50-970a-ae89603f6664" />
<img width="1392" height="940" alt="Screenshot 2026-03-07 at 2 00 26 PM" src="https://github.com/user-attachments/assets/9fd58e3b-b369-4558-89ca-c21472931bf2" />
<img width="1200" height="800" alt="spheres" src="https://github.com/user-attachments/assets/6061c56d-e708-4d28-84ed-91fd2c2739a9" />
<img width="1392" height="940" alt="Screenshot 2026-03-07 at 2 01 20 PM" src="https://github.com/user-attachments/assets/68b37da2-ae06-4444-8957-4e3ee2c296e4" />
<img width="1392" height="940" alt="Screenshot 2026-03-07 at 2 04 15 PM" src="https://github.com/user-attachments/assets/4cd69a31-60ba-479a-a0bd-5c71ddfe94b4" />
