#ifndef FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL \
    "@group(1) @binding(0) var ibl_prefiltered_env : texture_cube<f32>;\n" \
    "@group(1) @binding(1) var ibl_sampler : sampler;\n" \
    "@group(1) @binding(2) var ibl_brdf_lut : texture_2d<f32>;\n"

#endif
