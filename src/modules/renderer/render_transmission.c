#include "renderer.h"
#include "flecs_engine.h"

/* Maximum number of mips in the opaque snapshot pyramid. Higher roughness
 * values sample higher mips. Beyond ~6 the resolution is too low to contribute
 * meaningfully, so cap there. */
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_MAX_MIPS 6u

/* Per-mip sample counts for the GGX prefilter. Each entry is the
 * number of Hammersley-importance-sampled GGX taps the prefilter shader
 * takes per fragment when writing that mip.
 *
 * Noise from under-sampling a wide GGX lobe dominates at high roughness,
 * while low roughness is near-converged with very few taps. Mip
 * resolution also halves each step, so pushing sample work toward the
 * small high-roughness mips costs very little in total.
 *
 * Index 0 is unused (mip 0 is a straight copy of the scene color and
 * never runs the prefilter shader). Tune these individually. */
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_0   0u  /* unused */
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_1  16u
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_2  64u
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_3  64u
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_4  256u
#define FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_5  512u

static const uint32_t flecsEngine_prefilter_sample_counts[] =
{
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_0,
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_1,
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_2,
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_3,
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_4,
    FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_5
};

/* Dynamic uniform buffer slots are aligned to the WebGPU minimum
 * uniform buffer offset alignment (256 bytes). Each mip stores one
 * FlecsTransmissionPrefilterUniform at a 256-byte-aligned slot. */
#define FLECS_ENGINE_UNIFORM_OFFSET_ALIGNMENT 256u

typedef struct {
    float alpha;         /* Target roughness for this mip */
    float source_lod;    /* Source pyramid LOD to sample from */
    float sample_count;  /* Number of Hammersley taps */
    float _pad;
} FlecsTransmissionPrefilterUniform;

/* Downsample shader: fullscreen triangle + 13-tap Jimenez / Call-of-Duty
 * style filter. Produces the gaussian-like source pyramid that feeds the
 * GGX prefilter pass below. */
static const char *kDownsampleShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var src_tex : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_smp : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let ps = 1.0 / vec2<f32>(textureDimensions(src_tex));\n"
    "  let pl = 2.0 * ps;\n"
    "  let ns = -1.0 * ps;\n"
    "  let nl = -2.0 * ps;\n"
    "  let uv = in.uv;\n"
    "  let a = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, pl.y), 0.0).rgb;\n"
    "  let b = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(0.00, pl.y), 0.0).rgb;\n"
    "  let c = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, pl.y), 0.0).rgb;\n"
    "  let d = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, 0.00), 0.0).rgb;\n"
    "  let e = textureSampleLevel(src_tex, src_smp, uv, 0.0).rgb;\n"
    "  let f = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, 0.00), 0.0).rgb;\n"
    "  let g = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(nl.x, nl.y), 0.0).rgb;\n"
    "  let h = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(0.00, nl.y), 0.0).rgb;\n"
    "  let i = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(pl.x, nl.y), 0.0).rgb;\n"
    "  let j = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ns.x, ps.y), 0.0).rgb;\n"
    "  let k = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ps.x, ps.y), 0.0).rgb;\n"
    "  let l = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ns.x, ns.y), 0.0).rgb;\n"
    "  let m = textureSampleLevel(src_tex, src_smp, uv + vec2<f32>(ps.x, ns.y), 0.0).rgb;\n"
    "  var sample = (a + c + g + i) * 0.03125;\n"
    "  sample += (b + d + f + h) * 0.0625;\n"
    "  sample += (e + j + k + l + m) * 0.125;\n"
    "  return vec4<f32>(sample, 1.0);\n"
    "}\n";

/* GGX prefilter shader — per-mip pre-integration of the GGX lobe.
 *
 * For each output pixel, takes `sample_count` Hammersley quasi-random
 * samples, importance-samples the isotropic GGX distribution at the mip's
 * roughness, projects each sampled microfacet direction onto screen space
 * as a UV offset, and weighted-averages taps read from the source
 * gaussian pyramid. The source LOD is chosen so that successive samples
 * cover adjacent texels (mitigating aliasing at high roughness without
 * blowing up the sample count).
 *
 * Each output mip corresponds to a specific `alpha` — the transmission
 * shader then just samples `opaque_snapshot` at
 * `LOD = roughness * max_mip` to get the correct pre-filtered result. */
static const char *kPrefilterShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL

    "struct PrefilterUniforms {\n"
    "  alpha : f32,\n"
    "  source_lod : f32,\n"
    "  sample_count : f32,\n"
    "  _pad : f32,\n"
    "};\n"

    "@group(0) @binding(0) var src_tex : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_smp : sampler;\n"
    "@group(0) @binding(2) var<uniform> prefilter : PrefilterUniforms;\n"

    "const PI : f32 = 3.14159265359;\n"

    /* Van der Corput radical-inverse base 2 (bit reversal). */
    "fn radical_inverse_vdc(bits_in : u32) -> f32 {\n"
    "  var bits = bits_in;\n"
    "  bits = (bits << 16u) | (bits >> 16u);\n"
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
    "  return f32(bits) * 2.3283064365386963e-10;\n"
    "}\n"

    "fn hammersley(i : u32, N : u32) -> vec2<f32> {\n"
    "  return vec2<f32>(f32(i) / f32(N), radical_inverse_vdc(i));\n"
    "}\n"

    /* Importance-sample the isotropic GGX distribution. Returns a 2D
     * tangent-plane offset (sin_theta * (cos_phi, sin_phi)). The xy of
     * the 3D microfacet normal projects to exactly this, and at alpha=1
     * the magnitude covers [0,1] uniformly over the hemisphere.
     * `phi_offset` rotates the azimuth so neighboring pixels sample
     * different angular slices and the structured Hammersley pattern
     * decorrelates into high-frequency noise. */
    "fn sample_ggx_offset(\n"
    "  xi : vec2<f32>, alpha : f32, phi_offset : f32) -> vec2<f32> {\n"
    "  let phi = 2.0 * PI * xi.x + phi_offset;\n"
    "  let a2 = alpha * alpha;\n"
    "  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));\n"
    "  let sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));\n"
    "  return vec2<f32>(cos(phi), sin(phi)) * sin_theta;\n"
    "}\n"

    /* Cheap per-pixel hash in [0, 2*PI) used to rotate the Hammersley
     * sample set. The exact values don't matter — it just needs to
     * decorrelate between adjacent pixels so the fixed sample pattern
     * turns into noise instead of visible streaks. */
    "fn noise_phi(uv : vec2<f32>) -> f32 {\n"
    "  let dims = vec2<f32>(textureDimensions(src_tex));\n"
    "  let p = uv * dims;\n"
    "  let n = fract(sin(dot(p, vec2<f32>(12.9898, 78.233))) * 43758.5453);\n"
    "  return n * 2.0 * PI;\n"
    "}\n"

    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let alpha = prefilter.alpha;\n"
    "  let src_lod = prefilter.source_lod;\n"
    "  let sample_count = u32(prefilter.sample_count);\n"
    "  let phi_offset = noise_phi(in.uv);\n"

    /* Max UV radius covered by the prefilter lobe. 0.25 means rough
     * transmission sees up to ~25% of the image around the refracted UV,
     * which is enough to blend background detail without bleeding across
     * the whole screen. The angular offset (sin_theta in [0,1]) scales by
     * this to get the UV offset. */
    "  let max_radius_uv = 0.25;\n"

    "  var sum = vec3<f32>(0.0);\n"
    "  var total_weight = 0.0;\n"
    "  for (var i = 0u; i < sample_count; i = i + 1u) {\n"
    "    let xi = hammersley(i, sample_count);\n"
    "    let off = sample_ggx_offset(xi, alpha, phi_offset);\n"
    "    let offset_uv = off * max_radius_uv;\n"

    /* cos_theta weighting (Lambertian) keeps the integral stable and
     * matches the convention used by IBL prefilter. sin_theta^2 + cos_theta^2 = 1
     * so we recover it from the offset magnitude. */
    "    let r2 = dot(off, off);\n"
    "    let cos_theta = sqrt(max(0.0, 1.0 - r2));\n"

    "    let tap = textureSampleLevel(\n"
    "      src_tex, src_smp, in.uv + offset_uv, src_lod).rgb;\n"
    "    sum = sum + tap * cos_theta;\n"
    "    total_weight = total_weight + cos_theta;\n"
    "  }\n"
    "  return vec4<f32>(sum / max(total_weight, 0.0001), 1.0);\n"
    "}\n";

static uint32_t flecsEngine_transmission_computeMipCount(
    uint32_t width,
    uint32_t height)
{
    uint32_t max_dim = width > height ? width : height;
    uint32_t mips = 1u;
    while ((max_dim >> mips) > 0u && mips < FLECS_ENGINE_OPAQUE_SNAPSHOT_MAX_MIPS) {
        mips ++;
    }
    return mips;
}

static void flecsEngine_transmission_releaseTexture(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_mip_views) {
        for (uint32_t i = 0; i < engine->opaque_snapshot_mip_count; i ++) {
            if (engine->opaque_snapshot_mip_views[i]) {
                wgpuTextureViewRelease(engine->opaque_snapshot_mip_views[i]);
            }
        }
        ecs_os_free(engine->opaque_snapshot_mip_views);
        engine->opaque_snapshot_mip_views = NULL;
    }
    if (engine->opaque_snapshot_view) {
        wgpuTextureViewRelease(engine->opaque_snapshot_view);
        engine->opaque_snapshot_view = NULL;
    }
    if (engine->opaque_snapshot) {
        wgpuTextureRelease(engine->opaque_snapshot);
        engine->opaque_snapshot = NULL;
    }

    if (engine->opaque_snapshot_source_mip_views) {
        for (uint32_t i = 0; i < engine->opaque_snapshot_mip_count; i ++) {
            if (engine->opaque_snapshot_source_mip_views[i]) {
                wgpuTextureViewRelease(
                    engine->opaque_snapshot_source_mip_views[i]);
            }
        }
        ecs_os_free(engine->opaque_snapshot_source_mip_views);
        engine->opaque_snapshot_source_mip_views = NULL;
    }
    if (engine->opaque_snapshot_source_view) {
        wgpuTextureViewRelease(engine->opaque_snapshot_source_view);
        engine->opaque_snapshot_source_view = NULL;
    }
    if (engine->opaque_snapshot_source) {
        wgpuTextureRelease(engine->opaque_snapshot_source);
        engine->opaque_snapshot_source = NULL;
    }

    /* The prefilter bind group references the source view — when the
     * source is rebuilt, the bind group must be rebuilt too. */
    if (engine->opaque_snapshot_prefilter_bind_group) {
        wgpuBindGroupRelease(engine->opaque_snapshot_prefilter_bind_group);
        engine->opaque_snapshot_prefilter_bind_group = NULL;
    }

    engine->opaque_snapshot_width = 0;
    engine->opaque_snapshot_height = 0;
    engine->opaque_snapshot_mip_count = 0;
}

static bool flecsEngine_transmission_ensureDownsamplePipeline(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_downsample_pipeline) {
        return true;
    }

    if (!engine->opaque_snapshot_sampler) {
        engine->opaque_snapshot_sampler = wgpuDeviceCreateSampler(
            engine->device, &(WGPUSamplerDescriptor){
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
        if (!engine->opaque_snapshot_sampler) {
            return false;
        }
    }

    if (!engine->opaque_snapshot_downsample_layout) {
        WGPUBindGroupLayoutEntry entries[2] = {
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false
                }
            },
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Filtering
                }
            }
        };
        engine->opaque_snapshot_downsample_layout =
            wgpuDeviceCreateBindGroupLayout(
                engine->device, &(WGPUBindGroupLayoutDescriptor){
                    .entryCount = 2,
                    .entries = entries
                });
        if (!engine->opaque_snapshot_downsample_layout) {
            return false;
        }
    }

    WGPUShaderModule shader = flecsEngine_createShaderModule(
        engine->device, kDownsampleShaderSource);
    if (!shader) {
        return false;
    }

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &engine->opaque_snapshot_downsample_layout
        });
    if (!pipeline_layout) {
        wgpuShaderModuleRelease(shader);
        return false;
    }

    WGPUColorTargetState color_target = {
        .format = engine->hdr_color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    engine->opaque_snapshot_downsample_pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = shader,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = shader,
                .entryPoint = WGPU_STR("fs_main"),
                .targetCount = 1,
                .targets = &color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);

    return engine->opaque_snapshot_downsample_pipeline != NULL;
}

static bool flecsEngine_transmission_ensurePrefilterPipeline(
    FlecsEngineImpl *engine)
{
    if (engine->opaque_snapshot_prefilter_pipeline) {
        return true;
    }

    if (!engine->opaque_snapshot_prefilter_layout) {
        WGPUBindGroupLayoutEntry entries[3] = {
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false
                }
            },
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Filtering
                }
            },
            {
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .buffer = {
                    .type = WGPUBufferBindingType_Uniform,
                    .hasDynamicOffset = true,
                    .minBindingSize = sizeof(FlecsTransmissionPrefilterUniform)
                }
            }
        };
        engine->opaque_snapshot_prefilter_layout =
            wgpuDeviceCreateBindGroupLayout(
                engine->device, &(WGPUBindGroupLayoutDescriptor){
                    .entryCount = 3,
                    .entries = entries
                });
        if (!engine->opaque_snapshot_prefilter_layout) {
            return false;
        }
    }

    /* Uniform buffer: one 256-byte-aligned slot per mip. Sized once for
     * the maximum mip count; we write per-mip before each pass. */
    if (!engine->opaque_snapshot_prefilter_uniforms) {
        engine->opaque_snapshot_prefilter_uniforms = wgpuDeviceCreateBuffer(
            engine->device, &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)FLECS_ENGINE_OPAQUE_SNAPSHOT_MAX_MIPS
                    * FLECS_ENGINE_UNIFORM_OFFSET_ALIGNMENT
            });
        if (!engine->opaque_snapshot_prefilter_uniforms) {
            return false;
        }
    }

    WGPUShaderModule shader = flecsEngine_createShaderModule(
        engine->device, kPrefilterShaderSource);
    if (!shader) {
        return false;
    }

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &engine->opaque_snapshot_prefilter_layout
        });
    if (!pipeline_layout) {
        wgpuShaderModuleRelease(shader);
        return false;
    }

    WGPUColorTargetState color_target = {
        .format = engine->hdr_color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    engine->opaque_snapshot_prefilter_pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = shader,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = shader,
                .entryPoint = WGPU_STR("fs_main"),
                .targetCount = 1,
                .targets = &color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);

    return engine->opaque_snapshot_prefilter_pipeline != NULL;
}

static bool flecsEngine_transmission_ensurePipelines(
    FlecsEngineImpl *engine)
{
    return flecsEngine_transmission_ensureDownsamplePipeline(engine)
        && flecsEngine_transmission_ensurePrefilterPipeline(engine);
}

static bool flecsEngine_transmission_createTexture(
    FlecsEngineImpl *engine,
    uint32_t width,
    uint32_t height)
{
    uint32_t mip_count = flecsEngine_transmission_computeMipCount(width, height);

    /* Source pyramid: holds the Jimenez gaussian pyramid that feeds the
     * GGX prefilter pass. Needs RenderAttachment (downsample target),
     * TextureBinding (prefilter source), CopyDst (initial scene copy into
     * mip 0), and CopySrc (mip 0 → opaque_snapshot mip 0 unchanged copy). */
    engine->opaque_snapshot_source = wgpuDeviceCreateTexture(
        engine->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_CopySrc
                   | WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, 1 },
            .format = engine->hdr_color_format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        });
    if (!engine->opaque_snapshot_source) {
        return false;
    }

    engine->opaque_snapshot_source_view = wgpuTextureCreateView(
        engine->opaque_snapshot_source, &(WGPUTextureViewDescriptor){
            .format = engine->hdr_color_format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1
        });
    if (!engine->opaque_snapshot_source_view) {
        return false;
    }

    engine->opaque_snapshot_source_mip_views = ecs_os_calloc_n(
        WGPUTextureView, mip_count);
    if (!engine->opaque_snapshot_source_mip_views) {
        return false;
    }
    for (uint32_t i = 0; i < mip_count; i ++) {
        engine->opaque_snapshot_source_mip_views[i] = wgpuTextureCreateView(
            engine->opaque_snapshot_source, &(WGPUTextureViewDescriptor){
                .format = engine->hdr_color_format,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = i,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1
            });
        if (!engine->opaque_snapshot_source_mip_views[i]) {
            return false;
        }
    }

    /* Destination pyramid: mip 0 is a copy of the opaque target, mips
     * 1..N are written by the GGX prefilter pass. The transmission shader
     * samples this texture at @group(0) @binding(3). */
    engine->opaque_snapshot = wgpuDeviceCreateTexture(
        engine->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_TextureBinding
                   | WGPUTextureUsage_CopyDst
                   | WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, 1 },
            .format = engine->hdr_color_format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        });
    if (!engine->opaque_snapshot) {
        return false;
    }

    engine->opaque_snapshot_view = wgpuTextureCreateView(
        engine->opaque_snapshot, &(WGPUTextureViewDescriptor){
            .format = engine->hdr_color_format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1
        });
    if (!engine->opaque_snapshot_view) {
        return false;
    }

    engine->opaque_snapshot_mip_views = ecs_os_calloc_n(
        WGPUTextureView, mip_count);
    if (!engine->opaque_snapshot_mip_views) {
        return false;
    }
    for (uint32_t i = 0; i < mip_count; i ++) {
        engine->opaque_snapshot_mip_views[i] = wgpuTextureCreateView(
            engine->opaque_snapshot, &(WGPUTextureViewDescriptor){
                .format = engine->hdr_color_format,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = i,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1
            });
        if (!engine->opaque_snapshot_mip_views[i]) {
            return false;
        }
    }

    /* Prefilter bind group: references the full-chain source view, the
     * linear sampler, and the dynamic-offset uniform buffer. Rebuilt on
     * every texture resize because the source view is recreated. */
    engine->opaque_snapshot_prefilter_bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->opaque_snapshot_prefilter_layout,
            .entryCount = 3,
            .entries = (WGPUBindGroupEntry[3]){
                {
                    .binding = 0,
                    .textureView = engine->opaque_snapshot_source_view
                },
                {
                    .binding = 1,
                    .sampler = engine->opaque_snapshot_sampler
                },
                {
                    .binding = 2,
                    .buffer = engine->opaque_snapshot_prefilter_uniforms,
                    .offset = 0,
                    .size = sizeof(FlecsTransmissionPrefilterUniform)
                }
            }
        });
    if (!engine->opaque_snapshot_prefilter_bind_group) {
        return false;
    }

    engine->opaque_snapshot_width = width;
    engine->opaque_snapshot_height = height;
    engine->opaque_snapshot_mip_count = mip_count;
    return true;
}

/* Run a downsample pass: read source_mip_views[i-1], write to
 * source_mip_views[i]. Builds the gaussian pyramid for the prefilter. */
static void flecsEngine_transmission_downsampleMip(
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    uint32_t dst_mip)
{
    WGPUBindGroupEntry bind_entries[2] = {
        {
            .binding = 0,
            .textureView = engine->opaque_snapshot_source_mip_views[dst_mip - 1]
        },
        {
            .binding = 1,
            .sampler = engine->opaque_snapshot_sampler
        }
    };
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->opaque_snapshot_downsample_layout,
            .entryCount = 2,
            .entries = bind_entries
        });
    if (!bind_group) {
        return;
    }

    WGPURenderPassColorAttachment color_attachment = {
        .view = engine->opaque_snapshot_source_mip_views[dst_mip],
        WGPU_DEPTH_SLICE
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_attachment
        });
    if (!pass) {
        wgpuBindGroupRelease(bind_group);
        return;
    }

    wgpuRenderPassEncoderSetPipeline(
        pass, engine->opaque_snapshot_downsample_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bind_group);
}

/* Run a GGX prefilter pass: read opaque_snapshot_source (full pyramid),
 * write opaque_snapshot_mip_views[dst_mip] pre-integrated for the alpha
 * corresponding to this mip. */
static void flecsEngine_transmission_prefilterMip(
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    uint32_t dst_mip)
{
    uint32_t offset = dst_mip * FLECS_ENGINE_UNIFORM_OFFSET_ALIGNMENT;

    WGPURenderPassColorAttachment color_attachment = {
        .view = engine->opaque_snapshot_mip_views[dst_mip],
        WGPU_DEPTH_SLICE
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_attachment
        });
    if (!pass) {
        return;
    }

    wgpuRenderPassEncoderSetPipeline(
        pass, engine->opaque_snapshot_prefilter_pipeline);
    wgpuRenderPassEncoderSetBindGroup(
        pass, 0,
        engine->opaque_snapshot_prefilter_bind_group,
        1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void flecsEngine_transmission_updateSnapshot(
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    WGPUTexture src_texture,
    uint32_t width,
    uint32_t height)
{
    if (!width || !height) {
        return;
    }

    if (!flecsEngine_transmission_ensurePipelines(engine)) {
        return;
    }

    /* (Re)create snapshot textures if dimensions changed. */
    if (engine->opaque_snapshot_width != width ||
        engine->opaque_snapshot_height != height)
    {
        flecsEngine_transmission_releaseTexture(engine);
        if (!flecsEngine_transmission_createTexture(engine, width, height)) {
            flecsEngine_transmission_releaseTexture(engine);
            return;
        }
        /* Rebuild the globals bind group so it picks up the new view */
        engine->scene_bind_version++;
    }

    uint32_t mip_count = engine->opaque_snapshot_mip_count;

    /* Pre-write all per-mip prefilter uniforms. wgpuQueueWriteBuffer
     * writes are scheduled before all encoded commands execute, so
     * writing them here before encoding the prefilter passes is safe. */
    for (uint32_t i = 1; i < mip_count; i ++) {
        /* PBR convention: perceptual roughness maps linearly to LOD,
         * GGX alpha is the square. The runtime shader samples at
         * lod = roughness * (mip_count - 1), so mip i corresponds to
         * perceptual roughness i/(N-1), which integrates with GGX
         * alpha = (i/(N-1))^2. Without the square, low roughness
         * materials look much blurrier than they should. */
        float perceptual = mip_count > 1u
            ? (float)i / (float)(mip_count - 1u)
            : 0.0f;
        float alpha = perceptual * perceptual;

        /* Per-mip sample count (see the FLECS_ENGINE_OPAQUE_SNAPSHOT_PREFILTER_SAMPLES_MIP_*
         * constants above). Low roughness is near-converged with few
         * taps; high roughness needs many more to hide the per-pixel
         * rotation noise against high-contrast backgrounds. */
        uint32_t sample_count = flecsEngine_prefilter_sample_counts[i];
        if (!sample_count) {
            sample_count = 1u;
        }
        float sample_count_f = (float)sample_count;

        /* Source LOD matches the per-sample footprint to a source texel.
         * Without this, each Hammersley tap reads from the sharp source
         * image and the fixed sample pattern shows through as visible
         * streaks when the background has high-contrast features. The
         * formula:
         *   radius_texels  = alpha * max_radius_uv * max_dim
         *   area_per_sample = radius_texels^2 / N    (approximate)
         *   src_lod = 0.5 * log2(area_per_sample)
         * picks the source mip where one texel covers roughly the area
         * each sample is responsible for. The per-mip sample_count_f is
         * fed in so more samples correctly pull the source LOD down. */
        float max_dim = (float)(width > height ? width : height);
        float radius_texels = alpha * 0.25f * max_dim;
        float src_lod = 0.5f * log2f(
            (radius_texels * radius_texels) / sample_count_f);
        if (src_lod < 0.0f) {
            src_lod = 0.0f;
        }

        FlecsTransmissionPrefilterUniform uniform = {
            .alpha = alpha,
            .source_lod = src_lod,
            .sample_count = sample_count_f,
            ._pad = 0.0f
        };
        wgpuQueueWriteBuffer(
            engine->queue,
            engine->opaque_snapshot_prefilter_uniforms,
            (uint64_t)i * FLECS_ENGINE_UNIFORM_OFFSET_ALIGNMENT,
            &uniform,
            sizeof(uniform));
    }

    /* 1. Copy the resolved color target to source mip 0. */
    wgpuCommandEncoderCopyTextureToTexture(
        encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = src_texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUTexelCopyTextureInfo){
            .texture = engine->opaque_snapshot_source,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUExtent3D){ width, height, 1 });

    /* 2. Build the source gaussian pyramid via Jimenez downsample. */
    for (uint32_t i = 1; i < mip_count; i ++) {
        flecsEngine_transmission_downsampleMip(engine, encoder, i);
    }

    /* 3. Copy source mip 0 to snapshot mip 0 unchanged — roughness 0
     * must read the raw scene, never a filtered version. */
    wgpuCommandEncoderCopyTextureToTexture(
        encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = engine->opaque_snapshot_source,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUTexelCopyTextureInfo){
            .texture = engine->opaque_snapshot,
            .mipLevel = 0,
            .origin = { 0, 0, 0 }
        },
        &(WGPUExtent3D){ width, height, 1 });

    /* 4. GGX prefilter each higher mip at its target roughness. */
    for (uint32_t i = 1; i < mip_count; i ++) {
        flecsEngine_transmission_prefilterMip(engine, encoder, i);
    }
}

void flecsEngine_transmission_release(
    FlecsEngineImpl *engine)
{
    flecsEngine_transmission_releaseTexture(engine);

    if (engine->opaque_snapshot_downsample_pipeline) {
        wgpuRenderPipelineRelease(engine->opaque_snapshot_downsample_pipeline);
        engine->opaque_snapshot_downsample_pipeline = NULL;
    }
    if (engine->opaque_snapshot_downsample_layout) {
        wgpuBindGroupLayoutRelease(engine->opaque_snapshot_downsample_layout);
        engine->opaque_snapshot_downsample_layout = NULL;
    }
    if (engine->opaque_snapshot_prefilter_pipeline) {
        wgpuRenderPipelineRelease(engine->opaque_snapshot_prefilter_pipeline);
        engine->opaque_snapshot_prefilter_pipeline = NULL;
    }
    if (engine->opaque_snapshot_prefilter_layout) {
        wgpuBindGroupLayoutRelease(engine->opaque_snapshot_prefilter_layout);
        engine->opaque_snapshot_prefilter_layout = NULL;
    }
    if (engine->opaque_snapshot_prefilter_uniforms) {
        wgpuBufferRelease(engine->opaque_snapshot_prefilter_uniforms);
        engine->opaque_snapshot_prefilter_uniforms = NULL;
    }
    if (engine->opaque_snapshot_sampler) {
        wgpuSamplerRelease(engine->opaque_snapshot_sampler);
        engine->opaque_snapshot_sampler = NULL;
    }
}
