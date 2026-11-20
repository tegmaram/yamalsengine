#include "player.h"
#include <algorithm>

namespace {
// Push a vertical capsule (3 sphere samples) out of a box.
bool resolveCapsule(vec3& feet, float radius, float height, const AABB& box, vec3& hitNormal) {
    bool hit = false;
    const float ys[3] = {radius, height * 0.5f, height - radius};
    for (int i = 0; i < 3; ++i) {
        vec3 c{feet.x, feet.y + ys[i], feet.z};
        vec3 q{clampf(c.x, box.mn.x, box.mx.x), clampf(c.y, box.mn.y, box.mx.y),
               clampf(c.z, box.mn.z, box.mx.z)};
        vec3 d = c - q;
        float dist = length(d);
        if (dist < radius - 1e-6f) {
            vec3 n;
            if (dist > 1e-5f) {
                n = d / dist;
            } else {
                // center inside the box: push out along the shallowest axis
                float px = std::min(std::fabs(c.x - box.mn.x), std::fabs(box.mx.x - c.x));
                float py = std::min(std::fabs(c.y - box.mn.y), std::fabs(box.mx.y - c.y));
                float pz = std::min(std::fabs(c.z - box.mn.z), std::fabs(box.mx.z - c.z));
                if (px <= py && px <= pz) n = {c.x < (box.mn.x + box.mx.x) * 0.5f ? -1.f : 1.f, 0, 0};
                else if (py <= pz) n = {0, c.y < (box.mn.y + box.mx.y) * 0.5f ? -1.f : 1.f, 0};
                else n = {0, 0, c.z < (box.mn.z + box.mx.z) * 0.5f ? -1.f : 1.f};
            }
            float push = radius - dist;
            feet += n * push;
            hitNormal = n;
            hit = true;
        }
    }
    return hit;
}
} // namespace

void Player::look(float dx, float dy) {
    yaw += dx * sensitivity;
    pitch -= dy * sensitivity;
    const float lim = radians(85.0f);
    pitch = clampf(pitch, -lim, lim);
}

void Player::update(const Platform& in, const std::vector<AABB>& solids, float dt) {
    // --- crouch ---
    bool wantCrouch = in.keys['C'] || in.keys[KEY_CONTROL];
    float target = wantCrouch ? 1.0f : 0.0f;
    crouch += (target - crouch) * (1.0f - std::exp(-14.0f * dt));
    if (crouch < 1e-3f) crouch = 0.0f;

    // --- wish direction (horizontal) ---
    float fwd = (in.keys['W'] ? 1.0f : 0.0f) - (in.keys['S'] ? 1.0f : 0.0f);
    float strafe = (in.keys['D'] ? 1.0f : 0.0f) - (in.keys['A'] ? 1.0f : 0.0f);
    vec3 f{std::sin(yaw), 0.0f, -std::cos(yaw)};
    vec3 r{std::cos(yaw), 0.0f, std::sin(yaw)};
    vec3 wish = f * fwd + r * strafe;
    float wl = length(wish);
    if (wl > 1e-4f) wish = wish / wl;

    float speed = crouch > 0.5f ? crouchSpeed : walkSpeed;
    if (in.keys[KEY_SHIFT] && crouch < 0.5f) speed = sprintSpeed;
    vec3 targetVel = wish * speed;

    // --- exponential damping (frame-rate independent, no Quake friction) ---
    float k = grounded ? 12.0f : 2.5f;
    float a = 1.0f - std::exp(-k * dt);
    vel.x += (targetVel.x - vel.x) * a;
    vel.z += (targetVel.z - vel.z) * a;

    // --- gravity ---
    vel.y -= gravity * dt;

    // --- integrate + resolve ---
    pos += vel * dt;

    vec3 n{0, 0, 0};
    for (int iter = 0; iter < 4; ++iter) {
        bool any = false;
        for (const AABB& b : solids) {
            vec3 hitN{0, 0, 0};
            if (resolveCapsule(pos, radius, height(), b, hitN)) {
                any = true;
                n = hitN;
            }
        }
        if (!any) break;
    }

    grounded = n.y > 0.65f;
    if (n.y > 0.65f && vel.y < 0.0f) vel.y = 0.0f;
    if (n.y < -0.65f && vel.y > 0.0f) vel.y = 0.0f;

    // --- eye height smoothing (separate from capsule to avoid popping) ---
    float eyeTarget = eyeStand + (eyeCrouch - eyeStand) * crouch;
    eyeSmooth += (eyeTarget - eyeSmooth) * (1.0f - std::exp(-18.0f * dt));

    // --- head bob (subtle, disabled for horror by default) ---
    // if you want it: bob = sin(walkPhase) * 0.02f while grounded
}
