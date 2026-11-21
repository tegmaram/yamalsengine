#include "maze.h"
#include "meshbuild.h"

#include <random>
#include <algorithm>

MazeResult buildMaze(uint32_t seed, int W, int H, MeshData& mesh, std::vector<AABB>& solids) {
    if (W<3 || H<3) return {};
    const float cell = 5.0f;
    const float wallH = 7.5f;
    const float halfT = 0.15f; // half wall thickness

    // vWall[x][y] = wall on the west edge of cell (x,y), x in [0..W]
    std::vector<uint8_t> vWall((size_t)(W + 1) * H, 1);
    // hWall[x][y] = wall on the north edge of cell (x,y), y in [0..H]
    std::vector<uint8_t> hWall((size_t)W * (H + 1), 1);

    auto V = [&](int x, int y) -> uint8_t& { return vWall[(size_t)y * (W + 1) + x]; };
    auto Hh = [&](int x, int y) -> uint8_t& { return hWall[(size_t)y * W + x]; };

    // ---- carve with an iterative depth-first search ----
    std::mt19937 rng(seed);
    std::vector<uint8_t> visited((size_t)W * H, 0);
    std::vector<std::pair<int, int>> stack;
    stack.reserve((size_t)W * H);
    stack.push_back({0, 0});
    visited[0] = 1;

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (!stack.empty()) {
        auto [x, y] = stack.back();
        int options[4];
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            if (visited[(size_t)ny * W + nx]) continue;
            options[n++] = i;
        }
        if (n == 0) {
            stack.pop_back();
            continue;
        }
        int i = options[rng() % n];
        int nx = x + dx[i], ny = y + dy[i];
        if (nx > x) V(x + 1, y) = 0;
        else if (nx < x) V(x, y) = 0;
        else if (ny > y) Hh(x, y + 1) = 0;
        else Hh(x, y) = 0;

        visited[(size_t)ny * W + nx] = 1;
        stack.push_back({nx, ny});
    }

    // A few extra loops so the maze is not a pure tree (less frustrating, more readable).
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (x + 1 < W && (rng() % 100) < 12) V(x + 1, y) = 0;
            if (y + 1 < H && (rng() % 100) < 12) Hh(x, y + 1) = 0;
        }
    }

    // ---- geometry ----
    // Collinear wall segments are merged into runs before emitting boxes. Two boxes
    // that share a coplanar face with different shading is exactly what caused the
    // z-fighting, so no two wall boxes are allowed to touch face-to-face.
    const float ox = -(float)W * cell * 0.5f;
    const float oz = -(float)H * cell * 0.5f;

    const vec4 floorCol{0.185f, 0.175f, 0.160f, 1.0f};
    const vec4 wallCol{0.245f, 0.242f, 0.234f, 1.0f};

    addBox(mesh, {0, -0.25f, 0}, {(float)W * cell * 0.5f + 1.0f, 0.25f,
                                  (float)H * cell * 0.5f + 1.0f}, floorCol);
    solids.push_back({{-1e4f, -0.6f, -1e4f}, {1e4f, 0.0f, 1e4f}});

    const float wallY = wallH * 0.5f - 0.05f; // sunk into the floor

    // vertical runs: constant x, varying z
    for (int x = 0; x <= W; ++x) {
        int y = 0;
        while (y < H) {
            if (!V(x, y)) {
                ++y;
                continue;
            }
            int y0 = y;
            while (y < H && V(x, y)) ++y;
            int y1 = y;
            float z0 = oz + (float)y0 * cell;
            float z1 = oz + (float)y1 * cell;
            vec3 c{ox + (float)x * cell, wallY, (z0 + z1) * 0.5f};
            vec3 h{halfT, wallH * 0.5f, (z1 - z0) * 0.5f};
            addBox(mesh, c, h, wallCol);
            solids.push_back({c - h, c + h});
        }
    }

    // horizontal runs: constant z, varying x
    for (int y = 0; y <= H; ++y) {
        int x = 0;
        while (x < W) {
            if (!Hh(x, y)) {
                ++x;
                continue;
            }
            int x0 = x;
            while (x < W && Hh(x, y)) ++x;
            int x1 = x;
            float wx0 = ox + (float)x0 * cell;
            float wx1 = ox + (float)x1 * cell;
            vec3 c{(wx0 + wx1) * 0.5f, wallY, oz + (float)y * cell};
            vec3 h{(wx1 - wx0) * 0.5f, wallH * 0.5f, halfT};
            addBox(mesh, c, h, wallCol);
            solids.push_back({c - h, c + h});
        }
    }

    // a couple of debris crates for visual anchors + cover
    for (int i = 0; i < 8; ++i) {
        int cx = 1 + (int)(rng() % (W - 2));
        int cy = 1 + (int)(rng() % (H - 2));
        vec3 c{ox + ((float)cx + 0.5f) * cell, 0.33f, oz + ((float)cy + 0.5f) * cell};
        vec3 h{0.35f, 0.35f, 0.35f};
        addBox(mesh, c, h, {0.24f, 0.19f, 0.14f, 1.0f});
        solids.push_back({c - h, c + h});
    }

    MazeResult r;
    r.spawn = {ox + cell * 0.5f, 0.0f, oz + cell * 0.5f};
    r.cellsX = W;
    r.cellsZ = H;
    r.extent = (float)std::max(W, H) * cell;

    // face the first open edge so the player does not spawn staring at a wall
    if (!V(1, 0)) r.spawnYaw = radians(90.0f);
    else if (!Hh(0, 1)) r.spawnYaw = radians(180.0f);
    else r.spawnYaw = radians(0.0f);

    r.passages.resize(W*H);
    for (int z=0; z<H; ++z) for (int x=0; x<W; ++x)
        r.passages[z*W+x] = !V(x+1,z) | (!V(x,z)<<1) | (!Hh(x,z+1)<<2) | (!Hh(x,z)<<3);
    auto traverse = [&](int start) {
        r.nextRelay.assign(W*H,-1);
        std::vector<int> queue{start}; r.nextRelay[start]=start;
        const int offset[4]={1,-1,W,-W};
        for (size_t i=0; i<queue.size(); ++i) for(int d=0; d<4; ++d) {
            if (!(r.passages[queue[i]] & (1<<d))) continue;
            int n=queue[i]+offset[d];
            if (r.nextRelay[n]>=0) continue;
            r.nextRelay[n]=queue[i]; queue.push_back(n);
        }
        return queue.back();
    };
    r.relay=traverse(0); traverse(r.relay);
    vec3 beacon=r.center(r.relay);
    addBox(mesh,beacon+vec3(0,.8f,0),{.28f,.8f,.28f},{.85f,.26f,.03f,1});
    solids.push_back({beacon-vec3(.28f,0,.28f),beacon+vec3(.28f,1.6f,.28f)});
    return r;
}
