# Plan: GBA D-Pad Spacing Consistency with PSP

## Problem
GBA D-Pad spacing changes between landscape and portrait orientations because spacing is calculated from button positions in `AddCoreTouchButtons()`:

```cpp
float spacing = ((dpRight->x - dpLeft->x) * 0.5f * bounds.w) / (D_pad_Radius * bgScale);
```

- Landscape: `dpRight.x - dpLeft.x = 0.08` → spacing ≈ 0.96
- Portrait: `dpRight.x - dpLeft.x = 0.09` → spacing ≈ 1.08

PSP uses `config.fDpadSpacing` (default 1.0f) which is consistent and persisted.

## Root Cause
- PSP: Spacing from `TouchControlConfig.fDpadSpacing` (default 1.0f, saved per-game)
- GBA: Spacing calculated dynamically from button positions (not persisted, inconsistent)

## Decision: Option 3 — Use dpadSpacing from CoreTouchConfig

`CoreTouchConfig` already has `dpadSpacing` field (saved/loaded in `EmuCore/Config.cpp`) but it's not used in `AddCoreTouchButtons()`.

## Implementation Steps

1. **UI/EmuScreen.cpp:1394** — Replace spacing calculation with `cfg.dpadSpacing` (fallback to 1.0f)
2. **UI/EmuScreen.cpp:1381-1383** — Calculate `cx, cy` from button positions (keep current)
3. **EmuCore/Config.cpp:17-27** — Ensure `dpadSpacing` initialized properly (default 0.8f from CoreDPadGroup)
4. Build with both MULTICORE=ON/OFF
5. Test rotation on device

## PSP Reference
- `UI/GamepadEmu.cpp:1075` — `PSPDpad` created with `config.fDpadSpacing`
- `Core/Config.cpp:859` — Default `fDpadSpacing = 1.0f`
- `CoreDPadGroup` in `CoreTouchLayoutScreen.cpp:225` — Default spacing 0.8f (normalized)

## Validation
- [ ] Build succeeds MULTICORE=ON
- [ ] Build succeeds MULTICORE=OFF
- [ ] Spacing visually consistent between landscape/portrait
- [ ] No upstream code modified (zero deletion)