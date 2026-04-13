#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL_H

/* PBR texture array bindings and per-channel sample helpers.
 * 12 array textures = 4 channels (albedo, emissive, MR, normal) x 3 size
 * buckets (512 / 1024 / 2048), plus a filtering sampler at binding 12.
 * The bucket index per material lives in GpuMaterial.texture_bucket; the
 * switch on `bucket` is non-uniform across fragments, so derivatives must
 * be lifted out of the switch and passed in via textureSampleGrad. */
#define FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL \
    "@group(1) @binding(0)  var albedo_tex_512    : texture_2d_array<f32>;\n" \
    "@group(1) @binding(1)  var albedo_tex_1024   : texture_2d_array<f32>;\n" \
    "@group(1) @binding(2)  var albedo_tex_2048   : texture_2d_array<f32>;\n" \
    "@group(1) @binding(3)  var emissive_tex_512  : texture_2d_array<f32>;\n" \
    "@group(1) @binding(4)  var emissive_tex_1024 : texture_2d_array<f32>;\n" \
    "@group(1) @binding(5)  var emissive_tex_2048 : texture_2d_array<f32>;\n" \
    "@group(1) @binding(6)  var roughness_tex_512  : texture_2d_array<f32>;\n" \
    "@group(1) @binding(7)  var roughness_tex_1024 : texture_2d_array<f32>;\n" \
    "@group(1) @binding(8)  var roughness_tex_2048 : texture_2d_array<f32>;\n" \
    "@group(1) @binding(9)  var normal_tex_512    : texture_2d_array<f32>;\n" \
    "@group(1) @binding(10) var normal_tex_1024   : texture_2d_array<f32>;\n" \
    "@group(1) @binding(11) var normal_tex_2048   : texture_2d_array<f32>;\n" \
    "@group(1) @binding(12) var tex_sampler : sampler;\n" \
    "fn sample_albedo(uv : vec2<f32>, layer : u32, bucket : u32,\n" \
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  switch (bucket) {\n" \
    "    case 0u: { return textureSampleGrad(albedo_tex_512,  tex_sampler, uv, layer, dx, dy); }\n" \
    "    case 1u: { return textureSampleGrad(albedo_tex_1024, tex_sampler, uv, layer, dx, dy); }\n" \
    "    default: { return textureSampleGrad(albedo_tex_2048, tex_sampler, uv, layer, dx, dy); }\n" \
    "  }\n" \
    "}\n" \
    "fn sample_emissive(uv : vec2<f32>, layer : u32, bucket : u32,\n" \
    "                   dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  switch (bucket) {\n" \
    "    case 0u: { return textureSampleGrad(emissive_tex_512,  tex_sampler, uv, layer, dx, dy); }\n" \
    "    case 1u: { return textureSampleGrad(emissive_tex_1024, tex_sampler, uv, layer, dx, dy); }\n" \
    "    default: { return textureSampleGrad(emissive_tex_2048, tex_sampler, uv, layer, dx, dy); }\n" \
    "  }\n" \
    "}\n" \
    "fn sample_roughness(uv : vec2<f32>, layer : u32, bucket : u32,\n" \
    "                    dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  switch (bucket) {\n" \
    "    case 0u: { return textureSampleGrad(roughness_tex_512,  tex_sampler, uv, layer, dx, dy); }\n" \
    "    case 1u: { return textureSampleGrad(roughness_tex_1024, tex_sampler, uv, layer, dx, dy); }\n" \
    "    default: { return textureSampleGrad(roughness_tex_2048, tex_sampler, uv, layer, dx, dy); }\n" \
    "  }\n" \
    "}\n" \
    "fn sample_normal(uv : vec2<f32>, layer : u32, bucket : u32,\n" \
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  switch (bucket) {\n" \
    "    case 0u: { return textureSampleGrad(normal_tex_512,  tex_sampler, uv, layer, dx, dy); }\n" \
    "    case 1u: { return textureSampleGrad(normal_tex_1024, tex_sampler, uv, layer, dx, dy); }\n" \
    "    default: { return textureSampleGrad(normal_tex_2048, tex_sampler, uv, layer, dx, dy); }\n" \
    "  }\n" \
    "}\n"

#endif
