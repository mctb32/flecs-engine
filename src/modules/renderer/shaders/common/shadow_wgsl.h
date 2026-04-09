#ifndef FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL \
    "@group(0) @binding(3) var shadow_map : texture_depth_2d_array;\n" \
    "@group(0) @binding(4) var shadow_sampler : sampler_comparison;\n" \
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
    "  let texel_size = 1.0 / vec2<f32>(textureDimensions(shadow_map));\n" \
    "  var shadow = 0.0;\n" \
    "  for (var x = -1; x <= 1; x++) {\n" \
    "    for (var y = -1; y <= 1; y++) {\n" \
    "      let offset = vec2<f32>(f32(x), f32(y)) * texel_size;\n" \
    "      shadow += textureSampleCompareLevel(\n" \
    "        shadow_map, shadow_sampler,\n" \
    "        shadow_uv + offset, cascade, current_depth);\n" \
    "    }\n" \
    "  }\n" \
    "  return shadow / 9.0;\n" \
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
