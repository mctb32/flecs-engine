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

    FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL

    /* Opaque scene snapshot at group 0 binding 3 (was BRDF LUT) */
    "@group(0) @binding(3) var opaque_snapshot : texture_2d<f32>;\n"

    /* PBR texture array bindings at group 1 (same layout as pbr_textured) */
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

    /* Split PBR lighting into diffuse and specular for transmission blending.
     * Per the glTF spec, transmission replaces only the diffuse component;
     * specular reflections are added on top unattenuated. */
    "struct PbrLightingSplit {\n"
    "  diffuse : vec3<f32>,\n"
    "  specular : vec3<f32>\n"
    "};\n"
    "fn computePbrLightingSplit(\n"
    "  albedo : vec3<f32>,\n"
    "  metallic : f32,\n"
    "  roughness : f32,\n"
    "  emissive : vec3<f32>,\n"
    "  world_pos : vec3<f32>,\n"
    "  normal : vec3<f32>,\n"
    "  frag_coord : vec4<f32>\n"
    ") -> PbrLightingSplit {\n"
    "  let dr = max(roughness, 0.045);\n"
    "  let n = normalize(normal);\n"
    "  let light = getLightDirection();\n"
    "  let v = getViewDirection(world_pos);\n"
    "  let h = getHalfVector(v, light.dir);\n"
    "  let f0 = computeF0(albedo, metallic);\n"
    "  let ndv = saturate(dot(n, v));\n"
    "  let gv = geometrySchlickGGX(ndv, dr);\n"
    "  let shd = computeShadow(world_pos);\n"
    "  var dd = vec3<f32>(0.0);\n"
    "  var ds = vec3<f32>(0.0);\n"
    "  if (light.enabled) {\n"
    "    let nl = max(dot(n, light.dir), 0.0);\n"
    "    if (nl > 0.0) {\n"
    "      let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n"
    "      let s = uniforms.light_color.rgb * nl * shd;\n"
    "      dd = computeDiffuse(albedo, metallic, f) * s;\n"
    "      ds = computeSpecular(n, ndv, gv, light.dir, h, dr, f) * s;\n"
    "    }\n"
    "  }\n"
    "  let r = reflect(-v, n);\n"
    "  let mm = max(f32(textureNumLevels(ibl_prefiltered_env)) - 1.0, 0.0);\n"
    "  let lod = roughness * mm;\n"
    "  let pf = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, r, lod).rgb;\n"
    "  let brdf = envBRDFApprox(roughness, ndv);\n"
    "  let F_ibl = fresnelSchlickRoughness(ndv, f0, roughness);\n"
    "  let si = pf * computeSplitSumSpecularTerm(f0, brdf) * uniforms.ambient_light.w;\n"
    "  let irr = textureSampleLevel(ibl_irradiance_env, ibl_sampler, n, 0.0).rgb;\n"
    "  let kD = (vec3<f32>(1.0) - F_ibl) * (1.0 - metallic);\n"
    "  let di = irr * albedo * kD * uniforms.ambient_light.w;\n"
    "  var cd = vec3<f32>(0.0);\n"
    "  var cs = vec3<f32>(0.0);\n"
    "  let ci = getClusterIndex(frag_coord);\n"
    "  let ce = cluster_grid[ci];\n"
    "  for (var j = 0u; j < ce.light_count; j++) {\n"
    "    let li = light_indices[ce.light_offset + j];\n"
    "    let lp = lights[li].position.xyz;\n"
    "    let lr = lights[li].position.w;\n"
    "    let ld = lights[li].direction.xyz;\n"
    "    let lo = lights[li].direction.w;\n"
    "    let lc = lights[li].color.rgb;\n"
    "    let lin = lights[li].color.w;\n"
    "    let tl = lp - world_pos;\n"
    "    let d = length(tl);\n"
    "    if (d > lr || d < 0.001) { continue; }\n"
    "    let l = tl / d;\n"
    "    var spot = 1.0;\n"
    "    if (lo > -1.5) {\n"
    "      let th = dot(l, -ld);\n"
    "      if (th < lo) { continue; }\n"
    "      spot = clamp((th - lo) / max(lin - lo, PBR_DIV_EPSILON), 0.0, 1.0);\n"
    "    }\n"
    "    let hc = getHalfVector(v, l);\n"
    "    let nl = max(dot(n, l), 0.0);\n"
    "    if (nl <= 0.0) { continue; }\n"
    "    let rt = d / lr;\n"
    "    let r2 = rt * rt;\n"
    "    let att = clamp(1.0 - r2 * r2, 0.0, 1.0) / (d * d + 1.0);\n"
    "    let fc = fresnelSchlick(max(dot(hc, v), 0.0), f0);\n"
    "    let sc = lc * nl * att * spot;\n"
    "    cd += computeDiffuse(albedo, metallic, fc) * sc;\n"
    "    cs += computeSpecular(n, ndv, gv, l, hc, dr, fc) * sc;\n"
    "  }\n"
    "  return PbrLightingSplit(di + dd + cd, si + ds + cs + emissive);\n"
    "}\n"

    /* Per-channel sample helpers (same as pbr_textured). The branch on
     * `bucket` is non-uniform across fragments, so derivatives must be
     * lifted out of the switch and passed in via textureSampleGrad. */
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
    "  let mat = materials[input.material_id];\n"
    "  let bucket = mat.texture_bucket;\n"
    "  let dx = dpdx(input.uv);\n"
    "  let dy = dpdy(input.uv);\n"
    "  let mat_color = unpack4x8unorm(mat.color);\n"

    /* Sample albedo only if the material has an albedo texture
     * (layer 0 is the reserved neutral slot — always white). */
    "  var albedo = mat_color.rgb;\n"
    "  if (mat.layer_albedo != 0u) {\n"
    "    let base_color = sample_albedo(input.uv, mat.layer_albedo, bucket, dx, dy);\n"
    "    albedo = base_color.rgb * mat_color.rgb;\n"
    "  }\n"

    /* Sample metallic-roughness only if the material has an MR texture. */
    "  var roughness = mat.roughness;\n"
    "  var metallic = mat.metallic;\n"
    "  if (mat.layer_mr != 0u) {\n"
    "    let mr = sample_roughness(input.uv, mat.layer_mr, bucket, dx, dy);\n"
    "    roughness = mr.g * mat.roughness;\n"
    "    metallic = mr.b * mat.metallic;\n"
    "  }\n"

    /* Sample emissive only if the material actually emits. */
    "  var emissive = vec3<f32>(0.0);\n"
    "  if (mat.emissive_strength > 0.0) {\n"
    "    let em_color = unpack4x8unorm(mat.emissive_color).rgb;\n"
    "    var em_sample = vec3<f32>(1.0);\n"
    "    if (mat.layer_emissive != 0u) {\n"
    "      em_sample = sample_emissive(input.uv, mat.layer_emissive, bucket, dx, dy).rgb;\n"
    "    }\n"
    "    emissive = em_sample * em_color * mat.emissive_strength;\n"
    "  }\n"

    /* Apply normal map only if the material has one; otherwise use
     * the geometric normal directly and skip the TBN reconstruction. */
    "  var n = normalize(input.normal);\n"
    "  if (mat.layer_normal != 0u) {\n"
    "    let normal_sample = sample_normal(input.uv, mat.layer_normal, bucket, dx, dy).rgb;\n"
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

    /* Sample opaque scene at refracted UV using a roughness-dependent LOD.
     * Rough transmission (frosted glass) sees a blurred background by
     * selecting a higher mip level of the pre-generated pyramid. */
    "  let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);\n"
    "  let bg_lod = roughness * max_mip;\n"
    "  let background = textureSampleLevel(\n"
    "    opaque_snapshot, ibl_sampler, screen_uv, bg_lod).rgb;\n"

    /* Beer-Lambert absorption (glTF KHR_materials_volume spec):
     * transmittance = pow(attenuation_color, thickness / attenuation_distance).
     * Transmitted light is also tinted by the base color. */
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
