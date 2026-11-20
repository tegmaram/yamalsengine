#pragma once
#include "mathx.h"
#include <cstdint>
#include <vector>

struct Vertex {
    vec3 p;
    vec3 n;
    vec2 uv;
    vec4 c;
};

struct MeshData {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
};

struct CameraState {
    vec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovY = radians(70.0f);
};

struct SkyState {
    vec3 sunDir{0.3f, 0.15f, 0.6f};
    float coverage = 0.55f;
    float exposure = 1.0f;
    float time = 0.0f;

    // weather
    float storm = 0.0f;    // 0 calm .. 1 thunderstorm
    float wetness = 0.0f;  // 0 dry .. 1 soaked floor
    float rain = 0.0f;     // 0 .. 1 rain overlay intensity
    float cloudBase = 900.0f;
    float cloudTop = 1800.0f;

    // flashlight + lightning flash
    vec3 lightDir{0, 0, -1};
    float flashlight = 0.0f;
    float flash = 0.0f;
};

class Renderer {
public:
    bool init(void* hwnd, int width, int height, const char* shaderDir, float sunAzimuth);
    void shutdown();

    // Returns false when the frame should be skipped (minimized / out of date).
    bool beginFrame(int width, int height, const CameraState& cam, const SkyState& sky);
    void drawWorld(const MeshData& mesh); // uploads on first call
    void endFrame();
    void waitIdle();

    // Captures the next presented frame to a 24-bit BMP.
    void requestScreenshot(const char* path);

    // VRAM numbers from VK_EXT_memory_budget (0 when unsupported).
    float vramUsedMB = 0.0f;
    float vramBudgetMB = 0.0f;

private:
    struct Impl;
    Impl* m = nullptr;
};
