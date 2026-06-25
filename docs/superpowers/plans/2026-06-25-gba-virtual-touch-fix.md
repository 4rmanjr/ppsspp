# GBA Virtual Touch Buttons Fix ✅ SELESAI

> **Implementation complete.** Touch buttons confirmed working on Samsung A05s (2026-06-25).

**Goal:** Make GBA virtual on-screen touch buttons appear and work on Android.

**Root Causes (3 bugs, all fixed):**

| # | Bug | File | Fix | Commit/Hash |
|---|-----|------|-----|-------------|
| 1 | `LoadGBAOverrides()` override `bShowTouchControls=0` dari INI | `EmuCore/Config.cpp:292` | Force-set `g_Config.bShowTouchControls = true` setelah overrides | (early fix) |
| 2 | `EmuScreen::update()` early-return sebelum `UIScreen::update()` → `CreateViews()` tak pernah dipanggil → `root_` null | `UI/EmuScreen.cpp:1863-1868` | Tambah `UIScreen::update()` sebelum `UpdateGBA()` | `ce9c3ea` |
| 3 | `GamepadUpdateOpacity()` tanpa force parameter periksa `coreState == CORE_POWERDOWN` (PSP engine tidak pernah init untuk GBA) → return opacity=0 | `UI/GamepadEmu.cpp:48-53` | Panggil `GamepadUpdateOpacity(g_Config.iTouchButtonOpacity / 100.0f)` di GBA path | (fix follow-up) |

**Auxiliary bug found (portrait mode):** `LoadTouchConfig()` unconditional `cfgP.Clear()` — portrait default layout hilang. Fix: pindahkan `cfgP.Clear()` di dalam `if (secP)` block.

**Total: 3 root causes + 1 auxiliary bug = 4 fixes across 2 files.**

---

### Final Changes

**`UI/EmuScreen.cpp` — GBA `EmuScreen::update()` path:**
```cpp
if (IsGBA() && activeCore_) {
    // For GBA, coreState stays at CORE_POWERDOWN (PSP never inits),
    // so GamepadUpdateOpacity() without force would set opacity=0.
    // Force the configured opacity directly.
    GamepadUpdateOpacity(g_Config.iTouchButtonOpacity / 100.0f);
    UIScreen::update();  // CreateViews() → root_ + touch buttons
    UpdateGBA();
    return;
}
```

**`EmuCore/Config.cpp` — `LoadTouchConfig()`:**
```cpp
// Before (portrait bug):
cfgP.Clear();  // Always clear → portrait defaults lost if no INI section
if (secP) { ... }

// After (fixed):
if (secP) {
    cfgP.Clear();  // Only clear if section exists
    ...
}
```

**`EmuCore/Config.cpp` — `LoadConfig()` (earlier fix):**
```cpp
g_Config.bShowTouchControls = true;  // After LoadGBAOverrides()
```

---

### Remaining Polish ("di rapihkan")

`CreateViews()` GBA path masih nambah DevMenu + Resume buttons di root_ — ini seharusnya hanya muncul saat pause menu. `children=15` (10 touch buttons + 5 pause/utility views).

---

### Build Verification

- [x] Android `goldRelease` APK build + signed + installed via ADB WiFi
- [x] Device: Samsung A05s (SM-A057F)
- [x] Tested with: Pokemon Emerald (GBA ROM)
- [x] Outcome: Touch buttons appear and functional ✅
- [x] Config: `portrait=0 (landscape) configCount=10 children=15 opacity=configured`
