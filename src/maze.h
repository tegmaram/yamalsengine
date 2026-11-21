#pragma once
#include "mathx.h"
#include "player.h"
#include "vk_engine.h"
#include <vector>

struct MazeResult {
    vec3 spawn{0, 0, 0};
    float spawnYaw = 0.0f;
    int cellsX = 0;
    int cellsZ = 0;
    // Open passages: +X, -X, +Z, -Z. nextRelay points one cell closer.
    std::vector<uint8_t> passages;
    std::vector<int> nextRelay;
    int relay = 0;
    vec3 center(int id) const { return spawn + vec3(float(id%cellsX)*5,0,float(id/cellsX)*5); }
    int cellAt(vec3 p) const {
        int x=int(clampf(std::floor((p.x-spawn.x)/5+.5f),0,float(cellsX-1)));
        int z=int(clampf(std::floor((p.z-spawn.z)/5+.5f),0,float(cellsZ-1)));
        return z*cellsX+x;
    }
    float extent = 0.0f;
};

// Recursive-backtracker maze. Emits geometry + collision boxes, returns a spawn point.
MazeResult buildMaze(uint32_t seed, int cellsX, int cellsZ, MeshData& mesh,
                     std::vector<AABB>& solids);
