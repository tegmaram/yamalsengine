#pragma once
#include "player.h"
#include "vk_engine.h"

// The maze is made of boxes. Keep the closest eight within lamp range instead
// of rendering another depth pass. The floor receives light, never blocks it.
inline void prepareLamp(SkyState& sky, vec3 eye, const std::vector<AABB>& solids) {
    sky.blockers=0;
    if (!sky.flashlight) return;
    float scores[8]{};
    for (const auto& b:solids) {
        if(b.mx.y<=0) continue;
        vec3 q{clampf(eye.x,b.mn.x,b.mx.x),clampf(eye.y,b.mn.y,b.mx.y),clampf(eye.z,b.mn.z,b.mx.z)};
        float score=dot(q-eye,q-eye);
        if(score>18*18) continue;
        int i=0; while(i<sky.blockers && scores[i]<=score) ++i;
        if(i==8) continue;
        if(sky.blockers<8) ++sky.blockers;
        for(int j=sky.blockers-1;j>i;--j){ scores[j]=scores[j-1]; sky.lo[j]=sky.lo[j-1]; sky.hi[j]=sky.hi[j-1]; }
        scores[i]=score; sky.lo[i]=vec4(b.mn,0); sky.hi[i]=vec4(b.mx,0);
    }
}
