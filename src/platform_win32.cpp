#include "platform.h"

#include <windows.h>
#include <windowsx.h>

namespace {
Platform* g_platform = nullptr;
double g_start = 0.0;
bool g_recenter = false;
} // namespace

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Platform* p = g_platform;
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            if (p) p->quit = true;
            return 0;
        case WM_SIZE:
            if (p && wParam != SIZE_MINIMIZED) {
                p->width = LOWORD(lParam);
                p->height = HIWORD(lParam);
            }
            return 0;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            if (p) {
                if (!(lParam & (1 << 30))) p->keyPressed[(int)wParam] = true;
                p->keys[(int)wParam] = true;
            }
            if (wParam == VK_ESCAPE && p) p->quit = true;
            if (wParam == VK_F1 && p) platformSetCapture(*p, !p->captured);
            return 0;
        }
        case WM_SYSKEYUP:
        case WM_KEYUP:
            if (p) p->keys[(int)wParam] = false;
            return 0;
        case WM_KILLFOCUS:
            if (p) {
                for (bool& k : p->keys) k = false;
                platformSetCapture(*p, false);
            }
            return 0;
        case WM_INPUT: {
            if (!p) return 0;
            UINT size = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            if (size == 0 || size > 128) return 0;
            RAWINPUT raw{};
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != size)
                return 0;
            if (raw.header.dwType == RIM_TYPEMOUSE && p->captured) {
                p->mouseDX += (float)raw.data.mouse.lLastX;
                p->mouseDY += (float)raw.data.mouse.lLastY;
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (p) p->wheel += (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            return 0;
        case WM_ACTIVATE:
            if (p && LOWORD(wParam) == WA_INACTIVE) platformSetCapture(*p, false);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool platformInit(Platform& p, const wchar_t* title, int width, int height) {
    g_platform = &p;
    p.width = width;
    p.height = height;
    p.hinstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = (HINSTANCE)p.hinstance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"HorrorEngineWindow";
    if (!RegisterClassExW(&wc)) return false;

    RECT rc{0, 0, width, height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
                                nullptr, (HINSTANCE)p.hinstance, nullptr);
    if (!hwnd) return false;

    p.hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    QueryPerformanceCounter((LARGE_INTEGER*)&g_start);
    return true;
}

void platformShutdown(Platform& p) {
    platformSetCapture(p, false);
    if (p.hwnd) DestroyWindow((HWND)p.hwnd);
    p.hwnd = nullptr;
    g_platform = nullptr;
}

void platformSetCapture(Platform& p, bool captured) {
    HWND hwnd = (HWND)p.hwnd;
    if (!hwnd) return;
    if (captured) {
        SetCapture(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT clip{tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);
        ShowCursor(FALSE);
        g_recenter = true;
    } else {
        ReleaseCapture();
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }
    p.captured = captured;
    p.mouseDX = p.mouseDY = 0.0f;
}

void platformPoll(Platform& p) {
    for (bool& k : p.keyPressed) k = false;
    p.mouseDX = p.mouseDY = 0.0f;
    p.wheel = 0.0f;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (p.captured && g_recenter) {
        RECT rc;
        GetClientRect((HWND)p.hwnd, &rc);
        POINT c{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
        ClientToScreen((HWND)p.hwnd, &c);
        SetCursorPos(c.x, c.y);
        g_recenter = false;
    }
}

double platformTime() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return double(now.QuadPart - (LONGLONG)g_start) / double(freq.QuadPart);
}
