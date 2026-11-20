#pragma once
#include <cstdint>

// Virtual-key codes we need without dragging <windows.h> into gameplay code.
enum : int { KEY_SHIFT = 0x10, KEY_CONTROL = 0x11, KEY_ESCAPE = 0x1B, KEY_F1 = 0x70 };

struct Platform {
    void* hwnd = nullptr;
    void* hinstance = nullptr;
    int width = 1600;
    int height = 900;
    bool quit = false;
    bool captured = false;

    // Keyboard: indexed by VK_* code, e.g. keys['W']
    bool keys[256] = {};
    bool keyPressed[256] = {}; // edge, cleared each poll

    float mouseDX = 0.0f;
    float mouseDY = 0.0f;
    float wheel = 0.0f;
};

bool platformInit(Platform& p, const wchar_t* title, int width, int height);
void platformShutdown(Platform& p);
void platformPoll(Platform& p); // pumps messages, updates input, recenters cursor
void platformSetCapture(Platform& p, bool captured);
double platformTime(); // seconds since start
