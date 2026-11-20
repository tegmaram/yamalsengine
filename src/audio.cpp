#include "audio.h"

#include <windows.h>
#include <xaudio2.h>

#include <cmath>
#include <vector>
#include <cstdint>

namespace {

constexpr int SR = 44100;

struct Impl {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* thunderVoice = nullptr;
    std::vector<int16_t> thunderBuf;
    WAVEFORMATEX fmt{};
};

Impl* g = nullptr;
uint32_t g_seed = 0x9E3779B9u;

float rnd() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return float((g_seed >> 8) & 0xFFFFFF) / float(0x1000000);
}

// Distant rumble: brown noise, heavy low-pass, slow decay with a low-frequency
// amplitude wobble so it reads as a rolling wave rather than a noise burst.
void genThunder(std::vector<int16_t>& buf) {
    const int n = SR * 5;
    buf.resize(n);
    float lp = 0.0f, brown = 0.0f;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / float(SR);
        float w = rnd() * 2.0f - 1.0f;
        brown = brown * 0.995f + w * 0.02f;
        lp += (brown - lp) * 0.05f;
        float crack = std::exp(-t * 14.0f);
        float rumble = std::exp(-t * 0.75f) * (0.55f + 0.45f * std::sin(t * 2.3f + 0.4f));
        float env = crack * 0.7f + rumble * 0.6f;
        float v = lp * env * 3.4f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        buf[i] = (int16_t)(v * 32767.0f);
    }
    int f = SR / 50;
    for (int i = 0; i < f; ++i) {
        float a = float(i) / float(f);
        buf[i] = (int16_t)(buf[i] * a);
        buf[n - 1 - i] = (int16_t)(buf[n - 1 - i] * a);
    }
}

} // namespace

bool Audio::init() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    auto* im = new Impl();
    if (FAILED(XAudio2Create(&im->engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        delete im;
        return false;
    }
    if (FAILED(im->engine->CreateMasteringVoice(&im->master))) {
        im->engine->Release();
        delete im;
        return false;
    }

    im->fmt.wFormatTag = WAVE_FORMAT_PCM;
    im->fmt.nChannels = 1;
    im->fmt.nSamplesPerSec = SR;
    im->fmt.nAvgBytesPerSec = SR * 2;
    im->fmt.nBlockAlign = 2;
    im->fmt.wBitsPerSample = 16;
    im->fmt.cbSize = 0;

    genThunder(im->thunderBuf);

    if (FAILED(im->engine->CreateSourceVoice(&im->thunderVoice, &im->fmt))) {
        im->engine->Release();
        delete im;
        return false;
    }

    g = im;
    return true;
}

void Audio::shutdown() {
    if (!g) return;
    if (g->thunderVoice) {
        g->thunderVoice->Stop(0);
        g->thunderVoice->DestroyVoice();
    }
    if (g->master) g->master->DestroyVoice();
    if (g->engine) g->engine->Release();
    delete g;
    g = nullptr;
}

void Audio::playThunder(float v) {
    if (!g || !g->thunderVoice) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    g->thunderVoice->Stop(0);
    g->thunderVoice->FlushSourceBuffers();

    XAUDIO2_BUFFER b{};
    b.AudioBytes = (UINT32)(g->thunderBuf.size() * sizeof(int16_t));
    b.pAudioData = (const BYTE*)g->thunderBuf.data();
    b.LoopCount = 0;
    g->thunderVoice->SubmitSourceBuffer(&b);
    g->thunderVoice->SetVolume(0.55f + 0.45f * v);
    g->thunderVoice->Start(0);
}

bool Audio::ok() const { return g != nullptr; }
