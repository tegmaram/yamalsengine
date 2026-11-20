#include "platform.h"
#include "player.h"
#include "vk_engine.h"
#include "maze.h"
#include "audio.h"
#include "meshbuild.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float FIXED_DT = 1.0f / 60.0f;

uint32_t g_rng = 0x2545F491u;
float rand01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return float((g_rng >> 8) & 0xFFFFFF) / float(0x1000000);
}

// 1 ms timer resolution so the frame limiter's Sleep() is not quantized to 15.6 ms.
struct TimeBeginPeriodGuard {
    TimeBeginPeriodGuard() { timeBeginPeriod(1); }
    ~TimeBeginPeriodGuard() { timeEndPeriod(1); }
};

std::string exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring ws(buf);
    size_t slash = ws.find_last_of(L"\\/");
    if (slash != std::wstring::npos) ws = ws.substr(0, slash);
    char narrow[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    return std::string(narrow);
}

} // namespace

int main(int argc, char** argv) {
    int maxFrames = -1;
    std::string shotPath;
    float yawDeg = 0.0f, pitchDeg = 0.0f, coverage = -1.0f;
    float forcedStorm = -1.0f;
    float forcedFlash = -1.0f;
    bool forcedLight = false;
    uint32_t seed = (uint32_t)GetTickCount64();
    int fpsCap = 180;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == "--yaw" && i + 1 < argc) yawDeg = (float)std::atof(argv[++i]);
        else if (a == "--pitch" && i + 1 < argc) pitchDeg = (float)std::atof(argv[++i]);
        else if (a == "--cov" && i + 1 < argc) coverage = (float)std::atof(argv[++i]);
        else if (a == "--storm" && i + 1 < argc) forcedStorm = (float)std::atof(argv[++i]);
        else if (a == "--flash" && i + 1 < argc) forcedFlash = (float)std::atof(argv[++i]);
        else if (a == "--light" && i + 1 < argc) forcedLight = std::atoi(argv[++i]) != 0;
        else if (a == "--seed" && i + 1 < argc) seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--fps" && i + 1 < argc) fpsCap = std::atoi(argv[++i]);
    }

    Platform pf;
    if (!platformInit(pf, L"horror - maze", 1600, 900)) {
        std::fprintf(stderr, "failed to create window\n");
        return 1;
    }

    const vec3 sunDir = normalize(vec3(0.55f, 0.42f, -0.72f));

    Renderer renderer;
    std::string shaders = exeDir() + "/shaders";
    if (!renderer.init(pf.hwnd, pf.width, pf.height, shaders.c_str(),
                       std::atan2(sunDir.z, sunDir.x))) {
        std::fprintf(stderr, "failed to init renderer\n");
        return 1;
    }

    MeshData world;
    std::vector<AABB> solids;
    MazeResult maze = buildMaze(seed, 9, 9, world, solids);
    std::printf("maze %dx%d, seed %u\n", maze.cellsX, maze.cellsZ, seed);

    Audio audio;
    bool audioOk = audio.init();
    if (!audioOk) std::fprintf(stderr, "[audio] disabled\n");

    Player player;
    player.pos = maze.spawn;
    player.yaw = yawDeg != 0.0f ? radians(yawDeg) : maze.spawnYaw;
    player.pitch = radians(pitchDeg);

    platformSetCapture(pf, shotPath.empty());

    SkyState sky;
    sky.sunDir = sunDir;
    sky.exposure = 1.15f;

    // weather state
    float storm = forcedStorm > 0.0f ? clampf(forcedStorm, 0.0f, 1.0f) : 0.0f;
    float wetness = storm > 0.5f ? 1.0f : 0.0f;
    float thunderTimer = 2.0f;
    bool flashlightOn = forcedLight;
    float flash = 0.0f;

    TimeBeginPeriodGuard timerGuard; // 1 ms timer resolution for the frame limiter

    double prev = platformTime();
    double simAcc = 0.0;
    double fpsTimer = 0.0;
    double deadline = prev;
    const double targetDt = 1.0 / (double)fpsCap;
    int frames = 0;
    int frameNo = 0;
    float fpsSmooth = 0.0f;

    std::printf("WASD move | Shift sprint | C crouch | hold R: storm | F1 release mouse | Esc quit\n");

    while (!pf.quit) {
        platformPoll(pf);
        double now = platformTime();
        float dt = (float)(now - prev);
        prev = now;
        if (dt > 0.25f) dt = 0.25f;

        if (pf.captured) player.look(pf.mouseDX, pf.mouseDY);

        // ---- weather ----
        if (forcedStorm < 0.0f) {
            bool rHeld = pf.keys['R'];
            float target = rHeld ? 1.0f : 0.0f;
            float k = rHeld ? 0.9f : 0.28f;
            storm += (target - storm) * (1.0f - std::exp(-k * dt));
        }
        if (storm > 0.30f) wetness += dt * 0.22f * storm;
        else wetness -= dt * 0.03f;
        wetness = clampf(wetness, 0.0f, 1.0f);

        float rain = smoothstepf(0.30f, 0.95f, storm);
        float cov = coverage >= 0.0f ? coverage : (0.52f + 0.40f * storm);
        float cloudBase = 900.0f - 260.0f * storm;

        // ---- thunder ----
        flash *= std::exp(-dt * 6.0f);
        if (pf.keyPressed['F']) flashlightOn = !flashlightOn;
        if (forcedFlash >= 0.0f) flash = forcedFlash;

        if (storm > 0.55f) {
            thunderTimer -= dt;
            if (thunderTimer <= 0.0f) {
                audio.playThunder(storm);
                flash = 1.0f;
                thunderTimer = 2.5f + rand01() * 8.0f;
            }
        }

        // ---- player ----
        simAcc += dt;
        int steps = 0;
        while (simAcc >= FIXED_DT && steps < 5) {
            player.update(pf, solids, FIXED_DT);
            simAcc -= FIXED_DT;
            ++steps;
        }
        if (steps == 5) simAcc = 0.0;

        sky.time = (float)now;
        sky.storm = storm;
        sky.wetness = wetness;
        sky.rain = rain;
        sky.coverage = cov;
        sky.cloudBase = cloudBase;
        sky.exposure = 1.15f * (1.0f - 0.55f * storm);
        sky.lightDir = player.forward();
        sky.flashlight = flashlightOn ? 1.0f : 0.0f;
        sky.flash = flash;



        CameraState cam;
        cam.pos = player.eye();
        cam.yaw = player.yaw;
        cam.pitch = player.pitch;
        cam.fovY = radians(62.0f);

        if (renderer.beginFrame(pf.width, pf.height, cam, sky)) {
            renderer.drawWorld(world);
            if (maxFrames > 0 && frameNo == maxFrames - 1 && !shotPath.empty())
                renderer.requestScreenshot(shotPath.c_str());
            renderer.endFrame();
        }

        ++frameNo;
        if (maxFrames > 0 && frameNo >= maxFrames) break;

        // ---- frame limiter: sleep to ~1.5 ms before the deadline, spin the tail ----
        deadline += targetDt;
        double after = platformTime();
        if (after > deadline + 0.05) deadline = after; // fell behind, resync
        double wait = deadline - after;
        if (wait > 0.0020) Sleep((DWORD)((wait - 0.0012) * 1000.0));
        while (platformTime() < deadline) YieldProcessor();

        // ---- stats ----
        ++frames;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            fpsSmooth = (float)(frames / fpsTimer);
            std::printf("\r%5.1f fps  storm %4.2f  wet %4.2f  vram %4.0f/%4.0f MB  pos(%5.1f %5.1f %5.1f)   ",
                        fpsSmooth, storm, wetness, renderer.vramUsedMB, renderer.vramBudgetMB,
                        player.pos.x, player.pos.y, player.pos.z);
            std::fflush(stdout);
            frames = 0;
            fpsTimer = 0.0;
        }
    }

    audio.shutdown();
    renderer.shutdown();
    platformShutdown(pf);
    return 0;
}
