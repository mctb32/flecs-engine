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

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL

    FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL

    /* Opaque scene snapshot at group 0 binding 3 (was BRDF LUT) */
    "@group(0) @binding(3) var opaque_snapshot : texture_2d<f32>;\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL

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
    "  @location(5) @interpolate(flat) material_id : u32,\n"
    "  @location(6) model_scale : vec3<f32>\n"
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
    "  out.model_scale = vec3<f32>(length(input.m0), length(input.m1), length(input.m2));\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let mat = materials[input.material_id];\n"
    "  let bucket = mat.texture_bucket;\n"
    "  let uv = input.uv * vec2<f32>(mat.uv_scale_x, mat.uv_scale_y)\n"
    "         + vec2<f32>(mat.uv_offset_x, mat.uv_offset_y);\n"
    "  let dx = dpdx(uv);\n"
    "  let dy = dpdy(uv);\n"
    "  let mat_color = unpack4x8unorm(mat.color);\n"

    /* Sample albedo only if the material has an albedo texture
     * (layer 0 is the reserved neutral slot — always white). */
    "  var albedo = mat_color.rgb;\n"
    "  if (mat.layer_albedo != 0u) {\n"
    "    let base_color = sample_albedo(uv, mat.layer_albedo, bucket, dx, dy);\n"
    "    albedo = base_color.rgb * mat_color.rgb;\n"
    "  }\n"

    /* Sample metallic-roughness only if the material has an MR texture. */
    "  var roughness = mat.roughness;\n"
    "  var metallic = mat.metallic;\n"
    "  if (mat.layer_mr != 0u) {\n"
    "    let mr = sample_roughness(uv, mat.layer_mr, bucket, dx, dy);\n"
    "    roughness = mr.g * mat.roughness;\n"
    "    metallic = mr.b * mat.metallic;\n"
    "  }\n"

    /* Sample emissive only if the material actually emits. */
    "  var emissive = vec3<f32>(0.0);\n"
    "  if (mat.emissive_strength > 0.0) {\n"
    "    let em_color = unpack4x8unorm(mat.emissive_color).rgb;\n"
    "    var em_sample = vec3<f32>(1.0);\n"
    "    if (mat.layer_emissive != 0u) {\n"
    "      em_sample = sample_emissive(uv, mat.layer_emissive, bucket, dx, dy).rgb;\n"
    "    }\n"
    "    emissive = em_sample * em_color * mat.emissive_strength;\n"
    "  }\n"

    /* Apply normal map only if the material has one; otherwise use
     * the geometric normal directly and skip the TBN reconstruction. */
    "  var n = normalize(input.normal);\n"
    "  if (mat.layer_normal != 0u) {\n"
    "    let normal_sample = sample_normal(uv, mat.layer_normal, bucket, dx, dy).rgb;\n"
    "    let tangent_normal = normal_sample * 2.0 - 1.0;\n"
    "    let T_raw = input.tangent;\n"
    "    let T = normalize(T_raw - n * dot(n, T_raw));\n"
    "    let B = cross(n, T) * input.bitangent_sign;\n"
    "    let tbn = mat3x3<f32>(T, B, n);\n"
    "    n = normalize(tbn * tangent_normal);\n"
    "  }\n"
    "\n"
    "  let v = getViewDirection(input.world_pos);\n"
    "  let ndotv = saturate(dot(n, v));\n"

    /* Fresnel reflection (Schlick with IOR-derived F0) */
    "  let f0_ior = pow((1.0 - mat.ior) / (1.0 + mat.ior), 2.0);\n"
    "  let F = f0_ior + (1.0 - f0_ior) * pow(1.0 - ndotv, 5.0);\n"

    /* Screen-space refraction: project refracted exit point through VP */
    "  let refracted = refract(-v, n, 1.0 / mat.ior);\n"
    "  let exit_pos = input.world_pos + normalize(refracted)\n"
    "    * mat.thickness_factor * input.model_scale;\n"
    "  let exit_clip = uniforms.vp * vec4<f32>(exit_pos, 1.0);\n"
    "  var screen_uv = exit_clip.xy / exit_clip.w * vec2<f32>(0.5, -0.5)\n"
    "    + vec2<f32>(0.5, 0.5);\n"
    "  screen_uv = clamp(screen_uv, vec2<f32>(0.0), vec2<f32>(1.0));\n"

    "  let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);\n"
    "  let bg_lod = roughness * max_mip;\n"
    "  let background = textureSampleLevel(\n"
    "    opaque_snapshot, ibl_sampler, screen_uv, bg_lod).rgb;\n"

    "  let att_color = max(unpack4x8unorm(mat.attenuation_color).rgb, vec3<f32>(0.001));\n"
    "  let att_ratio = mat.thickness_factor / mat.attenuation_distance;\n"
    "  let transmittance = pow(att_color, vec3<f32>(att_ratio));\n"
    "  let transmitted = (1.0 - F) * background * transmittance * albedo;\n"

    /* Reflected light split into diffuse and specular */
    "  let reflected = computePbrLightingSplit(\n"
    "    albedo, metallic, roughness, emissive,\n"
    "    input.world_pos, n, input.pos);\n"

    /* Blend: transmission replaces diffuse, specular always added on top */
    "  let final_color = mix(reflected.diffuse, transmitted, mat.transmission_factor)\n"
    "    + reflected.specular;\n"
    "  return vec4<f32>(final_color, 1.0);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrTransmission(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrTransmissionShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
