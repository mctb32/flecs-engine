#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

#define FLECS_BEVEL_BOX_QUADS    (6)
#define FLECS_BEVEL_BOX_BEVELS   (12)
#define FLECS_BEVEL_BOX_CORNERS  (8)
#define FLECS_BEVEL_BOX_MAX_SEGMENTS (16)

/* [segments][smooth] where smooth index: 0=flat, 1=smooth */
typedef struct {
    flecsEngine_batch_t quad_batch;
    flecsEngine_batch_t bevel_batches[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    flecsEngine_batch_t corner_batches[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    flecsEngine_batch_buffers_t buffers;
    bool owns_material_data;
} flecsEngine_bevel_box_batch_t;

static void flecsEngine_bevel_box_setInstance(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float lc0_x, float lc0_y, float lc0_z,
    float lc1_x, float lc1_y, float lc1_z,
    float lc2_x, float lc2_y, float lc2_z,
    float lp_x, float lp_y, float lp_z)
{
    out->c0.x = wt->m[0][0]*lc0_x + wt->m[1][0]*lc0_y + wt->m[2][0]*lc0_z;
    out->c0.y = wt->m[0][1]*lc0_x + wt->m[1][1]*lc0_y + wt->m[2][1]*lc0_z;
    out->c0.z = wt->m[0][2]*lc0_x + wt->m[1][2]*lc0_y + wt->m[2][2]*lc0_z;

    out->c1.x = wt->m[0][0]*lc1_x + wt->m[1][0]*lc1_y + wt->m[2][0]*lc1_z;
    out->c1.y = wt->m[0][1]*lc1_x + wt->m[1][1]*lc1_y + wt->m[2][1]*lc1_z;
    out->c1.z = wt->m[0][2]*lc1_x + wt->m[1][2]*lc1_y + wt->m[2][2]*lc1_z;

    out->c2.x = wt->m[0][0]*lc2_x + wt->m[1][0]*lc2_y + wt->m[2][0]*lc2_z;
    out->c2.y = wt->m[0][1]*lc2_x + wt->m[1][1]*lc2_y + wt->m[2][1]*lc2_z;
    out->c2.z = wt->m[0][2]*lc2_x + wt->m[1][2]*lc2_y + wt->m[2][2]*lc2_z;

    out->c3.x = wt->m[0][0]*lp_x + wt->m[1][0]*lp_y + wt->m[2][0]*lp_z + wt->m[3][0];
    out->c3.y = wt->m[0][1]*lp_x + wt->m[1][1]*lp_y + wt->m[2][1]*lp_z + wt->m[3][1];
    out->c3.z = wt->m[0][2]*lp_x + wt->m[1][2]*lp_y + wt->m[2][2]*lp_z + wt->m[3][2];
}

static void flecsEngine_bevel_box_generateQuads(
    FlecsInstanceTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    float fw = w - 2*r, fh = h - 2*r, fd = d - 2*r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        fw,0,0, 0,fh,0, 0,0,1, 0,0,hd);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        -fw,0,0, 0,fh,0, 0,0,-1, 0,0,-hd);
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        0,0,-fd, 0,fh,0, 1,0,0, hw,0,0);
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        0,0,fd, 0,fh,0, -1,0,0, -hw,0,0);
    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        fw,0,0, 0,0,-fd, 0,1,0, 0,hh,0);
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        fw,0,0, 0,0,fd, 0,-1,0, 0,-hh,0);
}

static void flecsEngine_bevel_box_generateBevels(
    FlecsInstanceTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
    float fw = w - 2*r, fh = h - 2*r, fd = d - 2*r;
    float s = 2 * r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        fw,0,0, 0,s,0, 0,0,s, 0,hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        -fw,0,0, 0,s,0, 0,0,-s, 0,hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        -fw,0,0, 0,-s,0, 0,0,s, 0,-(hh-r),hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        fw,0,0, 0,-s,0, 0,0,-s, 0,-(hh-r),-(hd-r));

    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        0,-fh,0, s,0,0, 0,0,s, hw-r,0,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        0,fh,0, -s,0,0, 0,0,s, -(hw-r),0,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+6], wt,
        0,fh,0, s,0,0, 0,0,-s, hw-r,0,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+7], wt,
        0,-fh,0, -s,0,0, 0,0,-s, -(hw-r),0,-(hd-r));

    flecsEngine_bevel_box_setInstance(&t[base+8], wt,
        0,0,-fd, 0,s,0, s,0,0, hw-r,hh-r,0);
    flecsEngine_bevel_box_setInstance(&t[base+9], wt,
        0,0,fd, 0,s,0, -s,0,0, -(hw-r),hh-r,0);
    flecsEngine_bevel_box_setInstance(&t[base+10], wt,
        0,0,fd, 0,-s,0, s,0,0, hw-r,-(hh-r),0);
    flecsEngine_bevel_box_setInstance(&t[base+11], wt,
        0,0,-fd, 0,-s,0, -s,0,0, -(hw-r),-(hh-r),0);
}

static void flecsEngine_bevel_box_generateCorners(
    FlecsInstanceTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
    float s = 2 * r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        s,0,0, 0,s,0, 0,0,s, hw-r,hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        s,0,0, 0,-s,0, 0,0,-s, hw-r,-(hh-r),-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        -s,0,0, 0,s,0, 0,0,-s, -(hw-r),hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        -s,0,0, 0,-s,0, 0,0,s, -(hw-r),-(hh-r),hd-r);

    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        0,s,0, s,0,0, 0,0,-s, hw-r,hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        0,-s,0, s,0,0, 0,0,s, hw-r,-(hh-r),hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+6], wt,
        0,s,0, -s,0,0, 0,0,s, -(hw-r),hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+7], wt,
        0,-s,0, -s,0,0, 0,0,-s, -(hw-r),-(hh-r),-(hd-r));
}

static void flecsEngine_bevel_box_fillMaterialData(
    flecsEngine_batch_buffers_t *buf,
    int32_t base,
    int32_t count,
    const FlecsEngineImpl *engine,
    const FlecsRgba *color,
    const FlecsPbrMaterial *pbr,
    const FlecsEmissive *emissive)
{
    FlecsRgba c = color ? *color :
        *flecsEngine_defaultAttrCache_getColor(engine, 1);
    FlecsPbrMaterial p = pbr ? *pbr :
        *flecsEngine_defaultAttrCache_getMaterial(engine, 1);
    FlecsEmissive e = emissive ? *emissive :
        *flecsEngine_defaultAttrCache_getEmissive(engine, 1);

    for (int32_t i = 0; i < count; i ++) {
        buf->cpu_colors[base + i] = c;
        buf->cpu_pbr_materials[base + i] = p;
        buf->cpu_emissives[base + i] = e;
    }
}

static void flecsEngine_bevel_box_fillMaterialId(
    flecsEngine_batch_buffers_t *buf,
    int32_t base,
    int32_t count,
    const FlecsMaterialId *material_id)
{
    FlecsMaterialId mat = material_id[0];
    for (int32_t i = 0; i < count; i ++) {
        buf->cpu_material_ids[base + i] = mat;
    }
}

static int32_t flecsEngine_bevel_box_clampSegments(
    int32_t segments)
{
    if (segments < 1) return 1;
    if (segments > FLECS_BEVEL_BOX_MAX_SEGMENTS) {
        return FLECS_BEVEL_BOX_MAX_SEGMENTS;
    }
    return segments;
}

static void flecsEngine_bevel_box_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    flecsEngine_batch_buffers_t *buf = &ctx->buffers;
    bool owns_material_data = ctx->owns_material_data;

    /* Pass 1: count instances per sub-batch */
    ctx->quad_batch.count = 0;
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        ctx->bevel_batches[s][0].count = 0;
        ctx->bevel_batches[s][1].count = 0;
        ctx->corner_batches[s][0].count = 0;
        ctx->corner_batches[s][1].count = 0;
    }

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    while (ecs_query_next(&it)) {
        const FlecsBevel *bevel = ecs_field(&it, FlecsBevel, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            int32_t seg = flecsEngine_bevel_box_clampSegments(
                bevel[i].segments);
            int32_t sm = bevel[i].smooth ? 1 : 0;
            ctx->quad_batch.count += FLECS_BEVEL_BOX_QUADS;
            ctx->bevel_batches[seg][sm].count += FLECS_BEVEL_BOX_BEVELS;
            ctx->corner_batches[seg][sm].count += FLECS_BEVEL_BOX_CORNERS;
        }
    }

    /* Compute total and assign offsets into shared buffer */
    int32_t total = ctx->quad_batch.count;
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        for (int32_t sm = 0; sm < 2; sm ++) {
            total += ctx->bevel_batches[s][sm].count;
            total += ctx->corner_batches[s][sm].count;
        }
    }

    flecsEngine_batch_buffers_ensureCapacity(engine, buf, total);

    {
        int32_t offset = 0;
        ctx->quad_batch.offset = offset;
        offset += ctx->quad_batch.count;
        for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
            for (int32_t sm = 0; sm < 2; sm ++) {
                ctx->bevel_batches[s][sm].offset = offset;
                offset += ctx->bevel_batches[s][sm].count;
                ctx->corner_batches[s][sm].offset = offset;
                offset += ctx->corner_batches[s][sm].count;
            }
        }
    }

    /* Pass 2: reset counts and fill instances using offsets */
    int32_t quad_fill = 0;
    int32_t bevel_fill[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    int32_t corner_fill[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    memset(bevel_fill, 0, sizeof(bevel_fill));
    memset(corner_fill, 0, sizeof(corner_fill));

    it = ecs_query_iter(world, batch->query);
    while (ecs_query_next(&it)) {
        const FlecsBox *box = ecs_field(&it, FlecsBox, 0);
        const FlecsBevel *bevel = ecs_field(&it, FlecsBevel, 1);
        const FlecsWorldTransform3 *wt =
            ecs_field(&it, FlecsWorldTransform3, 2);

        for (int32_t i = 0; i < it.count; i ++) {
            float w = box[i].x, h = box[i].y, d = box[i].z;
            float r = bevel[i].radius;
            int32_t seg = flecsEngine_bevel_box_clampSegments(
                bevel[i].segments);
            int32_t sm = bevel[i].smooth ? 1 : 0;

            int32_t qi = ctx->quad_batch.offset + quad_fill;
            int32_t bi = ctx->bevel_batches[seg][sm].offset +
                bevel_fill[seg][sm];
            int32_t ci = ctx->corner_batches[seg][sm].offset +
                corner_fill[seg][sm];

            flecsEngine_bevel_box_generateQuads(
                buf->cpu_transforms, qi, &wt[i], w, h, d, r);
            flecsEngine_bevel_box_generateBevels(
                buf->cpu_transforms, bi, &wt[i], w, h, d, r);
            flecsEngine_bevel_box_generateCorners(
                buf->cpu_transforms, ci, &wt[i], w, h, d, r);

            if (owns_material_data) {
                const FlecsRgba *colors =
                    ecs_field(&it, FlecsRgba, 3);
                const FlecsPbrMaterial *pbrs =
                    ecs_field(&it, FlecsPbrMaterial, 4);
                const FlecsEmissive *emissives =
                    ecs_field(&it, FlecsEmissive, 5);

                const FlecsRgba *c = colors ? &colors[i] : NULL;
                const FlecsPbrMaterial *p = pbrs ? &pbrs[i] : NULL;
                const FlecsEmissive *e = emissives ?
                    &emissives[i] : NULL;

                flecsEngine_bevel_box_fillMaterialData(
                    buf, qi, FLECS_BEVEL_BOX_QUADS, engine, c, p, e);
                flecsEngine_bevel_box_fillMaterialData(
                    buf, bi, FLECS_BEVEL_BOX_BEVELS, engine, c, p, e);
                flecsEngine_bevel_box_fillMaterialData(
                    buf, ci, FLECS_BEVEL_BOX_CORNERS, engine, c, p, e);
            } else {
                const FlecsMaterialId *mat =
                    ecs_field(&it, FlecsMaterialId, 3);

                flecsEngine_bevel_box_fillMaterialId(
                    buf, qi, FLECS_BEVEL_BOX_QUADS, mat);
                flecsEngine_bevel_box_fillMaterialId(
                    buf, bi, FLECS_BEVEL_BOX_BEVELS, mat);
                flecsEngine_bevel_box_fillMaterialId(
                    buf, ci, FLECS_BEVEL_BOX_CORNERS, mat);
            }

            quad_fill += FLECS_BEVEL_BOX_QUADS;
            bevel_fill[seg][sm] += FLECS_BEVEL_BOX_BEVELS;
            corner_fill[seg][sm] += FLECS_BEVEL_BOX_CORNERS;
        }
    }

    /* Single upload */
    buf->count = total;
    flecsEngine_batch_buffers_upload(engine, buf);
}

static void flecsEngine_bevel_box_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    flecsEngine_batch_draw(engine, pass, &ctx->quad_batch);
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        flecsEngine_batch_draw(engine, pass, &ctx->bevel_batches[s][0]);
        flecsEngine_batch_draw(engine, pass, &ctx->bevel_batches[s][1]);
        flecsEngine_batch_draw(engine, pass, &ctx->corner_batches[s][0]);
        flecsEngine_batch_draw(engine, pass, &ctx->corner_batches[s][1]);
    }
}

static void flecsEngine_bevel_box_free(void *ptr)
{
    flecsEngine_bevel_box_batch_t *ctx = ptr;
    flecsEngine_batch_buffers_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static flecsEngine_bevel_box_batch_t* flecsEngine_bevel_box_createCtx(
    ecs_world_t *world,
    bool owns_material_data)
{
    flecsEngine_bevel_box_batch_t *ctx =
        ecs_os_calloc_t(flecsEngine_bevel_box_batch_t);
    ctx->owns_material_data = owns_material_data;
    flecsEngine_batch_buffers_init(&ctx->buffers, owns_material_data, false);

    flecsEngine_batch_init(&ctx->quad_batch, world,
        flecsEngine_quad_getAsset(world), 0, owns_material_data, 0, NULL);
    ctx->quad_batch.buffers = &ctx->buffers;

    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        flecsEngine_batch_init(&ctx->bevel_batches[s][0], world,
            flecsEngine_bevel_getAssetImpl(world, s, false),
            0, owns_material_data, 0, NULL);
        ctx->bevel_batches[s][0].buffers = &ctx->buffers;

        flecsEngine_batch_init(&ctx->bevel_batches[s][1], world,
            flecsEngine_bevel_getAssetImpl(world, s, true),
            0, owns_material_data, 0, NULL);
        ctx->bevel_batches[s][1].buffers = &ctx->buffers;

        flecsEngine_batch_init(&ctx->corner_batches[s][0], world,
            flecsEngine_bevelCorner_getAssetImpl(world, s, false),
            0, owns_material_data, 0, NULL);
        ctx->corner_batches[s][0].buffers = &ctx->buffers;

        flecsEngine_batch_init(&ctx->corner_batches[s][1], world,
            flecsEngine_bevelCorner_getAssetImpl(world, s, true),
            0, owns_material_data, 0, NULL);
        ctx->corner_batches[s][1].buffers = &ctx->buffers;
    }

    return ctx;
}

ecs_entity_t flecsEngine_createBatch_bevel_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsBevel), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp,
                .trav = EcsIsA, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba),
            ecs_id(FlecsPbrMaterial),
            ecs_id(FlecsEmissive)
        },
        .extract_callback = flecsEngine_bevel_box_extract,
        .callback = flecsEngine_bevel_box_render,
        .ctx = flecsEngine_bevel_box_createCtx(world, true),
        .free_ctx = flecsEngine_bevel_box_free
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_bevel_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsBevel), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp,
                .trav = EcsIsA },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .extract_callback = flecsEngine_bevel_box_extract,
        .callback = flecsEngine_bevel_box_render,
        .ctx = flecsEngine_bevel_box_createCtx(world, false),
        .free_ctx = flecsEngine_bevel_box_free
    });

    return batch;
}
