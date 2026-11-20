// Minimal math library. Column-major matrices, Vulkan convention (Y down NDC, depth 0..1).
#pragma once
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979323846f;

struct vec2 { float x = 0, y = 0; };
struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline vec3 operator*(float s, const vec3& v) { return v * s; }
inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(const vec3& v) { float l = length(v); return l > 1e-8f ? v / l : vec3(0, 1, 0); }
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    vec4(const vec3& v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
    vec3 xyz() const { return {x, y, z}; }
};

struct mat4 {
    // m[column][row]
    float m[4][4]{};

    static mat4 identity() {
        mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
    mat4 operator*(const mat4& b) const {
        mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k][row] * b.m[c][k];
                r.m[c][row] = s;
            }
        return r;
    }
    vec4 operator*(const vec4& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
                m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
    }
    static mat4 translate(const vec3& t) {
        mat4 r = identity();
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }
    static mat4 scale(const vec3& s) {
        mat4 r = identity();
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
};

// Right-handed look-at, Vulkan clip space.
inline mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up) {
    vec3 f = normalize(target - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r;
    r.m[0][0] = s.x; r.m[0][1] = u.x; r.m[0][2] = -f.x; r.m[0][3] = 0;
    r.m[1][0] = s.y; r.m[1][1] = u.y; r.m[1][2] = -f.y; r.m[1][3] = 0;
    r.m[2][0] = s.z; r.m[2][1] = u.z; r.m[2][2] = -f.z; r.m[2][3] = 0;
    r.m[3][0] = -dot(s, eye); r.m[3][1] = -dot(u, eye); r.m[3][2] = dot(f, eye); r.m[3][3] = 1;
    return r;
}

inline mat4 perspectiveVulkan(float fovYRadians, float aspect, float znear, float zfar) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    mat4 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f; // flip Y for Vulkan
    r.m[2][2] = zfar / (znear - zfar);
    r.m[2][3] = -1.0f;
    r.m[3][2] = (zfar * znear) / (znear - zfar);
    return r;
}

inline mat4 inverse(const mat4& a) {
    const float* m = &a.m[0][0];
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (std::fabs(det) < 1e-12f) return mat4::identity();
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) (&r.m[0][0])[i] = inv[i] * det;
    return r;
}

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float radians(float deg) { return deg * PI / 180.0f; }
