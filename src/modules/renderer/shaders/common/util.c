#include "../../renderer.h"

bool flecsEngine_shader_usesIbl(
    const FlecsShader *shader)
{
    if (!shader || !shader->source) {
        return false;
    }

    return strstr(
        shader->source,
        "@group(1) @binding(0) var ibl_prefiltered_env") != NULL;
}
