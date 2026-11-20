#pragma once
#include "mathx.h"
#include "platform.h"
#include <vector>

struct AABB {
    vec3 mn, mx;
};

struct Player {
    vec3 pos{0, 0, 0};   // feet position
    vec3 vel{0, 0, 0};
    float yaw = 0.0f;    // radians, 0 looks down -Z
    float pitch = 0.0f;
    float crouch = 0.0f; // 0 standing .. 1 crouched
    float eyeSmooth = 1.6f;
    bool grounded = true;

    // tuning
    float radius = 0.30f;
    float standHeight = 1.80f;
    float crouchHeight = 1.15f;
    float eyeStand = 1.60f;
    float eyeCrouch = 1.00f;
    float walkSpeed = 3.0f;
    float crouchSpeed = 1.4f;
    float sprintSpeed = 4.6f;
    float gravity = 20.0f;
    float sensitivity = 0.0022f;

    float height() const { return standHeight + (crouchHeight - standHeight) * crouch; }
    vec3 eye() const { return {pos.x, pos.y + eyeSmooth, pos.z}; }
    vec3 forward() const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        return normalize({std::sin(yaw) * cp, sp, -std::cos(yaw) * cp});
    }

    void look(float dx, float dy);
    void update(const Platform& in, const std::vector<AABB>& solids, float dt);
};
