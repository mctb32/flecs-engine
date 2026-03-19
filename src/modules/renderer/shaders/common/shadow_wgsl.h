#ifndef FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL \
    "@group(2) @binding(0) var shadow_map : texture_depth_2d;\n" \
    "@group(2) @binding(1) var shadow_sampler : sampler_comparison;\n" \
    "fn computeShadow(world_pos : vec3<f32>) -> f32 {\n" \
    "  let light_clip = uniforms.light_vp * vec4<f32>(world_pos, 1.0);\n" \
    "  let light_ndc = light_clip.xyz / light_clip.w;\n" \
    "  let shadow_uv = vec2<f32>(\n" \
    "    light_ndc.x * 0.5 + 0.5,\n" \
    "    light_ndc.y * -0.5 + 0.5\n" \
    "  );\n" \
    "  if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||\n" \
    "      shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||\n" \
    "      light_ndc.z < 0.0 || light_ndc.z > 1.0) {\n" \
    "    return 1.0;\n" \
    "  }\n" \
    "  let bias = 0.003;\n" \
    "  let current_depth = light_ndc.z - bias;\n" \
    "  let texel_size = 1.0 / vec2<f32>(textureDimensions(shadow_map));\n" \
    "  var shadow = 0.0;\n" \
    "  for (var x = -1i; x <= 1i; x++) {\n" \
    "    for (var y = -1i; y <= 1i; y++) {\n" \
    "      let offset = vec2<f32>(f32(x), f32(y)) * texel_size;\n" \
    "      shadow += textureSampleCompareLevel(\n" \
    "        shadow_map, shadow_sampler,\n" \
    "        shadow_uv + offset, current_depth);\n" \
    "    }\n" \
    "  }\n" \
    "  shadow /= 9.0;\n" \
    "  return shadow;\n" \
    "}\n"

#endif
