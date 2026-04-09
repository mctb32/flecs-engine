#include "../../renderer.h"

bool flecsEngine_shader_usesIbl(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(0) @binding(1) var ibl_prefiltered_env") != NULL;
}

bool flecsEngine_shader_usesShadow(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(0) @binding(5) var shadow_map") != NULL;
}

bool flecsEngine_shader_usesCluster(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@binding(7) var<uniform> cluster_info") != NULL;
}

bool flecsEngine_shader_usesTextures(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "var albedo_tex_1024") != NULL;
}
