#include <string.h>

#include "renderer.h"
#include "gpu_cull.h"
#include "gpu_timing.h"
#include "batches/common/common.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

/* Must match flecsEngine_gpuCullViewUniforms_t size/layout in shader. */
typedef struct {
    float planes[30][4];      /* [0..6) main, [6..12) c0, [12..18) c1, [18..24) c2, [24..30) c3 */
    float camera_pos[4];
    float screen[4];          /* x=factor, y=threshold, z=_, w=_ */
    uint32_t flags[4];        /* x=main_valid, y=cascade_valid, z=screen_cull_valid, w=_ */
} flecsEngine_gpuCullViewUniforms_t;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    uint32_t group_count;
    uint32_t _pad;
} flecsEngine_gpuCullBatchInfo_t;

static const char *FLECS_ENGINE_GPU_CULL_WGSL =
"struct Aabb {\n"
"    mn: vec3<f32>,\n"
"    _p0: f32,\n"
"    mx: vec3<f32>,\n"
"    _p1: f32,\n"
"};\n"
"struct GroupInfo {\n"
"    src_offset: u32,\n"
"    src_count: u32,\n"
"    _p0: u32,\n"
"    _p1: u32,\n"
"};\n"
"struct DrawArgs {\n"
"    index_count: u32,\n"
"    instance_count: atomic<u32>,\n"
"    first_index: u32,\n"
"    base_vertex: i32,\n"
"    first_instance: u32,\n"
"};\n"
"struct ViewData {\n"
"    planes: array<vec4<f32>, 30>,\n"
"    camera_pos: vec4<f32>,\n"
"    screen: vec4<f32>,\n"
"    flags: vec4<u32>,\n"
"};\n"
"struct BatchInfo {\n"
"    count: u32,\n"
"    capacity: u32,\n"
"    group_count: u32,\n"
"    _pad: u32,\n"
"};\n"
"\n"
"@group(0) @binding(0) var<uniform> view_data: ViewData;\n"
"\n"
"@group(1) @binding(0) var<storage, read> aabbs: array<Aabb>;\n"
"@group(1) @binding(1) var<storage, read> slot_to_group: array<u32>;\n"
"@group(1) @binding(2) var<storage, read> group_info: array<GroupInfo>;\n"
"@group(1) @binding(3) var<storage, read_write> visible_slots: array<u32>;\n"
"@group(1) @binding(4) var<storage, read_write> indirect_args: array<DrawArgs>;\n"
"@group(1) @binding(5) var<uniform> batch: BatchInfo;\n"
"\n"
"fn test_planes(base: u32, amin: vec3<f32>, amax: vec3<f32>) -> bool {\n"
"    for (var i: u32 = 0u; i < 6u; i = i + 1u) {\n"
"        let p = view_data.planes[base + i];\n"
"        let px = select(amin.x, amax.x, p.x >= 0.0);\n"
"        let py = select(amin.y, amax.y, p.y >= 0.0);\n"
"        let pz = select(amin.z, amax.z, p.z >= 0.0);\n"
"        if (p.x * px + p.y * py + p.z * pz + p.w < 0.0) { return false; }\n"
"    }\n"
"    return true;\n"
"}\n"
"\n"
"var<workgroup> wg_first_g: u32;\n"
"var<workgroup> wg_uniform: atomic<u32>;\n"
"var<workgroup> wg_count: atomic<u32>;\n"
"var<workgroup> wg_base: u32;\n"
"\n"
"/* Workgroup-level atomic reduction. When all 64 threads in the workgroup\n"
"   target the same group (common case — slot_to_group is laid out contiguous\n"
"   per group during extract), collapse to one atomicAdd on the global counter.\n"
"   Otherwise fall back to per-thread atomics so boundary workgroups remain\n"
"   correct. */\n"
"fn append_visible(\n"
"    lid: u32, uniform_wg: bool, args_idx_uniform: u32,\n"
"    args_idx: u32, pass_flag: bool,\n"
"    base_offset_uniform: u32, base_offset: u32, slot: u32)\n"
"{\n"
"    if (uniform_wg) {\n"
"        if (lid == 0u) {\n"
"            atomicStore(&wg_count, 0u);\n"
"        }\n"
"        workgroupBarrier();\n"
"        var local_idx: u32 = 0u;\n"
"        if (pass_flag) {\n"
"            local_idx = atomicAdd(&wg_count, 1u);\n"
"        }\n"
"        workgroupBarrier();\n"
"        if (lid == 0u) {\n"
"            let total = atomicLoad(&wg_count);\n"
"            if (total != 0u) {\n"
"                wg_base = atomicAdd(\n"
"                    &indirect_args[args_idx_uniform].instance_count, total);\n"
"            }\n"
"        }\n"
"        workgroupBarrier();\n"
"        if (pass_flag) {\n"
"            visible_slots[base_offset_uniform + wg_base + local_idx] = slot;\n"
"        }\n"
"    } else {\n"
"        if (pass_flag) {\n"
"            let idx = atomicAdd(&indirect_args[args_idx].instance_count, 1u);\n"
"            visible_slots[base_offset + idx] = slot;\n"
"        }\n"
"    }\n"
"}\n"
"\n"
"@compute @workgroup_size(64)\n"
"fn cs_main(\n"
"    @builtin(global_invocation_id) gid: vec3<u32>,\n"
"    @builtin(local_invocation_id) lid: vec3<u32>)\n"
"{\n"
"    let slot = gid.x;\n"
"    let in_range = slot < batch.count;\n"
"    var g: u32 = 0xFFFFFFFFu;\n"
"    var info_offset: u32 = 0u;\n"
"    var amin: vec3<f32> = vec3<f32>(0.0);\n"
"    var amax: vec3<f32> = vec3<f32>(0.0);\n"
"    if (in_range) {\n"
"        g = slot_to_group[slot];\n"
"        let info = group_info[g];\n"
"        info_offset = info.src_offset;\n"
"        let aabb = aabbs[slot];\n"
"        amin = aabb.mn;\n"
"        amax = aabb.mx;\n"
"    }\n"
"    let cap = batch.capacity;\n"
"    let gc = batch.group_count;\n"
"\n"
"    /* Check workgroup-uniformity in g. Thread 0 publishes its g; every thread\n"
"       compares and clears the uniform flag if different. Also require all\n"
"       threads in-range. */\n"
"    if (lid.x == 0u) {\n"
"        wg_first_g = g;\n"
"        atomicStore(&wg_uniform, 1u);\n"
"    }\n"
"    workgroupBarrier();\n"
"    if (!in_range || g != wg_first_g) {\n"
"        atomicStore(&wg_uniform, 0u);\n"
"    }\n"
"    workgroupBarrier();\n"
"    let uniform_wg = (atomicLoad(&wg_uniform) == 1u);\n"
"    let g_uniform = wg_first_g;\n"
"\n"
"    if (view_data.flags.x != 0u) {\n"
"        var pass_main = false;\n"
"        if (in_range && test_planes(0u, amin, amax)) {\n"
"            pass_main = true;\n"
"            if (view_data.flags.z != 0u) {\n"
"                let cen = (amin + amax) * 0.5;\n"
"                let h = (amax - amin) * 0.5;\n"
"                let r_sq = dot(h, h);\n"
"                let d = cen - view_data.camera_pos.xyz;\n"
"                let d_sq = dot(d, d);\n"
"                pass_main = r_sq * view_data.screen.x >=\n"
"                    view_data.screen.y * d_sq;\n"
"            }\n"
"        }\n"
"        let info_u = group_info[g_uniform];\n"
"        append_visible(lid.x, uniform_wg,\n"
"            g_uniform, g,\n"
"            pass_main,\n"
"            info_u.src_offset, info_offset, slot);\n"
"    }\n"
"\n"
"    if (view_data.flags.y != 0u) {\n"
"        for (var c: u32 = 0u; c < 4u; c = c + 1u) {\n"
"            let base_planes = 6u + c * 6u;\n"
"            let view_idx = c + 1u;\n"
"            let args_idx_u = view_idx * gc + g_uniform;\n"
"            let args_idx = view_idx * gc + g;\n"
"            let info_u = group_info[g_uniform];\n"
"            let base_u = view_idx * cap + info_u.src_offset;\n"
"            let base = view_idx * cap + info_offset;\n"
"            let pass_c = in_range && test_planes(base_planes, amin, amax);\n"
"            append_visible(lid.x, uniform_wg,\n"
"                args_idx_u, args_idx, pass_c, base_u, base, slot);\n"
"        }\n"
"    }\n"
"}\n";

static WGPUBindGroupLayout flecsEngine_gpuCull_createViewLayout(
    WGPUDevice device)
{
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(flecsEngine_gpuCullViewUniforms_t)
        }
    };
    return wgpuDeviceCreateBindGroupLayout(device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 1,
            .entries = &entry
        });
}

static WGPUBindGroupLayout flecsEngine_gpuCull_createBatchLayout(
    WGPUDevice device)
{
    WGPUBindGroupLayoutEntry entries[6] = {
        { .binding = 0, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
        { .binding = 1, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
        { .binding = 2, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage } },
        { .binding = 3, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_Storage } },
        { .binding = 4, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_Storage } },
        { .binding = 5, .visibility = WGPUShaderStage_Compute,
          .buffer = { .type = WGPUBufferBindingType_Uniform,
                      .minBindingSize = sizeof(flecsEngine_gpuCullBatchInfo_t) } },
    };
    return wgpuDeviceCreateBindGroupLayout(device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 6,
            .entries = entries
        });
}

int flecsEngine_gpuCull_init(
    FlecsEngineImpl *engine)
{
    engine->gpu_cull.shader_module = flecsEngine_createShaderModule(
        engine->device, FLECS_ENGINE_GPU_CULL_WGSL);
    if (!engine->gpu_cull.shader_module) {
        return -1;
    }

    engine->gpu_cull.view_bind_layout =
        flecsEngine_gpuCull_createViewLayout(engine->device);
    engine->gpu_cull.batch_bind_layout =
        flecsEngine_gpuCull_createBatchLayout(engine->device);
    if (!engine->gpu_cull.view_bind_layout ||
        !engine->gpu_cull.batch_bind_layout)
    {
        return -1;
    }

    WGPUBindGroupLayout layouts[2] = {
        engine->gpu_cull.view_bind_layout,
        engine->gpu_cull.batch_bind_layout
    };
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = layouts
        });
    if (!pipeline_layout) {
        return -1;
    }

    engine->gpu_cull.pipeline = wgpuDeviceCreateComputePipeline(
        engine->device, &(WGPUComputePipelineDescriptor){
            .layout = pipeline_layout,
            .compute = {
                .module = engine->gpu_cull.shader_module,
                .entryPoint = WGPU_STR("cs_main")
            }
        });
    wgpuPipelineLayoutRelease(pipeline_layout);

    if (!engine->gpu_cull.pipeline) {
        return -1;
    }

    return 0;
}

void flecsEngine_gpuCull_fini(
    FlecsEngineImpl *engine)
{
    FLECS_WGPU_RELEASE(engine->gpu_cull.pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(engine->gpu_cull.view_bind_layout,
        wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(engine->gpu_cull.batch_bind_layout,
        wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(engine->gpu_cull.shader_module, wgpuShaderModuleRelease);
}

int flecsEngine_gpuCull_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl)
{
    if (view_impl->cull_view_uniform_buffer) {
        return 0;
    }

    view_impl->cull_view_uniform_buffer = wgpuDeviceCreateBuffer(
        engine->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(flecsEngine_gpuCullViewUniforms_t)
        });
    if (!view_impl->cull_view_uniform_buffer) {
        return -1;
    }

    WGPUBindGroupEntry entry = {
        .binding = 0,
        .buffer = view_impl->cull_view_uniform_buffer,
        .offset = 0,
        .size = sizeof(flecsEngine_gpuCullViewUniforms_t)
    };
    view_impl->cull_view_bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->gpu_cull.view_bind_layout,
            .entryCount = 1,
            .entries = &entry
        });
    if (!view_impl->cull_view_bind_group) {
        return -1;
    }

    return 0;
}

void flecsEngine_gpuCull_finiView(
    FlecsRenderViewImpl *view_impl)
{
    FLECS_WGPU_RELEASE(view_impl->cull_view_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(view_impl->cull_view_uniform_buffer, wgpuBufferRelease);
}

void flecsEngine_gpuCull_writeViewUniforms(
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl)
{
    if (!view_impl->cull_view_uniform_buffer) {
        return;
    }

    flecsEngine_gpuCullViewUniforms_t u;
    ecs_os_zeromem(&u);

    memcpy(&u.planes[0][0], view_impl->frustum_planes,
        sizeof(float) * 6 * 4);
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c ++) {
        memcpy(&u.planes[6 + c * 6][0],
            view_impl->cascade_frustum_planes[c],
            sizeof(float) * 6 * 4);
    }

    u.camera_pos[0] = view_impl->camera_pos[0];
    u.camera_pos[1] = view_impl->camera_pos[1];
    u.camera_pos[2] = view_impl->camera_pos[2];
    u.camera_pos[3] = 1.0f;

    u.screen[0] = view_impl->screen_cull_factor;
    u.screen[1] = view_impl->screen_cull_threshold;

    u.flags[0] = view_impl->frustum_valid ? 1u : 0u;
    u.flags[1] = view_impl->cascade_frustum_valid ? 1u : 0u;
    u.flags[2] = view_impl->screen_cull_valid ? 1u : 0u;

    wgpuQueueWriteBuffer(engine->queue,
        view_impl->cull_view_uniform_buffer, 0, &u, sizeof(u));
}

/* Dispatch one batch's compute cull. */
void flecsEngine_gpuCull_dispatchBatch(
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUComputePassEncoder cpass,
    flecsEngine_batch_t *buf)
{
    if (buf->flags & FLECS_BATCH_NO_GPU_CULL) {
        return;
    }
    int32_t count = buf->buffers.count;
    int32_t group_count = buf->buffers.group_count;
    if (!count || !group_count) {
        return;
    }

    WGPUBindGroup batch_bg = flecsEngine_batch_ensureCullBindGroup(engine, buf);
    if (!batch_bg) {
        return;
    }

    wgpuComputePassEncoderSetBindGroup(cpass, 0,
        view_impl->cull_view_bind_group, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(cpass, 1, batch_bg, 0, NULL);

    uint32_t wg = (uint32_t)((count + 63) / 64);
    wgpuComputePassEncoderDispatchWorkgroups(cpass, wg, 1, 1);
}

typedef struct {
    WGPUComputePassEncoder cpass;
    FlecsEngineImpl *engine;
    FlecsRenderViewImpl *view_impl;
} flecsEngine_gpuCull_dispatchCtx_t;

static void flecsEngine_gpuCull_dispatchBatchSet(
    ecs_world_t *world,
    flecsEngine_gpuCull_dispatchCtx_t *ctx,
    const FlecsRenderBatchSet *batch_set);

static void flecsEngine_gpuCull_dispatchOne(
    ecs_world_t *world,
    flecsEngine_gpuCull_dispatchCtx_t *ctx,
    ecs_entity_t batch_entity)
{
    const FlecsRenderBatchSet *nested = ecs_get(
        world, batch_entity, FlecsRenderBatchSet);
    if (nested) {
        flecsEngine_gpuCull_dispatchBatchSet(world, ctx, nested);
        return;
    }

    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->ctx) {
        return;
    }

    flecsEngine_batch_t *buf = flecsEngine_batch_getCullBuf(batch);
    if (!buf) {
        return;
    }

    flecsEngine_gpuCull_dispatchBatch(ctx->engine, ctx->view_impl,
        ctx->cpass, buf);
}

static void flecsEngine_gpuCull_dispatchBatchSet(
    ecs_world_t *world,
    flecsEngine_gpuCull_dispatchCtx_t *ctx,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);
    for (int32_t i = 0; i < count; i ++) {
        if (!batches[i]) continue;
        flecsEngine_gpuCull_dispatchOne(world, ctx, batches[i]);
    }
}

void flecsEngine_gpuCull_dispatchAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t view_entity,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("GpuCullDispatch");

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPUComputePassTimestampWrites ts_writes;
    int ts_pair = flecsEngine_gpuTiming_allocPair(engine, "Cull");
    flecsEngine_gpuTiming_computePassTimestamps(engine, ts_pair, &ts_writes);

    WGPUComputePassDescriptor desc = {
        .timestampWrites = ts_pair >= 0 ? &ts_writes : NULL
    };
    WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(
        encoder, &desc);
    if (!cpass) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    wgpuComputePassEncoderSetPipeline(cpass, engine->gpu_cull.pipeline);

    flecsEngine_gpuCull_dispatchCtx_t ctx = {
        .cpass = cpass,
        .engine = engine,
        .view_impl = view_impl
    };

    flecsEngine_gpuCull_dispatchBatchSet(world, &ctx, batch_set);

    wgpuComputePassEncoderEnd(cpass);
    wgpuComputePassEncoderRelease(cpass);

    FLECS_TRACY_ZONE_END;
}
