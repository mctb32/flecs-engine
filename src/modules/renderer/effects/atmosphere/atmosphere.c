/* Physically-based sky and aerial perspective (Hillaire 2020).
 *
 * Pipeline, re-evaluated every frame:
 *   1. Transmittance LUT       (256x64, RGBA16F)
 *   2. Multi-scattering LUT    (32x32,  RGBA16F)
 *   3. Sky-view LUT            (192x108, RGBA16F)
 *   4. Aerial perspective LUT  (32x32x32 as 2D array, RGBA16F)
 *   5. Compose pass: samples sky-view for sky pixels, aerial LUT for scene
 *      pixels, adds the sun disk, writes to the effect output target.
 *
 * All raymarching is done in kilometres; scattering coefficients are per-km.
 */

#include <math.h>
#include <string.h>

#include "../../renderer.h"
#include "../../ibl/ibl_internal.h"
#include "../../../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsAtmosphere);
ECS_COMPONENT_DECLARE(FlecsAtmosphereImpl);

#define FLECS_ATMOS_TRANS_W (256u)
#define FLECS_ATMOS_TRANS_H (64u)
#define FLECS_ATMOS_MS_W    (32u)
#define FLECS_ATMOS_MS_H    (32u)
#define FLECS_ATMOS_SKYVIEW_W (192u)
#define FLECS_ATMOS_SKYVIEW_H (108u)
#define FLECS_ATMOS_AERIAL_W (32u)
#define FLECS_ATMOS_AERIAL_H (32u)
#define FLECS_ATMOS_AERIAL_SLICES (32u)

#define FLECS_ATMOS_FORMAT (WGPUTextureFormat_RGBA16Float)

/* -------------------------------------------------------------------------- */
/* Uniform block                                                              */
/* -------------------------------------------------------------------------- */

typedef struct FlecsAtmosphereUniform {
    mat4 inv_vp;
    /* xyz: camera world pos; w: camera altitude in km above sea level */
    float camera_pos_world[4];
    /* xyz: world-space direction *to* the sun; w: sun illuminance multiplier */
    float sun_direction[4];
    /* rgb: sun light color * intensity; a: 1 */
    float sun_color[4];
    float ground_albedo[4];
    /* x: planet radius (km), y: atmosphere top (km),
     * z: rayleigh scale height (km), w: mie scale height (km) */
    float atmosphere[4];
    /* x: mie anisotropy g, y: max aerial distance (km),
     * z: sun disk cos-half-angle, w: sun disk intensity */
    float mie_params[4];
    /* x: world units per km, y: 1 / world units per km,
     * z: sea_level_y, w: ground_altitude_km */
    float world_scale[4];
    float scat_rayleigh[4];   /* rgb: rayleigh scattering per km; w: 0 */
    float ext_mie[4];         /* x: mie scattering, y: mie extinction, zw: 0 */
    float abs_ozone[4];       /* rgb: ozone absorption per km; w: 0 */
    /* x: ap slices, y: 0, z: 0, w: 0 */
    float misc[4];
    /* x: aerial perspective intensity multiplier, yzw: unused. MUST stay
     * last to match the WGSL `AtmosUniforms` struct layout. */
    float aerial_params[4];
} FlecsAtmosphereUniform;

/* Aerial-perspective slice selector (one 16-byte uniform per slice, bound
 * separately so we don't have to mutate the main uniform mid-frame). */
typedef struct FlecsAtmosphereSliceUniform {
    uint32_t slice_index;
    uint32_t slice_count;
    float _pad[2];
} FlecsAtmosphereSliceUniform;

/* -------------------------------------------------------------------------- */
/* WGSL                                                                       */
/* -------------------------------------------------------------------------- */

static const char *kAtmosphereCommonWgsl =
"const PI : f32 = 3.14159265358979323846;\n"
"const PLANET_OFFSET : f32 = 0.001;\n"
"struct AtmosUniforms {\n"
"  inv_vp : mat4x4<f32>,\n"
"  camera_pos_world : vec4<f32>,\n"
"  sun_direction : vec4<f32>,\n"
"  sun_color : vec4<f32>,\n"
"  ground_albedo : vec4<f32>,\n"
"  atmosphere : vec4<f32>,\n"
"  mie_params : vec4<f32>,\n"
"  world_scale : vec4<f32>,\n"
"  scat_rayleigh : vec4<f32>,\n"
"  ext_mie : vec4<f32>,\n"
"  abs_ozone : vec4<f32>,\n"
"  misc : vec4<f32>,\n"
"  aerial_params : vec4<f32>,\n"
"};\n"
"struct Medium {\n"
"  scattering : vec3<f32>,\n"
"  extinction : vec3<f32>,\n"
"  scattering_rayleigh : vec3<f32>,\n"
"  scattering_mie : vec3<f32>,\n"
"};\n"
"fn bottomR(u : AtmosUniforms) -> f32 { return u.atmosphere.x; }\n"
"fn topR(u : AtmosUniforms) -> f32 { return u.atmosphere.y; }\n"
"fn sample_medium(alt_km : f32, u : AtmosUniforms) -> Medium {\n"
"  let density_ray = exp(-alt_km / u.atmosphere.z);\n"
"  let density_mie = exp(-alt_km / u.atmosphere.w);\n"
"  let density_ozone = max(0.0, 1.0 - abs(alt_km - 25.0) / 15.0);\n"
"  let scat_ray = u.scat_rayleigh.xyz * density_ray;\n"
"  let scat_mie = vec3<f32>(u.ext_mie.x) * density_mie;\n"
"  let ext_ray = scat_ray;\n"
"  let ext_mie = vec3<f32>(u.ext_mie.y) * density_mie;\n"
"  let abs_ozone = u.abs_ozone.xyz * density_ozone;\n"
"  var m : Medium;\n"
"  m.scattering_rayleigh = scat_ray;\n"
"  m.scattering_mie = scat_mie;\n"
"  m.scattering = scat_ray + scat_mie;\n"
"  m.extinction = ext_ray + ext_mie + abs_ozone;\n"
"  return m;\n"
"}\n"
"fn rayleigh_phase(c : f32) -> f32 {\n"
"  return (3.0 / (16.0 * PI)) * (1.0 + c * c);\n"
"}\n"
"fn mie_phase(c : f32, g : f32) -> f32 {\n"
"  let k = (3.0 / (8.0 * PI)) * (1.0 - g * g) / (2.0 + g * g);\n"
"  let denom = 1.0 + g * g - 2.0 * g * c;\n"
"  return k * (1.0 + c * c) / max(pow(max(denom, 1e-6), 1.5), 1e-6);\n"
"}\n"
/* Return t > 0 for the nearest positive intersection; < 0 if none. */
"fn ray_sphere_nearest(ro : vec3<f32>, rd : vec3<f32>, r : f32) -> f32 {\n"
"  let b = dot(ro, rd);\n"
"  let c = dot(ro, ro) - r * r;\n"
"  let disc = b * b - c;\n"
"  if (disc < 0.0) { return -1.0; }\n"
"  let sd = sqrt(disc);\n"
"  let t0 = -b - sd;\n"
"  let t1 = -b + sd;\n"
"  if (t0 >= 0.0) { return t0; }\n"
"  if (t1 >= 0.0) { return t1; }\n"
"  return -1.0;\n"
"}\n"
/* Bruneton 2008 transmittance LUT parameterisation (Hillaire 2020 uses it). */
"fn uv_to_trans_params(uv : vec2<f32>, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let Rt = topR(u);\n"
"  let H = sqrt(max(0.0, Rt * Rt - Rb * Rb));\n"
"  let rho = H * uv.y;\n"
"  let r = sqrt(rho * rho + Rb * Rb);\n"
"  let d_min = Rt - r;\n"
"  let d_max = rho + H;\n"
"  let d = d_min + uv.x * (d_max - d_min);\n"
"  var mu = 1.0;\n"
"  if (d > 0.0) {\n"
"    mu = (H * H - rho * rho - d * d) / (2.0 * r * d);\n"
"  }\n"
"  return vec2<f32>(r, clamp(mu, -1.0, 1.0));\n"
"}\n"
"fn trans_params_to_uv(r : f32, mu : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let Rt = topR(u);\n"
"  let H = sqrt(max(0.0, Rt * Rt - Rb * Rb));\n"
"  let rho = sqrt(max(0.0, r * r - Rb * Rb));\n"
"  let disc = max(0.0, r * r * (mu * mu - 1.0) + Rt * Rt);\n"
"  let d = max(0.0, -r * mu + sqrt(disc));\n"
"  let d_min = Rt - r;\n"
"  let d_max = rho + H;\n"
"  var x_mu = 0.0;\n"
"  if (d_max > d_min + 1e-6) { x_mu = (d - d_min) / (d_max - d_min); }\n"
"  let x_r = rho / max(H, 1e-6);\n"
"  return vec2<f32>(clamp(x_mu, 0.0, 1.0), clamp(x_r, 0.0, 1.0));\n"
"}\n"
"fn sample_transmittance_lut(trans : texture_2d<f32>, smp : sampler, r : f32, mu : f32, u : AtmosUniforms) -> vec3<f32> {\n"
"  let uv = trans_params_to_uv(r, mu, u);\n"
"  return textureSampleLevel(trans, smp, uv, 0.0).rgb;\n"
"}\n"
/* Multi-scattering LUT UV: x=cos(sun_zenith), y=altitude fraction. */
"fn ms_uv_to_params(uv : vec2<f32>, u : AtmosUniforms) -> vec2<f32> {\n"
"  let cos_sun = uv.x * 2.0 - 1.0;\n"
"  let alt = uv.y * (topR(u) - bottomR(u));\n"
"  return vec2<f32>(cos_sun, alt);\n"
"}\n"
"fn ms_params_to_uv(cos_sun : f32, r : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let alt = r - bottomR(u);\n"
"  return vec2<f32>(clamp(0.5 + 0.5 * cos_sun, 0.0, 1.0),\n"
"                   clamp(alt / (topR(u) - bottomR(u)), 0.0, 1.0));\n"
"}\n"
/* Sky-view LUT parameterisation: azimuth uniform, zenith split around horizon. */
"fn skyview_uv_to_dir(uv : vec2<f32>, view_r : f32, u : AtmosUniforms) -> vec3<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let horizon_cos = -sqrt(max(0.0, view_r * view_r - Rb * Rb)) / max(view_r, 1e-4);\n"
"  let horizon_ang = acos(clamp(horizon_cos, -1.0, 1.0));\n"
"  var zenith = 0.0;\n"
"  if (uv.y < 0.5) {\n"
"    let t = uv.y / 0.5;\n"
"    zenith = horizon_ang * t * t;\n"
"  } else {\n"
"    let t = (uv.y - 0.5) / 0.5;\n"
"    zenith = horizon_ang + (PI - horizon_ang) * t * t;\n"
"  }\n"
"  let azim = (uv.x - 0.5) * 2.0 * PI;\n"
"  let sz = sin(zenith);\n"
"  return vec3<f32>(sz * sin(azim), cos(zenith), sz * cos(azim));\n"
"}\n"
"fn dir_to_skyview_uv(dir : vec3<f32>, view_r : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let horizon_cos = -sqrt(max(0.0, view_r * view_r - Rb * Rb)) / max(view_r, 1e-4);\n"
"  let horizon_ang = acos(clamp(horizon_cos, -1.0, 1.0));\n"
"  let uu = atan2(dir.x, dir.z) / (2.0 * PI) + 0.5;\n"
"  let zenith = acos(clamp(dir.y, -1.0, 1.0));\n"
"  var vv = 0.0;\n"
"  if (zenith < horizon_ang) {\n"
"    let t = zenith / max(horizon_ang, 1e-6);\n"
"    vv = 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
"  } else {\n"
"    let t = (zenith - horizon_ang) / max(PI - horizon_ang, 1e-6);\n"
"    vv = 0.5 + 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
"  }\n"
"  return vec2<f32>(clamp(uu, 0.0, 1.0), clamp(vv, 0.0, 1.0));\n"
"}\n"
;

/* -------- Transmittance LUT shader ---------------------------------------- */

static const char *kTransShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "const TRANS_STEPS : i32 = 40;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let p = uv_to_trans_params(in.uv, u);\n"
    "  let r = p.x;\n"
    "  let mu = p.y;\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let rd = vec3<f32>(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  if (t_ground > 0.0 && t_ground < t_max) { t_max = t_ground; }\n"
    "  if (t_max <= 0.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }\n"
    "  let dt = t_max / f32(TRANS_STEPS);\n"
    "  var od = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < TRANS_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let alt = length(pos) - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    od = od + m.extinction * dt;\n"
    "  }\n"
    "  return vec4<f32>(exp(-od), 1.0);\n"
    "}\n";

/* -------- Multi-scattering LUT shader ------------------------------------- */
/* Hillaire 2020 section 5.2: 2nd-order scattering with analytic multi-scatter
 * series. SAMPLE_COUNT rays per texel (uniform spherical), SAMPLES_STEPS steps
 * each. f_ms is the multi-scatter transfer, L_2 is the second-order radiance. */

static const char *kMSShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var lut_sampler : sampler;\n"
    "const MS_DIR_SQRT : i32 = 8;\n"
    "const MS_STEP_COUNT : i32 = 20;\n"
    "fn integrate_scattered(ro : vec3<f32>, rd : vec3<f32>, sun : vec3<f32>, u : AtmosUniforms) -> vec4<f32> {\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground_raw = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  var hit_ground = false;\n"
    "  if (t_ground_raw > 0.0 && t_ground_raw < t_max) {\n"
    "    t_max = t_ground_raw;\n"
    "    hit_ground = true;\n"
    "  }\n"
    "  if (t_max <= 0.0) { return vec4<f32>(0.0); }\n"
    "  let dt = t_max / f32(MS_STEP_COUNT);\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  var f_ms = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < MS_STEP_COUNT; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    /* Isotropic phase (1/4π) for multi-scattering derivation. */
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u);\n"
    "    let s_int = (m.scattering - m.scattering * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    let phase_iso = 1.0 / (4.0 * PI);\n"
    "    L = L + throughput * (sun_trans * phase_iso * m.scattering) * dt;\n"
    "    f_ms = f_ms + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  if (hit_ground) {\n"
    "    let ground_point = ro + rd * t_max;\n"
    "    let up = ground_point / max(length(ground_point), 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    if (mu_sun > 0.0) {\n"
    "      let t_g = sample_transmittance_lut(trans_lut, lut_sampler,\n"
    "        bottomR(u), mu_sun, u);\n"
    "      L = L + throughput * t_g * u.ground_albedo.rgb * mu_sun / PI;\n"
    "    }\n"
    "  }\n"
    "  return vec4<f32>(L, (f_ms.x + f_ms.y + f_ms.z) / 3.0);\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let p = ms_uv_to_params(in.uv, u);\n"
    "  let cos_sun = p.x;\n"
    "  let alt = p.y;\n"
    "  let r = bottomR(u) + max(alt, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let sun = vec3<f32>(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);\n"
    "  var L_2 = vec3<f32>(0.0);\n"
    "  var F = 0.0;\n"
    "  let sqrt_count = MS_DIR_SQRT;\n"
    "  let inv_samples = 1.0 / (f32(sqrt_count) * f32(sqrt_count));\n"
    "  for (var i : i32 = 0; i < sqrt_count; i = i + 1) {\n"
    "    for (var j : i32 = 0; j < sqrt_count; j = j + 1) {\n"
    "      let u01 = (f32(i) + 0.5) / f32(sqrt_count);\n"
    "      let v01 = (f32(j) + 0.5) / f32(sqrt_count);\n"
    "      let phi = 2.0 * PI * u01;\n"
    "      let cos_theta = 1.0 - 2.0 * v01;\n"
    "      let sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));\n"
    "      let dir = vec3<f32>(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));\n"
    "      let s = integrate_scattered(ro, dir, sun, u);\n"
    "      L_2 = L_2 + s.rgb * inv_samples;\n"
    "      F = F + s.w * inv_samples;\n"
    "    }\n"
    "  }\n"
    "  let psi_ms = L_2 / max(1.0 - F, 1e-4);\n"
    "  return vec4<f32>(psi_ms, 1.0);\n"
    "}\n";

/* -------- Sky-view LUT shader --------------------------------------------- */

static const char *kSkyViewShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var ms_lut : texture_2d<f32>;\n"
    "@group(0) @binding(3) var lut_sampler : sampler;\n"
    "const SV_STEPS : i32 = 30;\n"
    "fn integrate_skyview(ro : vec3<f32>, rd : vec3<f32>, u : AtmosUniforms) -> vec3<f32> {\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground_raw = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  var hit_ground = false;\n"
    "  if (t_ground_raw > 0.0 && t_ground_raw < t_max) {\n"
    "    t_max = t_ground_raw;\n"
    "    hit_ground = true;\n"
    "  }\n"
    "  if (t_max <= 0.0) { return vec3<f32>(0.0); }\n"
    "  let dt = t_max / f32(SV_STEPS);\n"
    "  let sun = u.sun_direction.xyz;\n"
    "  let cos_sv = clamp(dot(rd, sun), -1.0, 1.0);\n"
    "  let mie_g = u.mie_params.x;\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < SV_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u);\n"
    /* Phase-weighted single scattering + isotropic multi-scattering. */
    "    let scat_r = m.scattering_rayleigh * rayleigh_phase(cos_sv);\n"
    "    let scat_m = m.scattering_mie * mie_phase(cos_sv, mie_g);\n"
    "    let single = (scat_r + scat_m) * sun_trans;\n"
    "    let ms_uv = ms_params_to_uv(mu_sun, r_p, u);\n"
    "    let psi_ms = textureSampleLevel(ms_lut, lut_sampler, ms_uv, 0.0).rgb;\n"
    "    let multi = psi_ms * m.scattering;\n"
    "    let in_scat = (single + multi) * u.sun_color.rgb;\n"
    "    let s_int = (in_scat - in_scat * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    L = L + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  return L;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let rd = skyview_uv_to_dir(in.uv, r, u);\n"
    "  let L = integrate_skyview(ro, rd, u);\n"
    "  return vec4<f32>(L, 1.0);\n"
    "}\n";

/* -------- Compute: sky-view -> cube face + cube mip downsample ----------- */

/* Single compute dispatch writes all 6 cube faces of mip 0, replacing the
 * old 6 fragment passes. */
static const char *kCubeFaceComputeShaderSource =
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var skyview_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var lut_sampler : sampler;\n"
    "@group(0) @binding(3) var cube_out : texture_storage_2d_array<rgba16float, write>;\n"
    "fn face_direction(idx : u32, uv : vec2<f32>) -> vec3<f32> {\n"
    "  let x = uv.x * 2.0 - 1.0;\n"
    "  let y = 1.0 - uv.y * 2.0;\n"
    "  var d : vec3<f32>;\n"
    "  switch (idx) {\n"
    "    case 0u: { d = vec3<f32>( 1.0,  y, -x); }\n"
    "    case 1u: { d = vec3<f32>(-1.0,  y,  x); }\n"
    "    case 2u: { d = vec3<f32>( x,  1.0, -y); }\n"
    "    case 3u: { d = vec3<f32>( x, -1.0,  y); }\n"
    "    case 4u: { d = vec3<f32>( x,  y,  1.0); }\n"
    "    default: { d = vec3<f32>(-x,  y, -1.0); }\n"
    "  }\n"
    "  return normalize(d);\n"
    "}\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(cube_out);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= 6u) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let dir = face_direction(gid.z, uv);\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let view_r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let sv_uv = dir_to_skyview_uv(dir, view_r, u);\n"
    "  let sky = textureSampleLevel(skyview_lut, lut_sampler, sv_uv, 0.0).rgb;\n"
    "  textureStore(cube_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), vec4<f32>(sky, 1.0));\n"
    "}\n";

/* 2x box mip downsample — one dispatch per target mip, replacing 42
 * fragment passes with 7 compute dispatches. */
static const char *kCubeDownsampleComputeShaderSource =
    "@group(0) @binding(0) var input_cube : texture_2d_array<f32>;\n"
    "@group(0) @binding(1) var lin_samp : sampler;\n"
    "@group(0) @binding(2) var output_cube : texture_storage_2d_array<rgba16float, write>;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(output_cube);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= 6u) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let c = textureSampleLevel(input_cube, lin_samp, uv, i32(gid.z), 0.0);\n"
    "  textureStore(output_cube, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), c);\n"
    "}\n";

/* Aerial perspective compute shader — writes the full 32x32x32 LUT in a
 * single dispatch, replacing 32 fragment passes. */
static const char *kAerialComputeShaderSource =
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var ms_lut : texture_2d<f32>;\n"
    "@group(0) @binding(3) var lut_sampler : sampler;\n"
    "@group(0) @binding(4) var aerial_out : texture_storage_2d_array<rgba16float, write>;\n"
    "const AP_STEPS : i32 = 6;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(aerial_out);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= u32(u.misc.x)) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    /* NDC -> world-space view ray (direction only). */
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 1.0, 1.0);\n"
    "  let world_h = u.inv_vp * ndc;\n"
    "  var world_pos = world_h.xyz;\n"
    "  if (abs(world_h.w) > 1e-6) { world_pos = world_h.xyz / world_h.w; }\n"
    "  let view_dir_world = normalize(world_pos - u.camera_pos_world.xyz);\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let r0 = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r0, 0.0);\n"
    "  let rd = view_dir_world;\n"
    "  let max_dist_km = u.mie_params.y;\n"
    "  let slice_count = u.misc.x;\n"
    "  let slice_t = (f32(gid.z) + 1.0) / slice_count;\n"
    "  let t_end_km = slice_t * max_dist_km;\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  var t_max = t_end_km;\n"
    "  if (t_top > 0.0 && t_top < t_max) { t_max = t_top; }\n"
    "  if (t_max <= 0.0) {\n"
    "    textureStore(aerial_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "      i32(gid.z), vec4<f32>(0.0, 0.0, 0.0, 1.0));\n"
    "    return;\n"
    "  }\n"
    "  let dt = t_max / f32(AP_STEPS);\n"
    "  let sun = u.sun_direction.xyz;\n"
    "  let cos_sv = clamp(dot(rd, sun), -1.0, 1.0);\n"
    "  let mie_g = u.mie_params.x;\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < AP_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u);\n"
    "    let scat_r = m.scattering_rayleigh * rayleigh_phase(cos_sv);\n"
    "    let scat_m = m.scattering_mie * mie_phase(cos_sv, mie_g);\n"
    "    let single = (scat_r + scat_m) * sun_trans;\n"
    "    let ms_uv = ms_params_to_uv(mu_sun, r_p, u);\n"
    "    let psi_ms = textureSampleLevel(ms_lut, lut_sampler, ms_uv, 0.0).rgb;\n"
    "    let multi = psi_ms * m.scattering;\n"
    "    let in_scat = (single + multi) * u.sun_color.rgb;\n"
    "    let s_int = (in_scat - in_scat * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    L = L + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  let t_avg = (throughput.x + throughput.y + throughput.z) / 3.0;\n"
    "  textureStore(aerial_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), vec4<f32>(L, t_avg));\n"
    "}\n";

/* -------- Compose shader -------------------------------------------------- */

static const char *kComposeShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(4) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(5) var skyview_lut : texture_2d<f32>;\n"
    "@group(0) @binding(6) var aerial_lut : texture_2d_array<f32>;\n"
    "@group(0) @binding(7) var lut_sampler : sampler;\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);\n"
    "  let h = u.inv_vp * ndc;\n"
    "  if (abs(h.w) > 1e-6) { return h.xyz / h.w; }\n"
    "  return h.xyz;\n"
    "}\n"
    /* Aerial LUT uses endpoint sampling: slice k stores the in-scatter and
     * transmittance integrated from the camera to t = ((k+1)/N) * max_dist.
     * So for a pixel at distance d, the matching fractional slice is
     *   slice_f = (d / max_dist) * N - 1
     * and we lerp with a virtual "slice -1" = identity (0, 0, 0, 1) to get a
     * smooth fadeout for d < max_dist / N (close-up scene pixels). */
    "fn sample_aerial(uv : vec2<f32>, d_km : f32, max_km : f32, slice_count : f32) -> vec4<f32> {\n"
    "  let sf = clamp(d_km / max_km, 0.0, 1.0) * slice_count - 1.0;\n"
    "  let lo = i32(floor(sf));\n"
    "  let hi = lo + 1;\n"
    "  let f = sf - f32(lo);\n"
    "  let identity = vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "  var a = identity;\n"
    "  var b = identity;\n"
    "  if (lo >= 0) {\n"
    "    a = textureSampleLevel(aerial_lut, lut_sampler, uv, lo, 0.0);\n"
    "  }\n"
    "  if (hi >= 0) {\n"
    "    b = textureSampleLevel(aerial_lut, lut_sampler, uv,\n"
    "      min(hi, i32(slice_count) - 1), 0.0);\n"
    "  }\n"
    "  return mix(a, b, f);\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  let is_sky = depth >= 0.9999;\n"
    "  if (is_sky) {\n"
    "    let world_pos = reconstruct_world_pos(in.uv, 1.0);\n"
    "    let rd = normalize(world_pos - u.camera_pos_world.xyz);\n"
    "    let alt_km = u.camera_pos_world.w;\n"
    "    let view_r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "    let sv_uv = dir_to_skyview_uv(rd, view_r, u);\n"
    "    var sky = textureSampleLevel(skyview_lut, lut_sampler, sv_uv, 0.0).rgb;\n"
    /* Sun disk: only visible when looking above horizon. */
    "    let sun_cos = dot(rd, u.sun_direction.xyz);\n"
    "    let disk_cos = u.mie_params.z;\n"
    "    let disk_int = u.mie_params.w;\n"
    "    if (disk_int > 0.0 && sun_cos > disk_cos) {\n"
    "      let ro = vec3<f32>(0.0, view_r, 0.0);\n"
    "      let t_ground = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "      if (t_ground < 0.0) {\n"
    "        let mu = clamp(u.sun_direction.y, -1.0, 1.0);\n"
    "        let t_sun = sample_transmittance_lut(trans_lut, lut_sampler, view_r, mu, u);\n"
    /* Hard centre, soft outer edge: aa = 1 at sun_cos = 1, fades to 0 at\n"
     * disk_cos. The fade zone is the outer 25% of the disk angular extent. */
    "        let fade_start = mix(1.0, disk_cos, 0.25);\n"
    "        let aa = smoothstep(disk_cos, fade_start, sun_cos);\n"
    "        sky = sky + t_sun * u.sun_color.rgb * disk_int * aa;\n"
    "      }\n"
    "    }\n"
    "    return vec4<f32>(sky, 1.0);\n"
    "  }\n"
    "  let world_pos = reconstruct_world_pos(in.uv, depth);\n"
    "  let ray = world_pos - u.camera_pos_world.xyz;\n"
    "  let dist_world = length(ray);\n"
    "  let dist_km = dist_world * u.world_scale.y;\n"
    "  let max_km = u.mie_params.y;\n"
    "  let slice_count = u.misc.x;\n"
    "  let ap = sample_aerial(in.uv, dist_km, max_km, slice_count);\n"
    /* Artistic push: scale haze and the corresponding transmittance loss
     * by `aerial_params.x`. At 1.0 this reduces to the physical blend
     *   composed = scene * T + L
     * At k > 1, transmittance is pushed towards `1 - k*(1-T)` (more loss)
     * and in-scatter is boosted by k, giving a stronger "atmospheric" look. */
    "  let k = u.aerial_params.x;\n"
    "  let t_scaled = max(0.0, 1.0 - k * (1.0 - ap.a));\n"
    "  var composed = src.rgb * t_scaled + ap.rgb * k;\n"
    "  return vec4<f32>(composed, src.a);\n"
    "}\n";

/* -------------------------------------------------------------------------- */
/* Resource lifecycle                                                         */
/* -------------------------------------------------------------------------- */

static void flecsEngine_atmos_releaseTextures(FlecsAtmosphereImpl *a)
{
    if (a->aerial_storage_view) {
        wgpuTextureViewRelease(a->aerial_storage_view);
        a->aerial_storage_view = NULL;
    }
    if (a->aerial_view) { wgpuTextureViewRelease(a->aerial_view); a->aerial_view = NULL; }
    if (a->aerial_texture) { wgpuTextureRelease(a->aerial_texture); a->aerial_texture = NULL; }
    if (a->skyview_view) { wgpuTextureViewRelease(a->skyview_view); a->skyview_view = NULL; }
    if (a->skyview_texture) { wgpuTextureRelease(a->skyview_texture); a->skyview_texture = NULL; }
    if (a->ms_view) { wgpuTextureViewRelease(a->ms_view); a->ms_view = NULL; }
    if (a->ms_texture) { wgpuTextureRelease(a->ms_texture); a->ms_texture = NULL; }
    if (a->trans_view) { wgpuTextureViewRelease(a->trans_view); a->trans_view = NULL; }
    if (a->trans_texture) { wgpuTextureRelease(a->trans_texture); a->trans_texture = NULL; }
}

static void flecsEngine_atmos_releaseResources(FlecsAtmosphereImpl *a)
{
    flecsEngine_atmos_releaseTextures(a);
    /* Cached LUT / aerial / cube bind groups. */
    if (a->trans_bind_group) {
        wgpuBindGroupRelease(a->trans_bind_group);
        a->trans_bind_group = NULL;
    }
    if (a->ms_bind_group) {
        wgpuBindGroupRelease(a->ms_bind_group);
        a->ms_bind_group = NULL;
    }
    if (a->skyview_bind_group) {
        wgpuBindGroupRelease(a->skyview_bind_group);
        a->skyview_bind_group = NULL;
    }
    if (a->aerial_compute_bind_group) {
        wgpuBindGroupRelease(a->aerial_compute_bind_group);
        a->aerial_compute_bind_group = NULL;
    }
    if (a->cube_face_bind_group) {
        wgpuBindGroupRelease(a->cube_face_bind_group);
        a->cube_face_bind_group = NULL;
    }
    for (uint32_t m = 0; m < 8; m++) {
        if (a->cube_ds_bind_groups[m]) {
            wgpuBindGroupRelease(a->cube_ds_bind_groups[m]);
            a->cube_ds_bind_groups[m] = NULL;
        }
        if (a->cube_mip_storage_views[m]) {
            wgpuTextureViewRelease(a->cube_mip_storage_views[m]);
            a->cube_mip_storage_views[m] = NULL;
        }
        if (a->cube_mip_read_views[m]) {
            wgpuTextureViewRelease(a->cube_mip_read_views[m]);
            a->cube_mip_read_views[m] = NULL;
        }
    }
    a->cube_mip_count = 0;
    if (a->cube_ds_pipeline) {
        wgpuComputePipelineRelease(a->cube_ds_pipeline);
        a->cube_ds_pipeline = NULL;
    }
    if (a->cube_ds_layout) {
        wgpuBindGroupLayoutRelease(a->cube_ds_layout);
        a->cube_ds_layout = NULL;
    }
    if (a->cube_face_pipeline) {
        wgpuComputePipelineRelease(a->cube_face_pipeline);
        a->cube_face_pipeline = NULL;
    }
    if (a->cube_face_layout) {
        wgpuBindGroupLayoutRelease(a->cube_face_layout);
        a->cube_face_layout = NULL;
    }
    if (a->aerial_compute_pipeline) {
        wgpuComputePipelineRelease(a->aerial_compute_pipeline);
        a->aerial_compute_pipeline = NULL;
    }
    if (a->aerial_compute_layout) {
        wgpuBindGroupLayoutRelease(a->aerial_compute_layout);
        a->aerial_compute_layout = NULL;
    }
    if (a->compose_pipeline_surface) {
        wgpuRenderPipelineRelease(a->compose_pipeline_surface);
        a->compose_pipeline_surface = NULL;
    }
    if (a->compose_pipeline_hdr) {
        wgpuRenderPipelineRelease(a->compose_pipeline_hdr);
        a->compose_pipeline_hdr = NULL;
    }
    if (a->skyview_pipeline) {
        wgpuRenderPipelineRelease(a->skyview_pipeline);
        a->skyview_pipeline = NULL;
    }
    if (a->ms_pipeline) {
        wgpuRenderPipelineRelease(a->ms_pipeline);
        a->ms_pipeline = NULL;
    }
    if (a->trans_pipeline) {
        wgpuRenderPipelineRelease(a->trans_pipeline);
        a->trans_pipeline = NULL;
    }
    if (a->compose_layout) {
        wgpuBindGroupLayoutRelease(a->compose_layout);
        a->compose_layout = NULL;
    }
    if (a->skyview_layout) {
        wgpuBindGroupLayoutRelease(a->skyview_layout);
        a->skyview_layout = NULL;
    }
    if (a->ms_layout) {
        wgpuBindGroupLayoutRelease(a->ms_layout);
        a->ms_layout = NULL;
    }
    if (a->trans_layout) {
        wgpuBindGroupLayoutRelease(a->trans_layout);
        a->trans_layout = NULL;
    }
    if (a->clamp_sampler) {
        wgpuSamplerRelease(a->clamp_sampler);
        a->clamp_sampler = NULL;
    }
    if (a->uniform_buffer) {
        wgpuBufferRelease(a->uniform_buffer);
        a->uniform_buffer = NULL;
    }
}

ECS_DTOR(FlecsAtmosphereImpl, ptr, {
    flecsEngine_atmos_releaseResources(ptr);
})

ECS_MOVE(FlecsAtmosphereImpl, dst, src, {
    flecsEngine_atmos_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

/* -------------------------------------------------------------------------- */
/* Settings & uniform fill                                                    */
/* -------------------------------------------------------------------------- */

FlecsAtmosphere flecsEngine_atmosphereSettingsDefault(void)
{
    return (FlecsAtmosphere){
        .sun_intensity = 20.0f,
        .sun_disk_intensity = 1.0f,
        .sun_disk_angular_radius = 0.00465f,
        .aerial_perspective_distance_km = 32.0f,
        .aerial_perspective_intensity = 1.0f,
        .sea_level_y = 0.0f,
        .world_units_per_km = 1000.0f,
        .ground_altitude_km = 0.5f,
        .planet_radius_km = 6360.0f,
        .atmosphere_thickness_km = 100.0f,
        .rayleigh_scale_height_km = 8.0f,
        .mie_scale_height_km = 1.2f,
        .mie_anisotropy = 0.8f,
        .ground_albedo = { 77, 77, 77, 255 }
    };
}

static void flecsEngine_atmos_fillUniform(
    const ecs_world_t *world,
    ecs_entity_t view_entity,
    const FlecsAtmosphere *s,
    FlecsAtmosphereUniform *out)
{
    memset(out, 0, sizeof(*out));
    glm_mat4_identity(out->inv_vp);

    float planet_r = s->planet_radius_km;
    float top_r = planet_r + s->atmosphere_thickness_km;

    out->atmosphere[0] = planet_r;
    out->atmosphere[1] = top_r;
    out->atmosphere[2] = s->rayleigh_scale_height_km;
    out->atmosphere[3] = s->mie_scale_height_km;

    /* Earth-like scattering coefficients (per km). From the Unreal
     * SkyAtmosphere reference implementation. */
    out->scat_rayleigh[0] = 0.005802f;
    out->scat_rayleigh[1] = 0.013558f;
    out->scat_rayleigh[2] = 0.033100f;
    out->scat_rayleigh[3] = 0.0f;

    out->ext_mie[0] = 0.003996f; /* mie scattering */
    out->ext_mie[1] = 0.004400f; /* mie extinction (scattering + absorption) */
    out->ext_mie[2] = 0.0f;
    out->ext_mie[3] = 0.0f;

    out->abs_ozone[0] = 0.000650f;
    out->abs_ozone[1] = 0.001881f;
    out->abs_ozone[2] = 0.000085f;
    out->abs_ozone[3] = 0.0f;

    out->ground_albedo[0] = flecsEngine_colorChannelToFloat(s->ground_albedo.r);
    out->ground_albedo[1] = flecsEngine_colorChannelToFloat(s->ground_albedo.g);
    out->ground_albedo[2] = flecsEngine_colorChannelToFloat(s->ground_albedo.b);
    out->ground_albedo[3] = 1.0f;

    float disk_half_angle = s->sun_disk_angular_radius > 1e-6f
        ? s->sun_disk_angular_radius : 0.00465f;
    out->mie_params[0] = s->mie_anisotropy;
    out->mie_params[1] = s->aerial_perspective_distance_km;
    out->mie_params[2] = cosf(disk_half_angle);
    out->mie_params[3] = s->sun_disk_intensity;

    float units_per_km = s->world_units_per_km > 1e-6f ? s->world_units_per_km : 1000.0f;
    out->world_scale[0] = units_per_km;
    out->world_scale[1] = 1.0f / units_per_km;
    out->world_scale[2] = s->sea_level_y;
    out->world_scale[3] = s->ground_altitude_km;

    out->misc[0] = (float)FLECS_ATMOS_AERIAL_SLICES;

    out->aerial_params[0] = s->aerial_perspective_intensity > 0.0f
        ? s->aerial_perspective_intensity : 1.0f;

    out->camera_pos_world[3] = s->ground_altitude_km;

    /* Default sun straight up with modest illuminance (in case no light entity). */
    out->sun_direction[0] = 0.0f;
    out->sun_direction[1] = 1.0f;
    out->sun_direction[2] = 0.0f;
    out->sun_direction[3] = s->sun_intensity;

    out->sun_color[0] = s->sun_intensity;
    out->sun_color[1] = s->sun_intensity;
    out->sun_color[2] = s->sun_intensity;
    out->sun_color[3] = 1.0f;

    if (!view_entity) return;
    const FlecsRenderView *view = ecs_get(world, view_entity, FlecsRenderView);
    if (!view) return;

    if (view->camera) {
        const FlecsCameraImpl *cam = ecs_get(world, view->camera, FlecsCameraImpl);
        if (cam) {
            mat4 mvp;
            glm_mat4_copy((vec4*)cam->mvp, mvp);
            glm_mat4_inv(mvp, out->inv_vp);
        }
        const FlecsWorldTransform3 *cam_tf = ecs_get(
            world, view->camera, FlecsWorldTransform3);
        if (cam_tf) {
            out->camera_pos_world[0] = cam_tf->m[3][0];
            out->camera_pos_world[1] = cam_tf->m[3][1];
            out->camera_pos_world[2] = cam_tf->m[3][2];
            float alt_km = s->ground_altitude_km +
                (cam_tf->m[3][1] - s->sea_level_y) / units_per_km;
            if (alt_km < 0.001f) alt_km = 0.001f;
            out->camera_pos_world[3] = alt_km;
        }
    }

    if (view->light) {
        const FlecsRotation3 *rot = ecs_get(world, view->light, FlecsRotation3);
        if (rot) {
            float ray_dir[3];
            if (flecsEngine_lightDirFromRotation(rot, ray_dir)) {
                out->sun_direction[0] = -ray_dir[0];
                out->sun_direction[1] = -ray_dir[1];
                out->sun_direction[2] = -ray_dir[2];
            }
        }
        /* Atmosphere uses its own absolute sun intensity; the directional
         * light's intensity only controls scene shading, not the sky. The
         * RGBA color is reused so e.g. a warm sun tints both the scene and
         * the scattering consistently. */
        const FlecsRgba *rgb = ecs_get(world, view->light, FlecsRgba);
        float r = rgb ? flecsEngine_colorChannelToFloat(rgb->r) : 1.0f;
        float g = rgb ? flecsEngine_colorChannelToFloat(rgb->g) : 1.0f;
        float b = rgb ? flecsEngine_colorChannelToFloat(rgb->b) : 1.0f;
        out->sun_color[0] = r * s->sun_intensity;
        out->sun_color[1] = g * s->sun_intensity;
        out->sun_color[2] = b * s->sun_intensity;
        out->sun_direction[3] = s->sun_intensity;
    }
}

/* -------------------------------------------------------------------------- */
/* Texture creation                                                           */
/* -------------------------------------------------------------------------- */

static bool flecsEngine_atmos_createSimpleTexture(
    const FlecsEngineImpl *engine,
    uint32_t w, uint32_t h,
    WGPUTextureFormat format,
    WGPUTexture *out_tex,
    WGPUTextureView *out_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { w, h, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture tex = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!tex) return false;

    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    WGPUTextureView view = wgpuTextureCreateView(tex, &view_desc);
    if (!view) {
        wgpuTextureRelease(tex);
        return false;
    }

    *out_tex = tex;
    *out_view = view;
    return true;
}

static bool flecsEngine_atmos_createAerialTexture(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a)
{
    WGPUTextureDescriptor desc = {
        /* TextureBinding: read by the compose shader as texture_2d_array.
         * StorageBinding: written by the aerial compute shader. */
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { FLECS_ATMOS_AERIAL_W, FLECS_ATMOS_AERIAL_H, FLECS_ATMOS_AERIAL_SLICES },
        .format = FLECS_ATMOS_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    a->aerial_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!a->aerial_texture) return false;

    WGPUTextureViewDescriptor array_desc = {
        .format = FLECS_ATMOS_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = FLECS_ATMOS_AERIAL_SLICES,
        .aspect = WGPUTextureAspect_All
    };
    a->aerial_view = wgpuTextureCreateView(a->aerial_texture, &array_desc);
    if (!a->aerial_view) return false;

    /* Storage-write view for the compute shader. Same dimension/layers as
     * the sample view above; WebGPU just needs a separate view handle. */
    a->aerial_storage_view = wgpuTextureCreateView(a->aerial_texture, &array_desc);
    return a->aerial_storage_view != NULL;
}

/* -------------------------------------------------------------------------- */
/* Pipeline creation                                                          */
/* -------------------------------------------------------------------------- */

static WGPUShaderModule flecsEngine_atmos_createModule(
    const FlecsEngineImpl *engine,
    const char *body)
{
    size_t common_len = strlen(kAtmosphereCommonWgsl);
    size_t body_len = strlen(body);
    char *buf = (char*)ecs_os_malloc(common_len + body_len + 1);
    memcpy(buf, kAtmosphereCommonWgsl, common_len);
    memcpy(buf + common_len, body, body_len);
    buf[common_len + body_len] = '\0';
    WGPUShaderModule module = flecsEngine_createShaderModule(engine->device, buf);
    ecs_os_free(buf);
    return module;
}

static WGPURenderPipeline flecsEngine_atmos_createPipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *vertex_entry,
    const char *fragment_entry,
    WGPUTextureFormat color_format)
{
    WGPUPipelineLayoutDescriptor pl_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bind_layout
    };
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(engine->device, &pl_desc);
    if (!pl) return NULL;

    WGPUColorTargetState color_target = {
        .format = color_format,
        .blend = NULL,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUVertexState vs = {
        .module = module,
        .entryPoint = WGPU_STR(vertex_entry)
    };
    WGPUFragmentState fs = {
        .module = module,
        .entryPoint = WGPU_STR(fragment_entry),
        .targetCount = 1,
        .targets = &color_target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pl,
        .vertex = vs,
        .fragment = &fs,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(engine->device, &desc);
    wgpuPipelineLayoutRelease(pl);
    return p;
}

static WGPUComputePipeline flecsEngine_atmos_createComputePipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *entry)
{
    WGPUPipelineLayoutDescriptor pl_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bind_layout
    };
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(engine->device, &pl_desc);
    if (!pl) return NULL;

    WGPUComputePipelineDescriptor desc = {
        .layout = pl,
        .compute = {
            .module = module,
            .entryPoint = WGPU_STR(entry)
        }
    };
    WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(engine->device, &desc);
    wgpuPipelineLayoutRelease(pl);
    return p;
}

/* -------------------------------------------------------------------------- */
/* Bind group layout helpers                                                  */
/* -------------------------------------------------------------------------- */

static WGPUBindGroupLayout flecsEngine_atmos_layoutUniformOnly(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[1] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 1, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutMS(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 3, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutSkyView(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 4, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

/* Aerial compute pipeline layout: uniform + trans LUT + MS LUT + sampler
 * + aerial storage output. */
static WGPUBindGroupLayout flecsEngine_atmos_layoutAerialCompute(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[5] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = FLECS_ATMOS_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 5, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

/* Cube face compute layout: uniform + skyview LUT + sampler + cube storage. */
static WGPUBindGroupLayout flecsEngine_atmos_layoutCubeFaceCompute(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = FLECS_ATMOS_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 4, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

/* Cube mip downsample compute layout: input array + sampler + output array. */
static WGPUBindGroupLayout flecsEngine_atmos_layoutCubeDsCompute(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = FLECS_ATMOS_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 3, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutCompose(
    const FlecsEngineImpl *engine)
{
    WGPUBindGroupLayoutEntry entries[8] = {
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
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsAtmosphereUniform)
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor d = { .entryCount = 8, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

/* -------------------------------------------------------------------------- */
/* Setup callback                                                             */
/* -------------------------------------------------------------------------- */

bool flecsEngine_atmosphere_ensureImpl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity)
{
    /* Component writes from inside a running system are deferred by flecs,
     * which means our populated struct would land in a staging buffer and
     * get zero-filled on the next frame's ensure. Suspend defer so the
     * component is materialized immediately. */
    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    FlecsAtmosphereImpl *existing = ecs_ensure(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (existing->uniform_buffer) {
        if (was_deferred) ecs_defer_resume(world);
        return true; /* already initialized */
    }

    FlecsAtmosphereImpl a = {0};

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsAtmosphereUniform)
    };
    a.uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!a.uniform_buffer) { ecs_err("atmos: uniform_buffer create failed"); flecsEngine_atmos_releaseResources(&a); return false; }

    WGPUSamplerDescriptor samp_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    };
    a.clamp_sampler = wgpuDeviceCreateSampler(engine->device, &samp_desc);
    if (!a.clamp_sampler) { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_TRANS_W, FLECS_ATMOS_TRANS_H, FLECS_ATMOS_FORMAT,
        &a.trans_texture, &a.trans_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_MS_W, FLECS_ATMOS_MS_H, FLECS_ATMOS_FORMAT,
        &a.ms_texture, &a.ms_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_SKYVIEW_W, FLECS_ATMOS_SKYVIEW_H, FLECS_ATMOS_FORMAT,
        &a.skyview_texture, &a.skyview_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createAerialTexture(engine, &a))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    a.trans_layout = flecsEngine_atmos_layoutUniformOnly(engine);
    a.ms_layout = flecsEngine_atmos_layoutMS(engine);
    a.skyview_layout = flecsEngine_atmos_layoutSkyView(engine);
    a.aerial_compute_layout = flecsEngine_atmos_layoutAerialCompute(engine);
    a.cube_face_layout = flecsEngine_atmos_layoutCubeFaceCompute(engine);
    a.cube_ds_layout = flecsEngine_atmos_layoutCubeDsCompute(engine);
    a.compose_layout = flecsEngine_atmos_layoutCompose(engine);
    if (!a.trans_layout || !a.ms_layout || !a.skyview_layout ||
        !a.aerial_compute_layout || !a.cube_face_layout ||
        !a.cube_ds_layout || !a.compose_layout)
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    WGPUShaderModule trans_mod = flecsEngine_atmos_createModule(engine, kTransShaderSource);
    WGPUShaderModule ms_mod = flecsEngine_atmos_createModule(engine, kMSShaderSource);
    WGPUShaderModule sv_mod = flecsEngine_atmos_createModule(engine, kSkyViewShaderSource);
    WGPUShaderModule ap_mod = flecsEngine_atmos_createModule(engine, kAerialComputeShaderSource);
    WGPUShaderModule cs_mod = flecsEngine_atmos_createModule(engine, kComposeShaderSource);
    WGPUShaderModule cf_mod = flecsEngine_atmos_createModule(engine, kCubeFaceComputeShaderSource);
    WGPUShaderModule ds_mod = flecsEngine_atmos_createModule(engine, kCubeDownsampleComputeShaderSource);
    if (!trans_mod || !ms_mod || !sv_mod || !ap_mod || !cs_mod || !cf_mod || !ds_mod) {
        if (trans_mod) wgpuShaderModuleRelease(trans_mod);
        if (ms_mod) wgpuShaderModuleRelease(ms_mod);
        if (sv_mod) wgpuShaderModuleRelease(sv_mod);
        if (ap_mod) wgpuShaderModuleRelease(ap_mod);
        if (cs_mod) wgpuShaderModuleRelease(cs_mod);
        if (cf_mod) wgpuShaderModuleRelease(cf_mod);
        if (ds_mod) wgpuShaderModuleRelease(ds_mod);
        flecsEngine_atmos_releaseResources(&a);
        return false;
    }

    a.trans_pipeline = flecsEngine_atmos_createPipeline(
        engine, trans_mod, a.trans_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);
    a.ms_pipeline = flecsEngine_atmos_createPipeline(
        engine, ms_mod, a.ms_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);
    a.skyview_pipeline = flecsEngine_atmos_createPipeline(
        engine, sv_mod, a.skyview_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);

    a.aerial_compute_pipeline = flecsEngine_atmos_createComputePipeline(
        engine, ap_mod, a.aerial_compute_layout, "cs_main");
    a.cube_face_pipeline = flecsEngine_atmos_createComputePipeline(
        engine, cf_mod, a.cube_face_layout, "cs_main");
    a.cube_ds_pipeline = flecsEngine_atmos_createComputePipeline(
        engine, ds_mod, a.cube_ds_layout, "cs_main");

    WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);
    WGPUTextureFormat surface_format = flecsEngine_getViewTargetFormat(engine);
    a.compose_pipeline_hdr = flecsEngine_atmos_createPipeline(
        engine, cs_mod, a.compose_layout, "vs_main", "fs_main", hdr_format);
    a.compose_pipeline_surface = flecsEngine_atmos_createPipeline(
        engine, cs_mod, a.compose_layout, "vs_main", "fs_main", surface_format);

    wgpuShaderModuleRelease(trans_mod);
    wgpuShaderModuleRelease(ms_mod);
    wgpuShaderModuleRelease(sv_mod);
    wgpuShaderModuleRelease(ap_mod);
    wgpuShaderModuleRelease(cs_mod);
    wgpuShaderModuleRelease(cf_mod);
    wgpuShaderModuleRelease(ds_mod);

    if (!a.trans_pipeline || !a.ms_pipeline || !a.skyview_pipeline ||
        !a.compose_pipeline_hdr || !a.compose_pipeline_surface ||
        !a.aerial_compute_pipeline || !a.cube_face_pipeline || !a.cube_ds_pipeline)
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    *existing = a;
    ecs_modified(world, atmosphere_entity, FlecsAtmosphereImpl);

    /* Attach an IBL (FlecsHdriImpl) to the same entity. A single cubemap
     * with mips serves as both the prefilter source (full chain) and the
     * irradiance source (just the top/smallest mip exposed as a 1-mip
     * view). Populated each frame by sky-view-to-face rendering + 2x mip
     * downsampling; no GGX prefiltering since atmosphere has no
     * high-frequency content. */
    FlecsHdriImpl *hdri = ecs_ensure(world, atmosphere_entity, FlecsHdriImpl);
    /* Adding the HdriImpl may have changed the entity's archetype, so the
     * previous `existing` pointer is now stale — re-fetch before writing. */
    existing = ecs_ensure(world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!hdri->ibl_prefiltered_cubemap) {
        const uint32_t cube_size = 128u;
        uint32_t mips = flecsIblComputeMipCount(cube_size);
        if (mips > 8u) mips = 8u;
        existing->cube_mip_count = mips;
        hdri->ibl_prefilter_mip_count = mips;

        /* Cubemap — populated by compute, so usage is StorageBinding +
         * TextureBinding (no RenderAttachment needed). */
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_StorageBinding |
                     WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { cube_size, cube_size, 6 },
            .format = WGPUTextureFormat_RGBA16Float,
            .mipLevelCount = mips,
            .sampleCount = 1
        };
        hdri->ibl_prefiltered_cubemap = wgpuDeviceCreateTexture(
            engine->device, &desc);
        hdri->ibl_prefiltered_cubemap_view = wgpuTextureCreateView(
            hdri->ibl_prefiltered_cubemap, &(WGPUTextureViewDescriptor){
                .format = WGPUTextureFormat_RGBA16Float,
                .dimension = WGPUTextureViewDimension_Cube,
                .baseMipLevel = 0, .mipLevelCount = mips,
                .baseArrayLayer = 0, .arrayLayerCount = 6
            });
        /* Top-mip cube view (irradiance binding). */
        hdri->ibl_irradiance_cubemap = NULL;
        hdri->ibl_irradiance_cubemap_view = wgpuTextureCreateView(
            hdri->ibl_prefiltered_cubemap, &(WGPUTextureViewDescriptor){
                .format = WGPUTextureFormat_RGBA16Float,
                .dimension = WGPUTextureViewDimension_Cube,
                .baseMipLevel = mips - 1u, .mipLevelCount = 1u,
                .baseArrayLayer = 0, .arrayLayerCount = 6
            });
        flecsIblCreateSampler(engine, hdri);

        /* Per-mip storage-write 2D-array views (used as compute outputs)
         * and per-mip read views (used when sampling the previous mip). */
        for (uint32_t m = 0; m < mips; m++) {
            existing->cube_mip_storage_views[m] = wgpuTextureCreateView(
                hdri->ibl_prefiltered_cubemap,
                &(WGPUTextureViewDescriptor){
                    .format = WGPUTextureFormat_RGBA16Float,
                    .dimension = WGPUTextureViewDimension_2DArray,
                    .baseMipLevel = m, .mipLevelCount = 1u,
                    .baseArrayLayer = 0, .arrayLayerCount = 6u
                });
            existing->cube_mip_read_views[m] = wgpuTextureCreateView(
                hdri->ibl_prefiltered_cubemap,
                &(WGPUTextureViewDescriptor){
                    .format = WGPUTextureFormat_RGBA16Float,
                    .dimension = WGPUTextureViewDimension_2DArray,
                    .baseMipLevel = m, .mipLevelCount = 1u,
                    .baseArrayLayer = 0, .arrayLayerCount = 6u
                });
        }

        flecsEngine_globals_createBindGroup(engine, hdri);
        ecs_modified(world, atmosphere_entity, FlecsHdriImpl);
    }

    /* Pre-build cached bind groups for the LUT + downsample passes. All
     * referenced resources (uniform buffer, LUT views, sampler, cubemap face
     * views, per-slice buffers) are now stable. This eliminates ~80 bind
     * group creations per frame. */
    {
        /* Transmittance LUT bind group — uniform only. */
        WGPUBindGroupEntry t_entries[1] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) }
        };
        existing->trans_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->trans_layout,
                .entryCount = 1, .entries = t_entries });

        /* Multi-scattering bind group — uniform + trans LUT + sampler. */
        WGPUBindGroupEntry ms_entries[3] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .sampler = existing->clamp_sampler }
        };
        existing->ms_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->ms_layout,
                .entryCount = 3, .entries = ms_entries });

        /* Sky-view bind group — uniform + trans + MS + sampler. */
        WGPUBindGroupEntry sv_entries[4] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .textureView = existing->ms_view },
            { .binding = 3, .sampler = existing->clamp_sampler }
        };
        existing->skyview_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->skyview_layout,
                .entryCount = 4, .entries = sv_entries });

        /* Aerial LUT compute bind group — a single group dispatched once
         * per frame to populate all 32^3 voxels. */
        WGPUBindGroupEntry a_entries[5] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .textureView = existing->ms_view },
            { .binding = 3, .sampler = existing->clamp_sampler },
            { .binding = 4, .textureView = existing->aerial_storage_view }
        };
        existing->aerial_compute_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = existing->aerial_compute_layout,
                .entryCount = 5, .entries = a_entries });

        /* Cube face compute bind group — writes mip 0 (all 6 faces) in one
         * dispatch. */
        WGPUBindGroupEntry cf_entries[4] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->skyview_view },
            { .binding = 2, .sampler = existing->clamp_sampler },
            { .binding = 3, .textureView = existing->cube_mip_storage_views[0] }
        };
        existing->cube_face_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = existing->cube_face_layout,
                .entryCount = 4, .entries = cf_entries });

        /* Cube-mip downsample bind groups — one per mip (m >= 1). Reads the
         * previous mip's 2D-array view. */
        for (uint32_t m = 1; m < existing->cube_mip_count; m++) {
            WGPUBindGroupEntry ds_entries[3] = {
                { .binding = 0,
                  .textureView = existing->cube_mip_read_views[m - 1] },
                { .binding = 1, .sampler = existing->clamp_sampler },
                { .binding = 2,
                  .textureView = existing->cube_mip_storage_views[m] }
            };
            existing->cube_ds_bind_groups[m] = wgpuDeviceCreateBindGroup(
                engine->device, &(WGPUBindGroupDescriptor){
                    .layout = existing->cube_ds_layout,
                    .entryCount = 3, .entries = ds_entries });
        }
    }

    if (was_deferred) ecs_defer_resume(world);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Pass helpers                                                               */
/* -------------------------------------------------------------------------- */

static bool flecsEngine_atmos_runPassBG(
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group,
    WGPUTextureView target_view)
{
    WGPURenderPassColorAttachment color = {
        .view = target_view,
        WGPU_DEPTH_SLICE
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    if (!pass) return false;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static bool flecsEngine_atmos_runTrans(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->trans_pipeline,
        a->trans_bind_group, a->trans_view);
}

static bool flecsEngine_atmos_runMS(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->ms_pipeline,
        a->ms_bind_group, a->ms_view);
}

static bool flecsEngine_atmos_runSkyView(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->skyview_pipeline,
        a->skyview_bind_group, a->skyview_view);
}

/* Helper: run a compute pass with one cached bind group and a single
 * dispatch. Single command per call. */
static bool flecsEngine_atmos_runComputePass(
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    WGPUBindGroup bind_group,
    uint32_t gx, uint32_t gy, uint32_t gz)
{
    WGPUComputePassDescriptor desc = {0};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(
        encoder, &desc);
    if (!pass) return false;
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, gx, gy, gz);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    return true;
}

static bool flecsEngine_atmos_runAerial(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    (void)engine;
    FLECS_TRACY_ZONE_BEGIN("AtmosAerial");
    /* 32x32x32 texels, workgroup size (8,8,1) -> dispatch (4,4,32). */
    bool ok = flecsEngine_atmos_runComputePass(
        encoder, a->aerial_compute_pipeline, a->aerial_compute_bind_group,
        (FLECS_ATMOS_AERIAL_W + 7) / 8,
        (FLECS_ATMOS_AERIAL_H + 7) / 8,
        FLECS_ATMOS_AERIAL_SLICES);
    FLECS_TRACY_ZONE_END;
    return ok;
}

static bool flecsEngine_atmos_runCompose(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    FLECS_TRACY_ZONE_BEGIN("AtmosCompose");
    if (!engine->depth.depth_texture_view) {
        FLECS_TRACY_ZONE_END;
        return false;
    }
    WGPUBindGroupEntry entries[8] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .sampler = a->clamp_sampler },
        { .binding = 2, .textureView = engine->depth.depth_texture_view },
        { .binding = 3, .buffer = a->uniform_buffer,
          .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
        { .binding = 4, .textureView = a->trans_view },
        { .binding = 5, .textureView = a->skyview_view },
        { .binding = 6, .textureView = a->aerial_view },
        { .binding = 7, .sampler = a->clamp_sampler }
    };
    WGPUBindGroupDescriptor bg_desc = {
        .layout = a->compose_layout,
        .entryCount = 8,
        .entries = entries
    };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(engine->device, &bg_desc);
    if (!bg) { FLECS_TRACY_ZONE_END; return false; }

    WGPURenderPipeline pipeline =
        output_format == flecsEngine_getViewTargetFormat(engine)
            ? a->compose_pipeline_surface
            : a->compose_pipeline_hdr;

    WGPURenderPassColorAttachment color = {
        .view = output_view,
        WGPU_DEPTH_SLICE
        .loadOp = output_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    if (!pass) { wgpuBindGroupRelease(bg); FLECS_TRACY_ZONE_END; return false; }
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
    FLECS_TRACY_ZONE_END;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Public render API                                                          */
/* -------------------------------------------------------------------------- */

bool flecsEngine_atmosphere_renderLuts(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    ecs_entity_t view_entity,
    WGPUCommandEncoder encoder)
{
    FlecsAtmosphereImpl *a = ecs_get_mut(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a || !a->uniform_buffer) return false;
    const FlecsAtmosphere *s = ecs_get(
        world, atmosphere_entity, FlecsAtmosphere);
    if (!s) return false;

    FLECS_TRACY_ZONE_BEGIN("AtmosLuts");

    FlecsAtmosphereUniform uniform;
    flecsEngine_atmos_fillUniform(world, view_entity, s, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue, a->uniform_buffer, 0, &uniform, sizeof(uniform));

    bool ok = false;
    FLECS_TRACY_ZONE_BEGIN_N(trans_zone, "AtmosLuts.trans");
    ok = flecsEngine_atmos_runTrans(engine, a, encoder);
    FLECS_TRACY_ZONE_END_N(trans_zone);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    FLECS_TRACY_ZONE_BEGIN_N(ms_zone, "AtmosLuts.ms");
    ok = flecsEngine_atmos_runMS(engine, a, encoder);
    FLECS_TRACY_ZONE_END_N(ms_zone);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    FLECS_TRACY_ZONE_BEGIN_N(sv_zone, "AtmosLuts.skyview");
    ok = flecsEngine_atmos_runSkyView(engine, a, encoder);
    FLECS_TRACY_ZONE_END_N(sv_zone);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    FLECS_TRACY_ZONE_BEGIN_N(ap_zone, "AtmosLuts.aerial");
    ok = flecsEngine_atmos_runAerial(engine, a, encoder);
    FLECS_TRACY_ZONE_END_N(ap_zone);
    FLECS_TRACY_ZONE_END;
    return ok;
}

bool flecsEngine_atmosphere_renderIbl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("AtmosIbl");
    const FlecsAtmosphereImpl *a = ecs_get(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a || !a->cube_face_pipeline || !a->cube_ds_pipeline ||
        a->cube_mip_count == 0)
    {
        FLECS_TRACY_ZONE_END;
        return false;
    }

    /* Pass 1: single compute dispatch writes all 6 cube faces of mip 0. */
    FLECS_TRACY_ZONE_BEGIN_N(faces_zone, "AtmosIbl.faces");
    uint32_t top_size = 128u; /* matches cube_size in ensureImpl */
    bool ok = flecsEngine_atmos_runComputePass(
        encoder, a->cube_face_pipeline, a->cube_face_bind_group,
        (top_size + 7) / 8, (top_size + 7) / 8, 6);
    FLECS_TRACY_ZONE_END_N(faces_zone);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    /* Pass 2+: one compute dispatch per target mip (7 total for 8 mips). */
    FLECS_TRACY_ZONE_BEGIN_N(ds_zone, "AtmosIbl.downsample");
    for (uint32_t m = 1; m < a->cube_mip_count; m++) {
        uint32_t size = top_size >> m;
        if (size < 1u) size = 1u;
        uint32_t gx = (size + 7) / 8;
        uint32_t gy = gx;
        if (!flecsEngine_atmos_runComputePass(encoder,
            a->cube_ds_pipeline, a->cube_ds_bind_groups[m], gx, gy, 6))
        {
            FLECS_TRACY_ZONE_END_N(ds_zone);
            FLECS_TRACY_ZONE_END;
            return false;
        }
    }
    FLECS_TRACY_ZONE_END_N(ds_zone);
    (void)world;
    FLECS_TRACY_ZONE_END;
    return true;
}

bool flecsEngine_atmosphere_renderCompose(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    const FlecsAtmosphereImpl *a = ecs_get(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a) return false;
    return flecsEngine_atmos_runCompose(
        engine, a, encoder, input_view, output_view, output_format, output_load_op);
}

void flecsEngine_atmosphere_register(ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsAtmosphere);
    ECS_COMPONENT_DEFINE(world, FlecsAtmosphereImpl);

    ecs_set_hooks(world, FlecsAtmosphereImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsAtmosphereImpl),
        .dtor = ecs_dtor(FlecsAtmosphereImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsAtmosphere),
        .members = {
            { .name = "sun_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "sun_disk_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "sun_disk_angular_radius", .type = ecs_id(ecs_f32_t) },
            { .name = "aerial_perspective_distance_km", .type = ecs_id(ecs_f32_t) },
            { .name = "aerial_perspective_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "sea_level_y", .type = ecs_id(ecs_f32_t) },
            { .name = "world_units_per_km", .type = ecs_id(ecs_f32_t) },
            { .name = "ground_altitude_km", .type = ecs_id(ecs_f32_t) },
            { .name = "planet_radius_km", .type = ecs_id(ecs_f32_t) },
            { .name = "atmosphere_thickness_km", .type = ecs_id(ecs_f32_t) },
            { .name = "rayleigh_scale_height_km", .type = ecs_id(ecs_f32_t) },
            { .name = "mie_scale_height_km", .type = ecs_id(ecs_f32_t) },
            { .name = "mie_anisotropy", .type = ecs_id(ecs_f32_t) },
            { .name = "ground_albedo", .type = ecs_id(flecs_rgba_t) }
        }
    });
}
