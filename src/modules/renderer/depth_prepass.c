#include "renderer.h"
#include "depth_prepass.h"
#include "flecs_engine.h"

static const char *FLECS_ENGINE_DEPTH_PREPASS_WGSL =
"struct Uniforms {\n"
"    mvp: mat4x4<f32>,\n"
"    inv_vp: mat4x4<f32>,\n"
"    light_vp: array<mat4x4<f32>, 4>,\n"
"    cascade_splits: vec4<f32>,\n"
"    light_ray_dir: vec4<f32>,\n"
"    light_color: vec4<f32>,\n"
"    camera_pos: vec4<f32>,\n"
"    shadow_info: vec4<f32>,\n"
"    ambient_light: vec4<f32>,\n"
"};\n"
"\n"
"@group(0) @binding(0) var<uniform> uniforms: Uniforms;\n"
"\n"
"struct InstanceTransform {\n"
"    c0: vec4<f32>,\n"
"    c1: vec4<f32>,\n"
"    c2: vec4<f32>,\n"
"    c3: vec4<f32>,\n"
"};\n"
"\n"
"@group(1) @binding(0) var<storage, read> transforms: array<InstanceTransform>;\n"
"\n"
"@vertex\n"
"fn vs_main(\n"
"    @location(0) pos: vec3<f32>,\n"
"    @location(1) slot: u32,\n"
") -> @builtin(position) vec4<f32> {\n"
"    let t = transforms[slot];\n"
"    let model = mat4x4<f32>(\n"
"        vec4<f32>(t.c0.xyz, 0.0),\n"
"        vec4<f32>(t.c1.xyz, 0.0),\n"
"        vec4<f32>(t.c2.xyz, 0.0),\n"
"        vec4<f32>(t.c3.xyz, 1.0));\n"
"    let world = model * vec4<f32>(pos, 1.0);\n"
"    return uniforms.mvp * world;\n"
"}\n";

int flecsEngine_depthPrepass_init(
    FlecsEngineImpl *engine)
{
    engine->depth_prepass.shader_module = flecsEngine_createShaderModule(
        engine->device, FLECS_ENGINE_DEPTH_PREPASS_WGSL);
    if (!engine->depth_prepass.shader_module) {
        ecs_err("depth-prepass shader module creation failed");
        return -1;
    }
    return 0;
}

void flecsEngine_depthPrepass_fini(
    FlecsEngineImpl *engine)
{
    FLECS_WGPU_RELEASE(engine->depth_prepass.shader_module,
        wgpuShaderModuleRelease);
}

WGPURenderPipeline flecsEngine_depthPrepass_createPipeline(
    const FlecsEngineImpl *engine,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
    uint32_t sample_count)
{
    if (!engine->depth_prepass.shader_module || !engine->scene_bind_layout) {
        return NULL;
    }

    WGPUBindGroupLayout inst_layout =
        flecsEngine_instanceBind_ensureLayout((FlecsEngineImpl*)engine);
    if (!inst_layout) {
        return NULL;
    }

    WGPUBindGroupLayout layouts[2] = {
        engine->scene_bind_layout,
        inst_layout
    };
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = layouts
    };
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth24Plus,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPUVertexState vertex_state = {
        .module = engine->depth_prepass.shader_module,
        .entryPoint = WGPU_STR("vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = NULL,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_Back,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE(sample_count)
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}
