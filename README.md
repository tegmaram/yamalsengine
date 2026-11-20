# horror

A small custom C++/Vulkan engine baseplate for a first-person horror game.

- **API:** Vulkan 1.4 (dynamic rendering, no render passes)
- **Toolchain:** MSVC 14.5x (VS 2026) + CMake + Ninja
- **Shaders:** GLSL 460 compiled with `glslc` from the Vulkan SDK
- **No third-party runtime deps** (raw Vulkan loader, hand-written math, hand-written Win32 platform layer)

## Status

Phase 1 (baseplate):

- [x] Win32 window, raw mouse input, mouse capture
- [x] Vulkan swapchain, depth buffer, 2 frames in flight, resize, mailbox present
- [x] 180 fps frame limiter (sleep + spin, 1 ms timer resolution)
- [x] Procedural sky + limb-darkened sun disk
- [x] **2D cloud deck**: one baked 1024² RGBA sheet, 3 parallax layers, ~5 taps/pixel, no raymarch
- [x] Baked cloud shadow channel (sun is fixed, so it is precomputed in the sheet)
- [x] Auto-generated maze (recursive backtracker + loops), capsule collision
- [x] WASD + sprint, crouch with eye-height blend, camera lantern
- [x] Hold **R**: storm transition (coverage, darkness, rain, wet floor, thunder)
- [x] Procedural thunder (no audio assets, no rain bed)

Measured on RX 7800 XT, 1600x900: **180 fps capped, 110 MB working set, 77 MB VRAM**.

Phase 2: Hillaire atmosphere LUT, flashlight shadow map, post FX, TAA/FSR, real textures.

## Build

```powershell
.\setup.ps1            # once, elevated: installs toolchain
.\build.ps1 -Run       # debug build + run
.\build.ps1 -Config release -Run
```

`build.ps1` enters the Visual Studio environment via `VsDevCmd` itself, so you do not
need a Developer PowerShell.

## Controls

| Key | Action |
|---|---|
| W A S D | walk |
| Shift | sprint |
| C / Ctrl | crouch (with headroom-aware stand up) |
| F1 | release / capture mouse |
| Esc | quit |

## Layout

```
src/
  main.cpp           entry point, world greybox, main loop
  platform_win32.*   window, raw input, timing
  player.*           kinematic capsule controller + crouch
  noise.*            tileable 3D Perlin-Worley cloud volume
  vk_engine.*        Vulkan renderer
shaders/
  sky.vert/frag      fullscreen sky + volumetric clouds + sun disk
  mesh.vert/frag     greybox geometry, cloud shadows, fog
```

## Tuning knobs (top of `src/main.cpp` and shaders)

| Thing | Where |
|---|---|
| Sun direction / elevation | `sunDir` in `main.cpp` |
| Cloud coverage | `sky.coverage` (0 = clear, 1 = overcast) |
| Exposure | `sky.exposure` |
| Cloud altitude / density | `CLOUD_BASE`, `CLOUD_TOP`, density remap in `sky.frag` |
| Cloud march quality | `params.w` (64 steps in `vk_engine.cpp`) |
| Walk / crouch speeds | `Player` fields in `player.h` |

## Assets

Nothing is bundled yet. When we add textures, the plan is CC0 sources:
Poly Haven, ambientCG, cgbookcase (see `CREDITS.md`).
