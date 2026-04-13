#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/shared_vertex_wgsl.h"
#include "common/cluster_wgsl.h"
#include "common/pbr_functions_wgsl.h"
#include "common/shadow_wgsl.h"
#include "common/pbr_lighting_wgsl.h"
#include "common/ibl_bindings_wgsl.h"

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL

    /* Opaque scene snapshot at group 0 binding 3 (same as pbr_transmission) */
    "@group(0) @binding(3) var opaque_snapshot : texture_2d<f32>;\n"

    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>,\n"
    "  @location(6) color : vec4<f32>,\n"
    "  @location(7) metallic : f32,\n"
    "  @location(8) roughness : f32,\n"
    "  @location(9) emissive_strength : f32,\n"
    "  @location(10) emissive_color : vec4<f32>,\n"
    "  @location(11) transmission_factor : f32,\n"
    "  @location(12) ior : f32,\n"
    "  @location(13) thickness_factor : f32,\n"
    "  @location(14) attenuation_distance : f32,\n"
    "  @location(15) attenuation_color : vec4<f32>\n"
    "};\n"

    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec4<f32>,\n"
    "  @location(1) normal : vec3<f32>,\n"
    "  @location(2) world_pos : vec3<f32>,\n"
    "  @location(3) metallic : f32,\n"
    "  @location(4) roughness : f32,\n"
    "  @location(5) emissive_strength : f32,\n"
    "  @location(6) emissive_color : vec3<f32>,\n"
    "  @location(7) transmission_factor : f32,\n"
    "  @location(8) ior : f32,\n"
    "  @location(9) thickness_factor : f32,\n"
    "  @location(10) attenuation_distance : f32,\n"
    "  @location(11) attenuation_color : vec3<f32>,\n"
    "  @location(12) model_scale : vec3<f32>\n"
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
    "  out.color = input.color;\n"
    "  out.metallic = input.metallic;\n"
    "  out.roughness = input.roughness;\n"
    "  out.emissive_strength = input.emissive_strength;\n"
    "  out.emissive_color = input.emissive_color.rgb;\n"
    "  out.transmission_factor = input.transmission_factor;\n"
    "  out.ior = input.ior;\n"
    "  out.thickness_factor = input.thickness_factor;\n"
    "  out.attenuation_distance = input.attenuation_distance;\n"
    "  out.attenuation_color = input.attenuation_color.rgb;\n"
    "  out.model_scale = vec3<f32>(length(input.m0), length(input.m1), length(input.m2));\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let albedo = input.color.rgb;\n"
    "  let metallic = input.metallic;\n"
    "  let roughness = input.roughness;\n"
    "  let has_em_color = dot(input.emissive_color, input.emissive_color) > 0.0;\n"
    "  let em_base = select(albedo, input.emissive_color, has_em_color);\n"
    "  let emissive = em_base * max(input.emissive_strength, 0.0);\n"

    "  let n = normalize(input.normal);\n"
    "  let v = getViewDirection(input.world_pos);\n"
    "  let ndotv = saturate(dot(n, v));\n"

    /* Fresnel reflection (Schlick with IOR-derived F0) */
    "  let f0_ior = pow((1.0 - input.ior) / (1.0 + input.ior), 2.0);\n"
    "  let F = f0_ior + (1.0 - f0_ior) * pow(1.0 - ndotv, 5.0);\n"

    /* Screen-space refraction: project refracted exit point through VP */
    "  let refracted = refract(-v, n, 1.0 / max(input.ior, 0.001));\n"
    "  let exit_pos = input.world_pos + normalize(refracted)\n"
    "    * input.thickness_factor * input.model_scale;\n"
    "  let exit_clip = uniforms.vp * vec4<f32>(exit_pos, 1.0);\n"
    "  var screen_uv = exit_clip.xy / exit_clip.w * vec2<f32>(0.5, -0.5)\n"
    "    + vec2<f32>(0.5, 0.5);\n"
    "  screen_uv = clamp(screen_uv, vec2<f32>(0.0), vec2<f32>(1.0));\n"

    /* Sample opaque scene at refracted UV using a roughness-dependent LOD. */
    "  let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);\n"
    "  let bg_lod = roughness * max_mip;\n"
    "  let background = textureSampleLevel(\n"
    "    opaque_snapshot, ibl_sampler, screen_uv, bg_lod).rgb;\n"

    /* Beer-Lambert absorption */
    "  let att_color = max(input.attenuation_color, vec3<f32>(0.001));\n"
    "  let att_ratio = input.thickness_factor / max(input.attenuation_distance, 0.001);\n"
    "  let transmittance = pow(att_color, vec3<f32>(att_ratio));\n"
    "  let transmitted = (1.0 - F) * background * transmittance * albedo;\n"

    /* Reflected light split into diffuse and specular */
    "  let reflected = computePbrLightingSplit(\n"
    "    albedo, metallic, roughness, emissive,\n"
    "    input.world_pos, n, input.pos);\n"

    /* Blend: transmission replaces diffuse, specular always added on top */
    "  let final_color = mix(reflected.diffuse, transmitted, input.transmission_factor)\n"
    "    + reflected.specular;\n"
    "  return vec4<f32>(final_color, 1.0);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrTransmissionColored(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrTransmissionColoredShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
