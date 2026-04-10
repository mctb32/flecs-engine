#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/shared_vertex_wgsl.h"
#include "common/cluster_wgsl.h"
#include "common/pbr_functions_wgsl.h"
#include "common/shadow_wgsl.h"
#include "common/pbr_lighting_wgsl.h"
#include "common/ibl_bindings_wgsl.h"
#include "common/gpu_material_wgsl.h"

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL

    /* PBR texture array bindings at group 1.
     * 4 channels × 3 size buckets (512 / 1024 / 2048) = 12 textures + sampler.
     * The bucket index per material lives in GpuMaterial.texture_bucket. */
    "@group(1) @binding(0)  var albedo_tex_512    : texture_2d_array<f32>;\n"
    "@group(1) @binding(1)  var albedo_tex_1024   : texture_2d_array<f32>;\n"
    "@group(1) @binding(2)  var albedo_tex_2048   : texture_2d_array<f32>;\n"
    "@group(1) @binding(3)  var emissive_tex_512  : texture_2d_array<f32>;\n"
    "@group(1) @binding(4)  var emissive_tex_1024 : texture_2d_array<f32>;\n"
    "@group(1) @binding(5)  var emissive_tex_2048 : texture_2d_array<f32>;\n"
    "@group(1) @binding(6)  var roughness_tex_512  : texture_2d_array<f32>;\n"
    "@group(1) @binding(7)  var roughness_tex_1024 : texture_2d_array<f32>;\n"
    "@group(1) @binding(8)  var roughness_tex_2048 : texture_2d_array<f32>;\n"
    "@group(1) @binding(9)  var normal_tex_512    : texture_2d_array<f32>;\n"
    "@group(1) @binding(10) var normal_tex_1024   : texture_2d_array<f32>;\n"
    "@group(1) @binding(11) var normal_tex_2048   : texture_2d_array<f32>;\n"
    "@group(1) @binding(12) var tex_sampler : sampler;\n"

    FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL

    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) tan : vec4<f32>,\n"
    "  @location(4) m0 : vec3<f32>,\n"
    "  @location(5) m1 : vec3<f32>,\n"
    "  @location(6) m2 : vec3<f32>,\n"
    "  @location(7) m3 : vec3<f32>,\n"
    "  @location(8) material_id : u32\n"
    "};\n"

    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) normal : vec3<f32>,\n"
    "  @location(1) world_pos : vec3<f32>,\n"
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) tangent : vec3<f32>,\n"
    "  @location(4) bitangent_sign : f32,\n"
    "  @location(5) @interpolate(flat) material_id : u32\n"
    "};\n"

    FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL

    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let vertex = buildPbrVertexState(\n"
    "    input.pos,\n"
    "    input.nrm,\n"
    "    input.m0,\n"
    "    input.m1,\n"
    "    input.m2,\n"
    "    input.m3);\n"
    "  out.pos = vertex.clip_pos;\n"
    "  out.normal = vertex.world_normal;\n"
    "  out.world_pos = vertex.world_pos;\n"
    "  out.uv = input.uv;\n"
    "  // Tangent is a surface-direction vector, so it transforms with the\n"
    "  // model matrix's 3x3 part (not the inverse-transpose like the normal).\n"
    "  let model_3x3 = mat3x3<f32>(input.m0, input.m1, input.m2);\n"
    "  out.tangent = model_3x3 * input.tan.xyz;\n"
    "  out.bitangent_sign = input.tan.w;\n"
    "  out.material_id = input.material_id;\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL

    /* Per-channel sample helpers. The branch on `bucket` is non-uniform
     * across fragments, so derivatives must be lifted out of the switch
     * and passed in via textureSampleGrad. */
    "fn sample_albedo(uv : vec2<f32>, layer : u32, bucket : u32,\n"
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n"
    "  switch (bucket) {\n"
    "    case 0u: { return textureSampleGrad(albedo_tex_512,  tex_sampler, uv, layer, dx, dy); }\n"
    "    case 1u: { return textureSampleGrad(albedo_tex_1024, tex_sampler, uv, layer, dx, dy); }\n"
    "    default: { return textureSampleGrad(albedo_tex_2048, tex_sampler, uv, layer, dx, dy); }\n"
    "  }\n"
    "}\n"
    "fn sample_emissive(uv : vec2<f32>, layer : u32, bucket : u32,\n"
    "                   dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n"
    "  switch (bucket) {\n"
    "    case 0u: { return textureSampleGrad(emissive_tex_512,  tex_sampler, uv, layer, dx, dy); }\n"
    "    case 1u: { return textureSampleGrad(emissive_tex_1024, tex_sampler, uv, layer, dx, dy); }\n"
    "    default: { return textureSampleGrad(emissive_tex_2048, tex_sampler, uv, layer, dx, dy); }\n"
    "  }\n"
    "}\n"
    "fn sample_roughness(uv : vec2<f32>, layer : u32, bucket : u32,\n"
    "                    dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n"
    "  switch (bucket) {\n"
    "    case 0u: { return textureSampleGrad(roughness_tex_512,  tex_sampler, uv, layer, dx, dy); }\n"
    "    case 1u: { return textureSampleGrad(roughness_tex_1024, tex_sampler, uv, layer, dx, dy); }\n"
    "    default: { return textureSampleGrad(roughness_tex_2048, tex_sampler, uv, layer, dx, dy); }\n"
    "  }\n"
    "}\n"
    "fn sample_normal(uv : vec2<f32>, layer : u32, bucket : u32,\n"
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n"
    "  switch (bucket) {\n"
    "    case 0u: { return textureSampleGrad(normal_tex_512,  tex_sampler, uv, layer, dx, dy); }\n"
    "    case 1u: { return textureSampleGrad(normal_tex_1024, tex_sampler, uv, layer, dx, dy); }\n"
    "    default: { return textureSampleGrad(normal_tex_2048, tex_sampler, uv, layer, dx, dy); }\n"
    "  }\n"
    "}\n"

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let material = materials[input.material_id];\n"
    "  let bucket = material.texture_bucket;\n"
    "  let dx = dpdx(input.uv);\n"
    "  let dy = dpdy(input.uv);\n"
    "  let base_color = sample_albedo(input.uv, material.layer_albedo, bucket, dx, dy);\n"
    "  if (base_color.a < 0.5) { discard; }\n"
    "  let mat_color = unpack4x8unorm(material.color);\n"
    "  let albedo = base_color.rgb * mat_color.rgb;\n"
    "\n"
    "  let mr = sample_roughness(input.uv, material.layer_mr, bucket, dx, dy);\n"
    "  let roughness = mr.g * material.roughness;\n"
    "  let metallic = mr.b * material.metallic;\n"
    "\n"
    "  let emissive_sample = sample_emissive(input.uv, material.layer_emissive, bucket, dx, dy).rgb;\n"
    "  let em_color = unpack4x8unorm(material.emissive_color).rgb;\n"
    "  let emissive = emissive_sample * em_color * max(material.emissive_strength, 0.0);\n"
    "\n"
    "  let normal_sample = sample_normal(input.uv, material.layer_normal, bucket, dx, dy).rgb;\n"
    "  let tangent_normal = normal_sample * 2.0 - 1.0;\n"
    "\n"
    "  // Build a stable TBN from the per-vertex tangent. This is independent\n"
    "  // of camera distance and screen resolution, unlike the previous\n"
    "  // dpdx/dpdy-derived tangent which collapsed at close range.\n"
    "  let N = normalize(input.normal);\n"
    "  // Re-orthonormalize the interpolated tangent against N (Gram-Schmidt).\n"
    "  let T_raw = input.tangent;\n"
    "  let T = normalize(T_raw - N * dot(N, T_raw));\n"
    "  let B = cross(N, T) * input.bitangent_sign;\n"
    "  let tbn = mat3x3<f32>(T, B, N);\n"
    "  let mapped_normal = normalize(tbn * tangent_normal);\n"
    "  let lit = computePbrLighting(\n"
    "    albedo,\n"
    "    metallic,\n"
    "    roughness,\n"
    "    emissive,\n"
    "    input.world_pos,\n"
    "    mapped_normal,\n"
    "    input.pos);\n"
    "  return vec4<f32>(lit, base_color.a * mat_color.a);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrTextured(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrTexturedShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
