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
    "}\n"

#endif
