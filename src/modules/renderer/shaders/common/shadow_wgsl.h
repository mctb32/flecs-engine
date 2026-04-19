#ifndef FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL \
    "@group(0) @binding(5) var shadow_map : texture_depth_2d_array;\n" \
    "@group(0) @binding(6) var shadow_sampler : sampler_comparison;\n" \
    "fn sampleShadowCascade(world_pos : vec3<f32>, cascade : i32) -> f32 {\n" \
    "  let light_clip = uniforms.light_vp[cascade] * vec4<f32>(world_pos, 1.0);\n" \
    "  let light_ndc = light_clip.xyz / light_clip.w;\n" \
    "  let shadow_uv = vec2<f32>(\n" \
    "    light_ndc.x * 0.5 + 0.5,\n" \
    "    light_ndc.y * -0.5 + 0.5\n" \
    "  );\n" \
    "  if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||\n" \
    "      shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||\n" \
    "      light_ndc.z < 0.0 || light_ndc.z > 1.0) {\n" \
    "    return -1.0;\n" \
    "  }\n" \
    "  let current_depth = light_ndc.z - uniforms.shadow_info.y;\n" \
    "  let shadow_size = vec2<f32>(textureDimensions(shadow_map));\n" \
    "  let texel_size = 1.0 / shadow_size;\n" \
    "  let uv = shadow_uv * shadow_size;\n" \
    "  var base_uv = vec2<f32>(floor(uv.x + 0.5), floor(uv.y + 0.5));\n" \
    "  let s = uv.x + 0.5 - base_uv.x;\n" \
    "  let t = uv.y + 0.5 - base_uv.y;\n" \
    "  base_uv = (base_uv - vec2<f32>(0.5)) * texel_size;\n" \
    "  let uw0 = 3.0 - 2.0 * s;\n" \
    "  let uw1 = 1.0 + 2.0 * s;\n" \
    "  let u0 = (2.0 - s) / uw0 - 1.0;\n" \
    "  let u1 = s / uw1 + 1.0;\n" \
    "  let vw0 = 3.0 - 2.0 * t;\n" \
    "  let vw1 = 1.0 + 2.0 * t;\n" \
    "  let v0 = (2.0 - t) / vw0 - 1.0;\n" \
    "  let v1 = t / vw1 + 1.0;\n" \
    "  var shadow = 0.0;\n" \
    "  shadow += uw0 * vw0 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u0, v0) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw1 * vw0 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u1, v0) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw0 * vw1 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u0, v1) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw1 * vw1 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u1, v1) * texel_size, cascade, current_depth);\n" \
    "  return shadow / 16.0;\n" \
    "}\n" \
    "fn computeShadow(world_pos : vec3<f32>) -> f32 {\n" \
    "  let clip = uniforms.vp * vec4<f32>(world_pos, 1.0);\n" \
    "  let view_depth = clip.w;\n" \
    "  if (view_depth > uniforms.cascade_splits.w) {\n" \
    "    return 1.0;\n" \
    "  }\n" \
    "  var cascade : i32 = 0;\n" \
    "  if (view_depth > uniforms.cascade_splits.z) { cascade = 3; }\n" \
    "  else if (view_depth > uniforms.cascade_splits.y) { cascade = 2; }\n" \
    "  else if (view_depth > uniforms.cascade_splits.x) { cascade = 1; }\n" \
    "  var shadow = sampleShadowCascade(world_pos, cascade);\n" \
    "  if (shadow < 0.0 && cascade < 3) {\n" \
    "    shadow = sampleShadowCascade(world_pos, cascade + 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0 && cascade > 0) {\n" \
    "    shadow = sampleShadowCascade(world_pos, cascade - 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0) { shadow = 1.0; }\n" \
    "  return shadow;\n" \
    "}\n"

#endif
