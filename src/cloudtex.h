#pragma once
#include <cstdint>
#include <vector>

// Baked 2D cumulus sheet (RGBA8, seamless, tiles in both axes).
//   R = coverage / shape mass   (0..1, low frequency)
//   G = density                 (0..1, masked by a fixed coverage so it reads as a cloud body)
//   B = detail erosion          (0..1, high frequency, used to chew the edges)
//   A = baked sun shadow        (0..1, 1 = lit, 0 = fully shadowed)
struct CloudSheet {
    int size = 0;
    std::vector<uint8_t> rgba;
};

// sunAzimuth in radians (0 = +X, pi/2 = +Z). ~0.5-2s on one core at 1024.
CloudSheet generateCloudSheet(int size, uint32_t seed, float sunAzimuth, float coverage);
