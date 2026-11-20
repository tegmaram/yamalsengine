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
    float extent = 0.0f;
};

// Recursive-backtracker maze. Emits geometry + collision boxes, returns a spawn point.
MazeResult buildMaze(uint32_t seed, int cellsX, int cellsZ, MeshData& mesh,
                     std::vector<AABB>& solids);
