#include <math.h>

#include "../../private.h"

#define CAMERA_DECELERATION 100.0
#define CAMERA_ANGULAR_DECELERATION 5.0

static const float CameraDeceleration = CAMERA_DECELERATION;
static const float CameraAcceleration = 50.0 + CAMERA_DECELERATION;
static const float CameraAngularDeceleration = CAMERA_ANGULAR_DECELERATION;
static const float CameraAngularAcceleration = 2.5 + CAMERA_ANGULAR_DECELERATION;
static const float CameraMaxSpeed = 40.0;
static const float CameraMouseSensitivity = 0.003;

extern ECS_COMPONENT_DECLARE(FlecsLookAt);
extern ECS_COMPONENT_DECLARE(FlecsPosition3);
extern ECS_COMPONENT_DECLARE(FlecsRotation3);
extern ECS_COMPONENT_DECLARE(FlecsVelocity3);
extern ECS_COMPONENT_DECLARE(FlecsAngularVelocity3);
extern ECS_COMPONENT_DECLARE(FlecsInput);
extern ECS_TAG_DECLARE(FlecsCameraController);

static
void CameraControllerSyncRotation(
    ecs_iter_t *it)
{
    const FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 0);
    const FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 1);
    FlecsLookAt *lookat = ecs_field(it, FlecsLookAt, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        lookat[i].x = p[i].x + sin(r[i].y) * cos(r[i].x);
        lookat[i].y = p[i].y + sin(r[i].x);
        lookat[i].z = p[i].z + cos(r[i].y) * cos(r[i].x);
    }
}

static
void CameraControllerAccelerate(
    ecs_iter_t *it)
{
    const FlecsInput *input = ecs_field(it, FlecsInput, 0);
    const FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 1);
    FlecsVelocity3 *v = ecs_field(it, FlecsVelocity3, 2);
    FlecsAngularVelocity3 *av = ecs_field(it, FlecsAngularVelocity3, 3);

    for (int32_t i = 0; i < it->count; i ++) {
        float angle = r[i].y;
        float accel = CameraAcceleration * it->delta_time;
        float angular_accel = CameraAngularAcceleration * it->delta_time;

        if (input->keys[FLECS_KEY_W].state) {
            v[i].x += sin(angle) * accel;
            v[i].z += cos(angle) * accel;
        }
        if (input->keys[FLECS_KEY_S].state) {
            v[i].x += sin(angle + GLM_PI) * accel;
            v[i].z += cos(angle + GLM_PI) * accel;
        }

        if (input->keys[FLECS_KEY_A].state) {
            v[i].x += cos(angle) * accel;
            v[i].z -= sin(angle) * accel;
        }
        if (input->keys[FLECS_KEY_D].state) {
            v[i].x += cos(angle + GLM_PI) * accel;
            v[i].z -= sin(angle + GLM_PI) * accel;
        }

        if (input->keys[FLECS_KEY_E].state) {
            v[i].y += accel;
        }
        if (input->keys[FLECS_KEY_Q].state) {
            v[i].y -= accel;
        }

        if (input->keys[FLECS_KEY_RIGHT].state) {
            av[i].y -= angular_accel;
        }
        if (input->keys[FLECS_KEY_LEFT].state) {
            av[i].y += angular_accel;
        }

        if (input->keys[FLECS_KEY_UP].state) {
            av[i].x += angular_accel;
        }
        if (input->keys[FLECS_KEY_DOWN].state) {
            av[i].x -= angular_accel;
        }

        if ((input->mouse.left.state || input->mouse.right.state)
            && it->delta_time > 0) {
            av[i].y = -input->mouse.rel.x * CameraMouseSensitivity
                / it->delta_time;
            av[i].x = -input->mouse.rel.y * CameraMouseSensitivity
                / it->delta_time;
        }
    }
}

static
void flecs_camera_controller_decel(
    float *v_ptr,
    float a,
    float dt)
{
    float v = v_ptr[0];

    if (v > 0) {
        v = glm_clamp(v - a * dt, 0, v);
    }
    if (v < 0) {
        v = glm_clamp(v + a * dt, v, 0);
    }

    v_ptr[0] = v;
}

static
void CameraControllerDecelerate(
    ecs_iter_t *it)
{
    FlecsVelocity3 *v = ecs_field(it, FlecsVelocity3, 0);
    FlecsAngularVelocity3 *av = ecs_field(it, FlecsAngularVelocity3, 1);
    FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 2);

    float dt = it->delta_time;
    vec3 zero = {0};

    for (int32_t i = 0; i < it->count; i ++) {
        vec3 v3 = {v[i].x, v[i].y, v[i].z}, vn3;
        glm_vec3_normalize_to(v3, vn3);

        float speed = glm_vec3_distance(zero, v3);
        if (speed > CameraMaxSpeed) {
            glm_vec3_scale(v3, CameraMaxSpeed / speed, v3);
            v[i].x = v3[0];
            v[i].y = v3[1];
            v[i].z = v3[2];
        }

        flecs_camera_controller_decel(&v[i].x, CameraDeceleration * fabs(vn3[0]), dt);
        flecs_camera_controller_decel(&v[i].y, CameraDeceleration * fabs(vn3[1]), dt);
        flecs_camera_controller_decel(&v[i].z, CameraDeceleration * fabs(vn3[2]), dt);

        flecs_camera_controller_decel(&av[i].x, CameraAngularDeceleration, dt);
        flecs_camera_controller_decel(&av[i].y, CameraAngularDeceleration, dt);

        if (r[i].x > GLM_PI / 2.0 - 0.05) {
            r[i].x = GLM_PI / 2.0 - 0.05;
            if (av[i].x > 0) av[i].x = 0;
        }
        if (r[i].x < -(GLM_PI / 2.0 - 0.05)) {
            r[i].x = -(GLM_PI / 2.0 - 0.05);
            if (av[i].x < 0) av[i].x = 0;
        }
    }
}

void FlecsEngineCameraControllerImport(
    ecs_world_t *world)
{
    ECS_SYSTEM(world, CameraControllerSyncRotation, EcsPostUpdate,
        [in]     flecs.engine.transform3.Position3,
        [in]     flecs.engine.transform3.Rotation3,
        [out]    flecs.engine.transform3.LookAt,
        [none]   CameraController);

    ECS_SYSTEM(world, CameraControllerAccelerate, EcsPostUpdate,
        [in]     flecs.engine.input.Input,
        [in]     flecs.engine.transform3.Rotation3,
        [inout]  flecs.engine.movement.Velocity3,
        [inout]  flecs.engine.movement.AngularVelocity3,
        [none]   CameraController);

    ECS_SYSTEM(world, CameraControllerDecelerate, EcsPostUpdate,
        [inout]  flecs.engine.movement.Velocity3,
        [inout]  flecs.engine.movement.AngularVelocity3,
        [inout]  flecs.engine.transform3.Rotation3,
        [none]   CameraController);

    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsPosition3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsRotation3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsVelocity3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsAngularVelocity3));
}
