#ifndef FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL \
    "struct GpuMaterial {\n" \
    "  color : u32,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive_strength : f32,\n" \
    "  emissive_color : u32\n" \
    "};\n" \
    "@group(0) @binding(1) var<storage, read> materials : array<GpuMaterial>;\n"

#endif
