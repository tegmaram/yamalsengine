#pragma once
#include "vk_engine.h"

// Emits an axis-aligned box into a mesh. `inward` flips normals (for interiors).
inline void addBox(MeshData& m, vec3 c, vec3 h, vec4 col, bool inward = false) {
    const vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; ++a) {
        for (int s = 0; s < 2; ++s) {
            float sign = s == 0 ? 1.0f : -1.0f;
            vec3 n = axis[a] * (inward ? -sign : sign);
            vec3 u = axis[(a + 1) % 3];
            vec3 v = axis[(a + 2) % 3];
            float hu = (&h.x)[(a + 1) % 3];
            float hv = (&h.x)[(a + 2) % 3];
            vec3 base = c + axis[a] * ((&h.x)[a] * sign);

            vec3 p[4] = {base - u * hu - v * hv, base + u * hu - v * hv, base + u * hu + v * hv,
                         base - u * hu + v * hv};
            vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

            vec4 fc = col;
            float shade = 0.92f + 0.08f * float((a * 2 + s) % 3);
            fc.x *= shade;
            fc.y *= shade;
            fc.z *= shade;

            uint32_t baseIdx = (uint32_t)m.verts.size();
            for (int i = 0; i < 4; ++i) m.verts.push_back({p[i], n, uv[i], fc});
            if (inward) {
                m.idx.insert(m.idx.end(), {baseIdx + 0, baseIdx + 2, baseIdx + 1, baseIdx + 0,
                                           baseIdx + 3, baseIdx + 2});
            } else {
                m.idx.insert(m.idx.end(), {baseIdx + 0, baseIdx + 1, baseIdx + 2, baseIdx + 0,
                                           baseIdx + 2, baseIdx + 3});
            }
        }
    }
}
