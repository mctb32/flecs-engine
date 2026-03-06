#include "types.h"

void flecsEngine_registerVec3Type(
    ecs_world_t *world,
    ecs_entity_t component);

WGPUColor flecsEngine_getClearColor(
    const FlecsEngineImpl *impl);

void flecsEngine_getClearColorVec4(
    const FlecsEngineImpl *impl,
    float out[4]);

uint64_t flecsEngine_type_sizeof(
    const ecs_world_t *world,
    ecs_entity_t type);

float flecsEngine_colorChannelToFloat(
    uint8_t value);

int32_t flecsEngine_vertexAttrFromType(
    const ecs_world_t *world,
    ecs_entity_t type,
    WGPUVertexAttribute *attrs,
    int32_t attr_count,
    int32_t location_offset);

ecs_entity_t flecsEngine_vecEntity(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecVec3(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecU16(
    ecs_world_t *world);
