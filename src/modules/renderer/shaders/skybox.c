#include "shaders.h"
#include "../renderer.h"

static const char *kShaderSource =
    "struct Uniforms {\n"
    "  vp : mat4x4<f32>,\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  clear_color : vec4<f32>,\n"
    "  light_ray_dir : vec4<f32>,\n"
    "  light_color : vec4<f32>,\n"
    "  camera_pos : vec4<f32>\n"
    "}\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "@group(1) @binding(0) var ibl_prefiltered_env : texture_cube<f32>;\n"
    "@group(1) @binding(1) var ibl_sampler : sampler;\n"
    "@group(1) @binding(2) var ibl_brdf_lut : texture_2d<f32>;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>,\n"
    "  @location(6) color : vec4<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) clip_xy : vec2<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let model = mat4x4<f32>(\n"
    "    vec4<f32>(input.m0, 0.0),\n"
    "    vec4<f32>(input.m1, 0.0),\n"
    "    vec4<f32>(input.m2, 0.0),\n"
    "    vec4<f32>(input.m3, 1.0)\n"
    "  );\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  out.pos = vec4<f32>(world_pos.xy, 0.99999, 1.0);\n"
    "  out.clip_xy = out.pos.xy;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let clip = vec4<f32>(input.clip_xy, 1.0, 1.0);\n"
    "  let world_far_h = uniforms.inv_vp * clip;\n"
    "  let inv_w = select(1.0, 1.0 / world_far_h.w, abs(world_far_h.w) > 1e-5);\n"
    "  let world_far = world_far_h.xyz * inv_w;\n"
    "  let raw_dir = world_far - uniforms.camera_pos.xyz;\n"
    "  let ray_dir = select(vec3<f32>(0.0, 1.0, 0.0), normalize(raw_dir), dot(raw_dir, raw_dir) > 1e-8);\n"
    "  let sky = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, ray_dir, 0.0).rgb;\n"
    "  return vec4<f32>(sky, 1.0);\n"
    "}\n";

ecs_entity_t flecsEngineShader_skybox(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "SkyboxShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
