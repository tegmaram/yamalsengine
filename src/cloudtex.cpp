#include "cloudtex.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace {

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float rnd01(uint32_t h) { return float(h & 0x00FFFFFFu) / float(0x01000000u); }
inline int wrapi(int v, int p) { int r = v % p; return r < 0 ? r + p : r; }
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Seamless 2D gradient noise. `period` = lattice cells across the unit square.
float perlin2(float x, float y, int period, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = fade(xf), v = fade(yf);

    auto dotGrad = [&](int cx, int cy, float dx, float dy) {
        uint32_t h = hash2(wrapi(cx, period), wrapi(cy, period), seed);
        // 8 evenly spaced gradients
        float a = float(h & 7u) * 0.78539816339f;
        return std::cos(a) * dx + std::sin(a) * dy;
    };

    float n00 = dotGrad(xi, yi, xf, yf);
    float n10 = dotGrad(xi + 1, yi, xf - 1, yf);
    float n01 = dotGrad(xi, yi + 1, xf, yf - 1);
    float n11 = dotGrad(xi + 1, yi + 1, xf - 1, yf - 1);

    float nx0 = n00 + u * (n10 - n00);
    float nx1 = n01 + u * (n11 - n01);
    return (nx0 + v * (nx1 - nx0)) * 1.6f; // roughly -1..1
}

float fbm2(float x, float y, int period, int octaves, float gain, uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    int p = period;
    for (int i = 0; i < octaves; ++i) {
        // detune slightly to hide octave alignment; keep the period integer so it tiles
        sum += perlin2(x, y, p, seed + (uint32_t)i * 101u) * amp;
        norm += amp;
        x *= 2.0f;
        y *= 2.0f;
        p *= 2;
        amp *= gain;
    }
    return sum / norm;
}

// Seamless Worley F1, `cells` per unit square. Returns 1 - distance (1 at cell centre).
float worley2(float x, float y, int cells, uint32_t seed) {
    float best = 1e9f;
    int xi = (int)std::floor(x * cells), yi = (int)std::floor(y * cells);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = xi + dx, cy = yi + dy;
            uint32_t h = hash2(wrapi(cx, cells), wrapi(cy, cells), seed);
            float fx = (float)cx + rnd01(h);
            float fy = (float)cy + rnd01(h * 747796405u + 2891336453u);
            float ddx = fx - x * cells, ddy = fy - y * cells;
            float d = std::sqrt(ddx * ddx + ddy * ddy);
            best = std::min(best, d);
        }
    }
    return std::clamp(1.0f - best * 0.85f, 0.0f, 1.0f);
}

inline float smoothstepf(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

CloudSheet generateCloudSheet(int size, uint32_t seed, float sunAzimuth, float coverage) {
    CloudSheet sheet;
    sheet.size = size;
    sheet.rgba.assign((size_t)size * size * 4, 0);

    std::vector<float> mass((size_t)size * size);
    std::vector<float> density((size_t)size * size);

    const float inv = 1.0f / float(size);

    // ---- 1. shape: large warped masses + billow texture ----
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) * inv;
            float v = (y + 0.5f) * inv;

            // one warp level drives the big silhouettes
            float qx = fbm2(u * 2.0f, v * 2.0f, 2, 3, 0.55f, seed + 11u);
            float qy = fbm2(u * 2.0f + 0.31f, v * 2.0f + 0.77f, 2, 3, 0.55f, seed + 71u);

            float big = fbm2(u * 2.0f + qx * 0.28f, v * 2.0f + qy * 0.28f, 2, 3, 0.5f, seed);
            big = big * 0.5f + 0.5f;

            float puff = fbm2(u * 6.0f + qx * 0.6f, v * 6.0f + qy * 0.6f, 6, 4, 0.55f, seed + 5u);
            puff = std::fabs(puff) * 2.0f - 1.0f;
            puff = puff * 0.5f + 0.5f;

            float m = big * 0.74f + puff * 0.26f;
            m = smoothstepf(0.15f, 0.88f, m);
            mass[(size_t)y * size + x] = m;
        }
    }

    // ---- 2. density: coverage window ----
    const float lo = 1.0f - coverage;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            size_t i = (size_t)y * size + x;
            float m = mass[i];
            float d = smoothstepf(lo, lo + 0.22f, m);
            density[i] = d;
        }
    }

    // ---- 3. thickness channel: blurred density (used for AO / edge / rim) ----
    const int R = 10;
    std::vector<float> tmp((size_t)size * size, 0.0f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;
            for (int k = -R; k <= R; ++k) acc += density[(size_t)y * size + wrapi(x + k, size)];
            tmp[(size_t)y * size + x] = acc / float(2 * R + 1);
        }
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;
            for (int k = -R; k <= R; ++k) acc += tmp[(size_t)wrapi(y + k, size) * size + x];
            float thick = acc / float(2 * R + 1);
            thick = smoothstepf(0.30f, 0.72f, thick);
            sheet.rgba[((size_t)y * size + x) * 4 + 3] =
                (uint8_t)std::clamp(thick * 255.0f, 0.0f, 255.0f);
        }
    }

    // ---- 4. detail channel ----
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) * inv;
            float v = (y + 0.5f) * inv;
            size_t i = (size_t)y * size + x;
            float det = fbm2(u * 24.0f, v * 24.0f, 24, 3, 0.6f, seed + 999u);
            det = det * 0.5f + 0.5f;
            sheet.rgba[i * 4 + 2] = (uint8_t)std::clamp(det * 255.0f, 0.0f, 255.0f);
            sheet.rgba[i * 4 + 1] = (uint8_t)std::clamp(density[i] * 255.0f, 0.0f, 255.0f);
            sheet.rgba[i * 4 + 0] = (uint8_t)std::clamp(mass[i] * 255.0f, 0.0f, 255.0f);
        }
    }
    return sheet;
}
