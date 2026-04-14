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
#include "common/pbr_textures_wgsl.h"

/* Unified non-transmission PBR shader. Reads per-instance material data
 * from the storage buffer at @group(2) @binding(0), and samples PBR
 * texture arrays at @group(1). Each texture sample is gated on the
 * material's layer index — a layer of 0 (the reserved neutral slot)
 * means "no texture assigned", so the material's factor values drive
 * the shading directly (matching the previous pbr_colored path).
 * The branches are coherent per-material, so cost is minimal on
 * modern GPUs; the unified shader replaces both the colored-only and
 * textured PBR shader permutations. */
static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL
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
    "  let model_3x3 = mat3x3<f32>(input.m0, input.m1, input.m2);\n"
    "  out.tangent = model_3x3 * input.tan.xyz;\n"
    "  out.bitangent_sign = input.tan.w;\n"
    "  out.material_id = input.material_id;\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let material = materials[input.material_id];\n"
    "  let bucket = material.texture_bucket;\n"
    "  let uv = input.uv * vec2<f32>(material.uv_scale_x, material.uv_scale_y)\n"
    "         + vec2<f32>(material.uv_offset_x, material.uv_offset_y);\n"
    "  let dx = dpdx(uv);\n"
    "  let dy = dpdy(uv);\n"
    "  let mat_color = unpack4x8unorm(material.color);\n"

    /* Sample albedo only when the material has one; otherwise use the
     * material's color factor directly (matches the old pbr_colored). */
    "  var albedo = mat_color.rgb;\n"
    "  var alpha = mat_color.a;\n"
    "  if (material.layer_albedo != 0u) {\n"
    "    let base_color = sample_albedo(uv, material.layer_albedo, bucket, dx, dy);\n"
    "    if (base_color.a < 0.5) { discard; }\n"
    "    albedo = base_color.rgb * mat_color.rgb;\n"
    "    alpha = base_color.a * mat_color.a;\n"
    "  }\n"

    /* Sample metallic-roughness only if the material has an MR texture. */
    "  var roughness = material.roughness;\n"
    "  var metallic = material.metallic;\n"
    "  if (material.layer_mr != 0u) {\n"
    "    let mr = sample_roughness(uv, material.layer_mr, bucket, dx, dy);\n"
    "    roughness = mr.g * material.roughness;\n"
    "    metallic = mr.b * material.metallic;\n"
    "  }\n"

    /* Emissive: fall back to albedo as base when the material doesn't
     * specify an emissive color but has non-zero strength. Matches the
     * previous pbr_colored behavior for procedural geometry. */
    "  let em_color = unpack4x8unorm(material.emissive_color).rgb;\n"
    "  let em_has_color = dot(em_color, em_color) > 0.0;\n"
    "  let em_base = select(albedo, em_color, em_has_color);\n"
    "  var emissive = vec3<f32>(0.0);\n"
    "  if (material.emissive_strength > 0.0) {\n"
    "    var em_sample = vec3<f32>(1.0);\n"
    "    if (material.layer_emissive != 0u) {\n"
    "      em_sample = sample_emissive(uv, material.layer_emissive, bucket, dx, dy).rgb;\n"
    "    }\n"
    "    emissive = em_sample * em_base * max(material.emissive_strength, 0.0);\n"
    "  }\n"

    /* Apply normal map only when the material has one. */
    "  var mapped_normal = normalize(input.normal);\n"
    "  if (material.layer_normal != 0u) {\n"
    "    let normal_sample = sample_normal(uv, material.layer_normal, bucket, dx, dy).rgb;\n"
    "    let tangent_normal = normal_sample * 2.0 - 1.0;\n"
    "    let T_raw = input.tangent;\n"
    "    let T = normalize(T_raw - mapped_normal * dot(mapped_normal, T_raw));\n"
    "    let B = cross(mapped_normal, T) * input.bitangent_sign;\n"
    "    let tbn = mat3x3<f32>(T, B, mapped_normal);\n"
    "    mapped_normal = normalize(tbn * tangent_normal);\n"
    "  }\n"

    "  let lit = computePbrLighting(\n"
    "    albedo,\n"
    "    metallic,\n"
    "    roughness,\n"
    "    emissive,\n"
    "    input.world_pos,\n"
    "    mapped_normal,\n"
    "    input.pos);\n"
    "  return vec4<f32>(lit, alpha);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrColored(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrColoredShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
