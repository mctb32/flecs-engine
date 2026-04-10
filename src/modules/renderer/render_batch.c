#include <math.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);

ECS_DTOR(FlecsRenderBatch, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderBatch, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

/* Lazily create an empty bind group layout used as a placeholder for the
 * @group(1) PBR textures slot when a pipeline does not sample any material
 * textures. Keeps pipeline bind group indices contiguous so WebGPU accepts
 * pipeline switches between textured and non-textured draws. */
static WGPUBindGroupLayout flecsEngine_renderBatch_ensureEmptyBindLayout(
    FlecsEngineImpl *engine)
{
    if (engine->empty_bind_layout) {
        return engine->empty_bind_layout;
    }
    engine->empty_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 0,
            .entries = NULL
        });
    return engine->empty_bind_layout;
}

/* Lazily create an empty bind group matching the empty bind layout. Used to
 * "unbind" a slot when switching from a pipeline that uses it to one that
 * does not — WebGPU keeps previously bound groups across pipeline switches,
 * so a stale non-empty bind group at an empty layout slot fails validation. */
static WGPUBindGroup flecsEngine_renderBatch_ensureEmptyBindGroup(
    FlecsEngineImpl *engine)
{
    if (engine->empty_bind_group) {
        return engine->empty_bind_group;
    }
    WGPUBindGroupLayout layout =
        flecsEngine_renderBatch_ensureEmptyBindLayout(engine);
    if (!layout) {
        return NULL;
    }
    engine->empty_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = layout,
            .entryCount = 0,
            .entries = NULL
        });
    return engine->empty_bind_group;
}

static void flecsEngine_renderBatch_releaseImpl(
    FlecsRenderBatchImpl *ptr)
{
    if (ptr->pipeline_hdr) {
        wgpuRenderPipelineRelease(ptr->pipeline_hdr);
        ptr->pipeline_hdr = NULL;
    }

    if (ptr->pipeline_shadow) {
        wgpuRenderPipelineRelease(ptr->pipeline_shadow);
        ptr->pipeline_shadow = NULL;
    }
}

ECS_DTOR(FlecsRenderBatchImpl, ptr, {
    flecsEngine_renderBatch_releaseImpl(ptr);
})

ECS_MOVE(FlecsRenderBatchImpl, dst, src, {
    flecsEngine_renderBatch_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COMPONENT_DECLARE(FlecsRenderBatchSet);

ECS_CTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_init_t(NULL, &ptr->batches, ecs_entity_t, 0);
})

ECS_MOVE(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    dst->batches = ecs_vec_copy_t(NULL, &src->batches, ecs_entity_t);
})

ECS_DTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_fini_t(NULL, &ptr->batches, ecs_entity_t);
})

static bool flecsEngine_renderBatch_usesMaterialId(
    const FlecsRenderBatch *batch)
{
    for (int32_t i = 0; i < FLECS_ENGINE_INSTANCE_TYPES_MAX; i ++) {
        ecs_entity_t type = batch->instance_types[i];
        if (!type) {
            break;
        }

        if (type == ecs_id(FlecsMaterialId)) {
            return true;
        }
    }

    return false;
}

static int32_t flecsEngine_renderBatch_setupInstanceBindings(
    const ecs_world_t *world,
    FlecsRenderBatch *rb,
    int32_t location_offset,
    WGPUVertexBufferLayout *vertex_buffers,
    int32_t vertex_buffer_count,
    WGPUVertexAttribute *instance_attrs)
{
    int32_t attr_count = 0;
    for (int i = 0; i < FLECS_ENGINE_INSTANCE_TYPES_MAX; i ++) {
        ecs_entity_t type = rb->instance_types[i];
        if (!type) {
            break;
        }

        // Setup instance attributes
        int32_t count = flecsEngine_vertexAttrFromType(
            world, rb->instance_types[i], &instance_attrs[attr_count], 16, location_offset);

        if (count == -1) {
            continue;
        }

        vertex_buffers[vertex_buffer_count ++] = (WGPUVertexBufferLayout){
            .arrayStride = flecsEngine_type_sizeof(world, type),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = count,
            .attributes = &instance_attrs[attr_count],
        };

        location_offset += count;
        attr_count += count;
    }

    return vertex_buffer_count;
}

static WGPURenderPipeline flecsEngine_renderBatch_createShadowPipeline(
    const FlecsEngineImpl *engine,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count)
{
    if (!engine->shadow.shader_module || !engine->shadow.pass_bind_layout) {
        return NULL;
    }

    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &engine->shadow.pass_bind_layout
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .depthBias = 2,
        .depthBiasSlopeScale = 2.0f,
        .depthBiasClamp = 0.01f,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPUVertexState vertex_state = {
        .module = engine->shadow.shader_module,
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
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static WGPURenderPipeline flecsEngine_renderBatch_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    bool use_textures,
    const WGPUBlendState *blend,
    WGPUCullMode cull_mode,
    WGPUCompareFunction depth_test,
    bool depth_write,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
    WGPUTextureFormat color_format,
    uint32_t sample_count)
{
    /* Bind group layout order is fixed by update frequency:
     *   group 0 = scene globals (frame uniform, IBL, shadow, cluster, materials)
     *   group 1 = PBR material textures (scene-stable)
     *
     * Group 0 is always populated because every PBR shader samples the
     * frame uniform buffer at binding 0. Group 1 is populated iff the
     * pipeline samples PBR textures; otherwise a shared empty placeholder
     * keeps bind group indices contiguous across pipeline switches. */
    if (!engine->ibl_shadow_bind_layout) {
        return NULL;
    }

    WGPUBindGroupLayout empty_layout =
        flecsEngine_renderBatch_ensureEmptyBindLayout((FlecsEngineImpl*)engine);

    WGPUBindGroupLayout bind_layouts[2] = {
        engine->ibl_shadow_bind_layout,
        empty_layout
    };
    uint32_t bind_layout_count = 2u;

    if (use_textures) {
        WGPUBindGroupLayout tex_layout =
            flecsEngine_textures_ensureBindLayout((FlecsEngineImpl*)engine);
        if (tex_layout) {
            bind_layouts[1] = tex_layout;
        }
    }

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = bind_layout_count,
        .bindGroupLayouts = bind_layouts
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &pipeline_layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All,
        .blend = blend
    };

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth24Plus,
        .depthWriteEnabled = depth_write
            ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = depth_test,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };


    WGPUVertexState vertex_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->vertex_entry ? shader->vertex_entry : "vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPUFragmentState fragment_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->fragment_entry ? shader->fragment_entry : "fs_main"),
        .targetCount = 1,
        .targets = &color_target
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = &fragment_state,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = cull_mode,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE(sample_count)
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static void flecsEngine_renderBatch_logErr(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *msg)
{
    char *name = ecs_get_path(world, entity);
    ecs_err(msg, name);
    ecs_os_free(name);
}

static bool flecsEngine_renderBatch_validate(
    const ecs_world_t *world,
    ecs_entity_t e,
    const FlecsRenderBatch *rb)
{
    if (!rb->vertex_type) {
        flecsEngine_renderBatch_logErr(world, e,
            "missing vertex type for render batch %s");
        return false;
    }
    if (!rb->instance_types[0]) {
        flecsEngine_renderBatch_logErr(world, e,
            "missing instance type for render batch %s");
        return false;
    }
    if (!rb->shader) {
        flecsEngine_renderBatch_logErr(world, e,
            "missing shader asset for render batch %s");
        return false;
    }

    return true;
}

static void flecsEngine_renderBatch_setupShadowPipeline(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    FlecsRenderBatch *rb,
    FlecsRenderBatchImpl *impl,
    const WGPUVertexBufferLayout *vertex_buffers,
    int32_t vertex_buffer_count)
{
    if (!impl->uses_shadow || !engine->shadow.pass_bind_layout) {
        return;
    }

    if (impl->uses_textures &&
        rb->vertex_type == ecs_id(FlecsLitVertexUv))
    {
        /* For textured batches, build the shadow pipeline with
         * the non-UV vertex layout so that instance transform
         * locations match the shadow shader expectations. */
        WGPUVertexAttribute shadow_vert_attrs[16];
        WGPUVertexAttribute shadow_inst_attrs[256] = {0};
        WGPUVertexBufferLayout shadow_vbufs[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};

        int32_t sv_count = flecsEngine_vertexAttrFromType(
            world, ecs_id(FlecsLitVertex), shadow_vert_attrs, 16, 0);

        shadow_vbufs[0] = (WGPUVertexBufferLayout){
            .arrayStride = flecsEngine_type_sizeof(world, ecs_id(FlecsLitVertex)),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = sv_count,
            .attributes = shadow_vert_attrs
        };

        int32_t shadow_vbuf_count = flecsEngine_renderBatch_setupInstanceBindings(
            world, rb, sv_count,
            shadow_vbufs, 1, shadow_inst_attrs);

        impl->pipeline_shadow = flecsEngine_renderBatch_createShadowPipeline(
            engine,
            shadow_vbufs,
            (uint32_t)shadow_vbuf_count);
    } else {
        impl->pipeline_shadow = flecsEngine_renderBatch_createShadowPipeline(
            engine,
            vertex_buffers,
            (uint32_t)vertex_buffer_count);
    }
}

static void FlecsRenderBatch_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderBatch *rb = ecs_field(it, FlecsRenderBatch, 0);
    WGPUVertexAttribute instance_attrs[256] = {0};
    WGPUVertexBufferLayout vertex_buffers[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};
    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
    ecs_assert(engine != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        int32_t vertex_buffer_count = 0;
        FlecsRenderBatchImpl impl = {};
        
        if (!flecsEngine_renderBatch_validate(world, e, &rb[i])) {
            continue;
        }

        const FlecsShader *shader = ecs_get(world, rb[i].shader, FlecsShader);
        if (!shader) {
            flecsEngine_renderBatch_logErr(world, e,
                "invalid shader asset for render batch %s");
            continue;
        }

        const FlecsShaderImpl *shader_impl = flecsEngine_shader_ensureImpl(
            world, rb[i].shader);
        if (!shader_impl || !shader_impl->shader_module) {
            continue;
        }

        // Setup vertex attributes
        WGPUVertexAttribute vertex_attrs[16];
        int32_t vertex_attr_count = flecsEngine_vertexAttrFromType(
            world, rb[i].vertex_type, vertex_attrs, 16, 0);
        if (vertex_attr_count == -1) {
            continue;
        }

        vertex_buffers[0] = (WGPUVertexBufferLayout){
            .arrayStride = flecsEngine_type_sizeof(world, rb[i].vertex_type),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = vertex_attr_count,
            .attributes = vertex_attrs
        };
        vertex_buffer_count ++;

        // Setup instance data bindings
        vertex_buffer_count = flecsEngine_renderBatch_setupInstanceBindings(world, &rb[i],
            vertex_attr_count, vertex_buffers, vertex_buffer_count,
            instance_attrs);

        bool use_material_buffer = flecsEngine_renderBatch_usesMaterialId(&rb[i]);
        impl.uses_material = use_material_buffer;
        impl.uses_ibl = shader_impl->uses_ibl;
        impl.uses_shadow = shader_impl->uses_shadow;
        impl.uses_cluster = shader_impl->uses_cluster;
        impl.uses_textures = shader_impl->uses_textures;
        bool has_blend = rb[i].blend.color.operation != 0;
        const WGPUBlendState *blend = has_blend ? &rb[i].blend : NULL;
        WGPUCullMode cull_mode = rb[i].cull_mode;
        if (!cull_mode) {
            cull_mode = WGPUCullMode_Back;
        }

        /* Default depth settings when not explicitly configured */
        WGPUCompareFunction depth_test = rb[i].depth_test;
        bool depth_write = rb[i].depth_write;
        if (!depth_test) {
            depth_test = WGPUCompareFunction_Less;
            depth_write = true;
        }

        /* Every batch uses the scene-globals bind layout for @group(0) —
         * that's where the frame uniform buffer lives. */
        if (!flecsEngine_globals_ensureBindLayout(engine)) {
            flecsEngine_renderBatch_logErr(world, e,
                "failed to create render batch '%s': "
                "scene bind layout is not available");
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        uint32_t sample_count = engine->sample_count > 1
            ? (uint32_t)engine->sample_count : 1;

        impl.pipeline_hdr = flecsEngine_renderBatch_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.uses_textures,
            blend,
            cull_mode,
            depth_test,
            depth_write,
            vertex_buffers,
            (uint32_t)vertex_buffer_count,
            hdr_format,
            sample_count);
        if (!impl.pipeline_hdr) {
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        if (!has_blend) {
            flecsEngine_renderBatch_setupShadowPipeline(
                world, engine, &rb[i], &impl,
                vertex_buffers, vertex_buffer_count);
        }

        ecs_set_ptr(world, e, FlecsRenderBatchImpl, &impl);
    }
}

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchRender");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    FlecsRenderBatchImpl *impl = ecs_get_mut(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    if (pipeline != engine->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        engine->last_pipeline = pipeline;
    }

    /* WebGPU keeps previously bound bind groups across pipeline switches.
     * If a prior textured draw bound a non-empty group at @group(1), this
     * pipeline (which expects an empty layout there) would fail validation.
     * Reset the slot with an empty bind group when this pipeline doesn't use
     * textures; the textured render callbacks overwrite @group(1) themselves. */
    if (!impl->uses_textures) {
        WGPUBindGroup empty = flecsEngine_renderBatch_ensureEmptyBindGroup(
            (FlecsEngineImpl*)engine);
        if (empty) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, empty, 0, NULL);
        }
    }

    /* Every shader samples the frame uniform buffer at group(0) binding(0),
     * so the scene-globals bind group is always bound — regardless of
     * whether the shader uses IBL, shadow, cluster, or materials. */
    {
        ecs_entity_t hdri = view->hdri;
        if (!hdri) {
            hdri = engine->sky_background_hdri;
        }

        FlecsHdriImpl *ibl = ecs_get_mut(world, hdri, FlecsHdriImpl);
        ecs_assert(ibl != NULL, ECS_INTERNAL_ERROR, NULL);

        /* Recreate combined bind group if scene resources changed
         * (shadow atlas resize, cluster reallocation, or materials
         * buffer reallocation). */
        if (ibl->scene_bind_version != engine->scene_bind_version) {
            flecsEngine_globals_createBindGroup(engine, ibl);
        }

        /* Scene globals (frame uniform + IBL + shadow + cluster + materials)
         * live at @group(0). */
        wgpuRenderPassEncoderSetBindGroup(
            pass, 0, ibl->ibl_shadow_bind_group, 0, NULL);
    }

    batch->callback(world, engine, pass, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchExtract");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->extract_callback) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    batch->extract_callback(world, engine, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_extractShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchExtractShadow");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->shadow_extract_callback) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    batch->shadow_extract_callback(world, engine, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchShadow");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    const FlecsRenderBatchImpl *impl = ecs_get(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl || !impl->pipeline_shadow || !impl->uses_shadow) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_shadow;

    if (pipeline != engine->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        engine->last_pipeline = pipeline;
    }

    wgpuRenderPassEncoderSetBindGroup(
        pass, 0, engine->shadow.pass_bind_groups[engine->shadow.current_cascade],
        0, NULL);

    if (batch->shadow_callback) {
        batch->shadow_callback(world, engine, pass, batch);
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchSet);

    ecs_set_hooks(world, FlecsRenderBatch, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatch),
        .dtor = ecs_dtor(FlecsRenderBatch),
        .on_set = FlecsRenderBatch_on_set
    });

    ecs_set_hooks(world, FlecsRenderBatchImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatchImpl),
        .dtor = ecs_dtor(FlecsRenderBatchImpl)
    });

    ecs_set_hooks(world, FlecsRenderBatchSet, {
        .ctor = ecs_ctor(FlecsRenderBatchSet),
        .move = ecs_move(FlecsRenderBatchSet),
        .copy = ecs_copy(FlecsRenderBatchSet),
        .dtor = ecs_dtor(FlecsRenderBatchSet)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderBatchSet),
        .members = {
            { .name = "batches", .type = flecsEngine_vecEntity(world) }
        }
    });
}
