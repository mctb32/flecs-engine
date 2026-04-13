#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL \
    "fn computePbrLighting(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  normal : vec3<f32>,\n" \
    "  frag_coord : vec4<f32>\n" \
    ") -> vec3<f32> {\n" \
    "  let direct_roughness = max(roughness, 0.045);\n" \
    "  let n = normalize(normal);\n" \
    "  let light = getLightDirection();\n" \
    "  let v = getViewDirection(world_pos);\n" \
    "  let h = getHalfVector(v, light.dir);\n" \
    "  let f0 = computeF0(albedo, metallic);\n" \
    "  let ndotv = saturate(dot(n, v));\n" \
    "  let ggx_v = geometrySchlickGGX(ndotv, direct_roughness);\n" \
    "  let shadow = computeShadow(world_pos);\n" \
    "  let direct = computeDirectLighting(\n" \
    "    n, v, light.dir, h,\n" \
    "    albedo, metallic, direct_roughness,\n" \
    "    f0, ndotv, ggx_v,\n" \
    "    light.enabled) * shadow;\n" \
    "  let r = reflect(-v, n);\n" \
    "  let max_mip = max(f32(textureNumLevels(ibl_prefiltered_env)) - 1.0, 0.0);\n" \
    "  let lod = roughness * max_mip;\n" \
    "  let prefiltered_color = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, r, lod).rgb;\n" \
    "  let brdf = envBRDFApprox(roughness, ndotv);\n" \
    "  let F = fresnelSchlickRoughness(ndotv, f0, roughness);\n" \
    "  let specular_ibl = prefiltered_color * computeSplitSumSpecularTerm(f0, brdf) * uniforms.ambient_light.w;\n" \
    "  let irradiance = textureSampleLevel(ibl_irradiance_env, ibl_sampler, n, 0.0).rgb;\n" \
    "  let kD = (vec3<f32>(1.0) - F) * (1.0 - metallic);\n" \
    "  let diffuse_ibl = irradiance * albedo * kD * uniforms.ambient_light.w;\n" \
    "  let cluster_idx = getClusterIndex(frag_coord);\n" \
    "  let cluster_light = computeClusterLighting(\n" \
    "    n, v, world_pos, albedo, metallic, direct_roughness,\n" \
    "    f0, ndotv, ggx_v, cluster_idx);\n" \
    "  return diffuse_ibl + direct + cluster_light + specular_ibl + emissive;\n" \
    "}\n" \
    /* Split PBR lighting into diffuse and specular for transmission blending.
     * Per the glTF spec, transmission replaces only the diffuse component;
     * specular reflections are added on top unattenuated. */ \
    "struct PbrLightingSplit {\n" \
    "  diffuse : vec3<f32>,\n" \
    "  specular : vec3<f32>\n" \
    "};\n" \
    "fn computePbrLightingSplit(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  normal : vec3<f32>,\n" \
    "  frag_coord : vec4<f32>\n" \
    ") -> PbrLightingSplit {\n" \
    "  let dr = max(roughness, 0.045);\n" \
    "  let n = normalize(normal);\n" \
    "  let light = getLightDirection();\n" \
    "  let v = getViewDirection(world_pos);\n" \
    "  let h = getHalfVector(v, light.dir);\n" \
    "  let f0 = computeF0(albedo, metallic);\n" \
    "  let ndv = saturate(dot(n, v));\n" \
    "  let gv = geometrySchlickGGX(ndv, dr);\n" \
    "  let shd = computeShadow(world_pos);\n" \
    "  var dd = vec3<f32>(0.0);\n" \
    "  var ds = vec3<f32>(0.0);\n" \
    "  if (light.enabled) {\n" \
    "    let nl = max(dot(n, light.dir), 0.0);\n" \
    "    if (nl > 0.0) {\n" \
    "      let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "      let s = uniforms.light_color.rgb * nl * shd;\n" \
    "      dd = computeDiffuse(albedo, metallic, f) * s;\n" \
    "      ds = computeSpecular(n, ndv, gv, light.dir, h, dr, f) * s;\n" \
    "    }\n" \
    "  }\n" \
    "  let r = reflect(-v, n);\n" \
    "  let mm = max(f32(textureNumLevels(ibl_prefiltered_env)) - 1.0, 0.0);\n" \
    "  let lod = roughness * mm;\n" \
    "  let pf = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, r, lod).rgb;\n" \
    "  let brdf = envBRDFApprox(roughness, ndv);\n" \
    "  let F_ibl = fresnelSchlickRoughness(ndv, f0, roughness);\n" \
    "  let si = pf * computeSplitSumSpecularTerm(f0, brdf) * uniforms.ambient_light.w;\n" \
    "  let irr = textureSampleLevel(ibl_irradiance_env, ibl_sampler, n, 0.0).rgb;\n" \
    "  let kD = (vec3<f32>(1.0) - F_ibl) * (1.0 - metallic);\n" \
    "  let di = irr * albedo * kD * uniforms.ambient_light.w;\n" \
    "  var cd = vec3<f32>(0.0);\n" \
    "  var cs = vec3<f32>(0.0);\n" \
    "  let ci = getClusterIndex(frag_coord);\n" \
    "  let ce = cluster_grid[ci];\n" \
    "  for (var j = 0u; j < ce.light_count; j++) {\n" \
    "    let li = light_indices[ce.light_offset + j];\n" \
    "    let lp = lights[li].position.xyz;\n" \
    "    let lr = lights[li].position.w;\n" \
    "    let ld = lights[li].direction.xyz;\n" \
    "    let lo = lights[li].direction.w;\n" \
    "    let lc = lights[li].color.rgb;\n" \
    "    let lin = lights[li].color.w;\n" \
    "    let tl = lp - world_pos;\n" \
    "    let d = length(tl);\n" \
    "    if (d > lr || d < 0.001) { continue; }\n" \
    "    let l = tl / d;\n" \
    "    var spot = 1.0;\n" \
    "    if (lo > -1.5) {\n" \
    "      let th = dot(l, -ld);\n" \
    "      if (th < lo) { continue; }\n" \
    "      spot = clamp((th - lo) / max(lin - lo, PBR_DIV_EPSILON), 0.0, 1.0);\n" \
    "    }\n" \
    "    let hc = getHalfVector(v, l);\n" \
    "    let nl = max(dot(n, l), 0.0);\n" \
    "    if (nl <= 0.0) { continue; }\n" \
    "    let rt = d / lr;\n" \
    "    let r2 = rt * rt;\n" \
    "    let att = clamp(1.0 - r2 * r2, 0.0, 1.0) / (d * d + 1.0);\n" \
    "    let fc = fresnelSchlick(max(dot(hc, v), 0.0), f0);\n" \
    "    let sc = lc * nl * att * spot;\n" \
    "    cd += computeDiffuse(albedo, metallic, fc) * sc;\n" \
    "    cs += computeSpecular(n, ndv, gv, l, hc, dr, fc) * sc;\n" \
    "  }\n" \
    "  return PbrLightingSplit(di + dd + cd, si + ds + cs + emissive);\n" \
    "}\n"

#endif
