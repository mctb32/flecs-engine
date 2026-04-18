#include "private.h"

WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source)
{
    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = {
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = WGPU_SHADER_CODE(wgsl_source)
    };

    return wgpuDeviceCreateShaderModule(
        device, &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&wgsl_desc
        });
}

WGPURenderPipeline flecsEngine_createFullscreenPipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *vertex_entry,
    const char *fragment_entry,
    const WGPUColorTargetState *color_target,
    const WGPUDepthStencilState *depth_stencil)
{
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_layout
        });
    if (!pipeline_layout) {
        return NULL;
    }

    if (!vertex_entry) {
        vertex_entry = "vs_main";
    }
    if (!fragment_entry) {
        fragment_entry = "fs_main";
    }

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        impl->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = module,
                .entryPoint = WGPU_STR(vertex_entry)
            },
            .fragment = &(WGPUFragmentState){
                .module = module,
                .entryPoint = WGPU_STR(fragment_entry),
                .targetCount = color_target ? 1 : 0,
                .targets = color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .depthStencil = depth_stencil,
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

WGPURenderPassEncoder flecsEngine_beginColorPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value)
{
    WGPURenderPassColorAttachment color_att = {
        .view = view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clear_value
    };

    return wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        });
}

WGPUComputePipeline flecsEngine_createComputePipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *entry)
{
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_layout
        });
    if (!pipeline_layout) {
        return NULL;
    }

    if (!entry) {
        entry = "cs_main";
    }

    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(
        impl->device, &(WGPUComputePipelineDescriptor){
            .layout = pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = WGPU_STR(entry)
            }
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

WGPUSampler flecsEngine_createLinearClampSampler(
    WGPUDevice device)
{
    return wgpuDeviceCreateSampler(device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
}

WGPUBuffer flecsEngine_createUniformBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = size
    });
}

bool flecsEngine_fullscreenPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group)
{
    WGPURenderPassEncoder pass = flecsEngine_beginColorPass(
        encoder, view, load_op, clear_value);
    if (!pass) {
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (bind_group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    }
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}
