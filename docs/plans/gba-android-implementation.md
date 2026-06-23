# Plan: GBA Android Implementation

**Date:** 2026-06-23 (updated 2026-06-23)
**Status:** 🟡 In Progress
**PR:** —

## Overview

Port GBA emulation features from Linux SDL to Android. GBA core (mGBA) + EmuCore layer
shared cross-platform — focus is Android-specific integration: build, audio, touch, file handling.

---

## Priority Matrix

| P | Area | Why |
|---|------|-----|
| P0 | Build system | Tanpa build = zero progress |
| P0 | Audio | Tanpa audio = rusak |
| P1 | Touch controls | Android no keyboard |
| P1 | File handling | Cara buka ROM di Android |
| P2 | UI navigation | Akses settings screen |
| P2 | Video verify | Render mungkin beda GLES |
| P3 | Polish | Edge cases, perf |

---

## Task List

### P0 — Build System

- [x] ~~**P0-1: Verify Gradle→CMake cross-compile mGBA**~~
  - **BLOCKED** — cannot build inside proot distro (no Android NDK)
  - Need `ext/libmgba` to compile for `arm64-v8a`, `armeabi-v7a`, `x86_64`
  - mGBA CMakeLists.txt may need Android toolchain flags
  - **Files:** `build.gradle.kts`, `CMakeLists.txt`, `EmuCore/CMakeLists.txt`, `ext/libmgba/CMakeLists.txt`
  - **Risk:** mGBA uses POSIX threads + VFS — Android NDK may need `-DCMAKE_C_FLAGS` or `-D_POSIX_C_SOURCE`
  - **⛔ BLOCKED by:** No Android NDK/build environment available

- [x] **P0-2: Add EmuCore sources to Android build** ✅
  - `jni/Android.mk` updated with: `EmuCore/GBACore.cpp`, `EmuCore/EmuCore.cpp`, `EmuCore/PSPCore.cpp`, `EmuCore/Config.cpp`, `EmuCore/RecentFilesRegistry.cpp`
  - **Files:** `jni/Android.mk`

- [x] **P0-3: Add GBA UI sources to Android build** ✅
  - `jni/Android.mk` updated with: `UI/GBASettingsScreen.cpp`, `UI/TouchLayoutGBA.cpp`, `UI/CoreTouchLayoutScreen.cpp`
  - **Files:** `jni/Android.mk`

- [x] **P0-4: Define PPSSPP_MULTICORE for ndk-build path** ✅
  - Added `-DPPSSPP_MULTICORE` + `ext/libmgba/include` to `jni/Locals.mk`
  - **Files:** `jni/Locals.mk`

**Definition of Done:** GBA ROM loads on Android device without build error.

---

### P0 — Audio

- [x] **P0-5: Route GBA audio to Android output** ✅
  - Implemented conditional audio path in `UI/EmuScreen.cpp::UpdateGBA()`:
    - Desktop (`!MOBILE_DEVICE`): direct SDL push via `SDL_AudioStream` (existing)
    - Android (`MOBILE_DEVICE`): int16→int32 conversion → `System_AudioPushSamples()` via PPSSPP mixer
  - **Approach A** (mixer path) — zero new dependencies, existing OpenSL/AAudio picks it up
  - **Files:** `UI/EmuScreen.cpp`

- [ ] **P0-6: Verify audio sync / no crackle**
  - Android audio callback timing differs from SDL
  - May need buffer size adjustment in GBACore
  - **Files:** `EmuCore/GBACore.cpp` (AUDIO_BUF_SIZE, TARGET_PAIRS)
  - **⛔ Needs device test**

**Definition of Done:** GBA game audio audible, no crackle/underrun.

---

### P1 — Touch Controls

- [x] **P1-1: Migrate touch layout to CoreTouchConfig system** ✅
  - Migrated from `TouchLayoutGBA::GetLayout()` → centralized `CoreTouchConfig`
  - New `EmuCore::Config.h/.cpp`: `CoreTouchButton`, `CoreTouchConfig` structs
  - `AddGBATouchButtons()` now reads from `EmuCore::GetTouchConfig(coreType_, portrait)`
  - Default GBA layout defined in `FillDefaultGBATouchLayout()`
  - Supports landscape + portrait orientations
  - Per-core INI sections: `[GBA ControlLayout]`, `[GBA ControlLayoutPortrait]`
  - **Files:** `EmuCore/Config.h`, `EmuCore/Config.cpp`, `UI/EmuScreen.cpp`
  - **⚠️ Needs density verification on real Android device**

- [ ] **P1-2: Test VIRTKEY_GBA_* touch binding**
  - Android on-screen buttons map to VIRTKEY_GBA_* via KeyMap system
  - Verify button press → GBA key event
  - **Files:** `Core/KeyMap.cpp`, `UI/EmuScreen.cpp`
  - **⛔ Needs device test**

**Definition of Done:** GBA touch controls visible, responsive, correct mapping.

---

### P1 — File Handling

- [x] **P1-3: Add .gba/.gb/.gbc intent filter to AndroidManifest.xml** ✅
  - Added intent patterns for `.gba`, `.GBA`, `.gb`, `.GB`, `.gbc`, `.GBC`
  - Triple path depth (single, double, triple extension)
  - **Files:** `android/AndroidManifest.xml`

- [x] **P1-4: Handle GBA intent in PpssppActivity.java** ✅
  - **No change needed** — existing shared intent handler already routes to EmuScreen
  - `DetectType()` in `EmuCore/EmuCore.cpp` already maps `.gba` → `Type::GBA`
  - **Files:** none needed

- [ ] **P1-5: Verify GBA ROM browser in MainScreen**
  - GameBrowser extension filter already includes `.gba/.gb` (C++ side)
  - Android file picker/directory browser should show them
  - **Files:** `UI/GameBrowser.cpp` (verify), `UI/MainScreen.cpp`
  - **⛔ Needs device test**

**Definition of Done:** User can open .gba file from file manager OR from in-app ROM browser.

---

### P2 — UI Navigation

- [x] **P2-1: Verify GBASettingsScreen accessible on Android** ✅
  - Added "Customize On-Screen Controls" button to `GBASettingsScreen` (tab Controls)
  - Opens new `CoreTouchLayoutScreen(gamePath, Type::GBA)`
  - Source files added to `Android.mk` so they compile on Android
  - **Files:** `UI/GBASettingsScreen.cpp`, `UI/CoreTouchLayoutScreen.h/.cpp`
  - **⚠️ Needs visual verify on Android device**

- [ ] **P2-2: Verify pause menu save/load for GBA**
  - PauseScreen.cpp already routes `IsGBA()` → `GBACore::SaveStateToFile()`
  - Verify on Android (UI framework shared)
  - **Files:** `UI/PauseScreen.cpp`
  - **⛔ Needs device test**

**Definition of Done:** Settings + pause menu functional on Android.

---

### P2 — Video Verify

- [ ] **P2-3: Verify GBA rendering on GLES backend**
  - Thin3D shared across platforms, but GLES may differ from desktop GL
  - Check texture format, shader preset availability
  - **Files:** `EmuCore/GBACore.cpp` (InitRendering, Render)

- [ ] **P2-4: Verify GBA rendering on Vulkan backend**
  - Android may use Vulkan on supported devices
  - Check pipeline creation, buffer upload
  - **Files:** `EmuCore/GBACore.cpp`

**Definition of Done:** GBA video renders correctly on both GLES and Vulkan.

---

### P3 — Polish

- [ ] **P3-1: GBA performance tuning on Android**
  - mGBA frame timing vs Android vsync
  - Audio buffer tuning for Android latency
  - **Files:** `EmuCore/GBACore.cpp`

- [ ] **P3-2: Handle device rotation / lifecycle**
  - Android activity recreation on rotate
  - GBA state preservation (save/restore)
  - **Files:** `android/src/org/ppsspp/ppsspp/` Java files

- [ ] **P3-3: Edge case — GBA ROM + PSP save state conflict**
  - Ensure PSP save slots not polluted by GBA saves
  - Should already work (different directory/prefix)
  - **Files:** Verifikasi `PauseScreen.cpp`, `EmuScreen.cpp`

- [ ] **P3-4: Edge case — low memory devices**
  - mGBA state size ~388KB — negligible
  - Audio buffer size tuning for low-end
  - **Files:** `EmuCore/GBACore.cpp`

---

## Session Notes

### Claude Code Session (2026-06-23)

Session terpaksa berhenti karena **API Error 402 Insufficient Balance**.

**Completed in session:**
- Per-core touch config system (`EmuCore/Config.h/.cpp`)
- `CoreTouchLayoutScreen` editor screen
- Android intent filter `.gba/.gb/.gbc`
- Android audio bridge (mixer path)
- `Android.mk` + `Locals.mk` updates
- `EmuCore/EmuCore.h` → `Type::COUNT` sentinel

**Pending verification:** Semua yang bertanda needs device test tidak bisa diverifikasi
tanpa build + install ke Android device.

---

## Architecture Decision Records

### ADR-1: Android audio path

**Context:** Linux SDL uses `GetRawAudio()` → direct SDL push. Android has no SDL audio.

**Decision:** Start with PPSSPP mixer path (`int16→int32 → System_AudioPushSamples`).
This adds minimal new code — GBA audio flows through same mixer as PSP audio.
If latency is too high, implement direct OpenSL/AAudio path later.

### ADR-2: Legacy ndk-build vs Gradle→CMake

**Context:** Two build paths exist. ndk-build (`jni/Android.mk`) is outdated.

**Decision:** Focus on Gradle→CMake path. Only update `jni/Android.mk` if CMake path
fails for some device. The CMake path is the future — it already handles mGBA + EmuCore.

### ADR-3: Per-Core Touch Config

**Context:** `TouchLayoutGBA::GetLayout()` was hardcoded per-core. Adding new core = copy-paste.

**Decision:** Centralized `CoreTouchConfig` system di `EmuCore::Config.h/.cpp`:
- Array of `CoreTouchConfig` indexed by `EmuCore::Type` enum
- INI-based load/save per core: `[GBA ControlLayout]`, `[PSP ControlLayout]`
- Default layout via `FillDefault<Core>TouchLayout()`
- Editor via `CoreTouchLayoutScreen` (generic, accepts `Type` parameter)
- PSP existing system (`[ControlLayout]`) TIDAK disentuh — zero break

---

## File Change Summary

| File | Change |
|------|--------|
| `EmuCore/Config.h` | **(NEW STRUCT)** CoreTouchButton, CoreTouchConfig, GetTouchConfig, LoadTouchConfig, SaveTouchConfig |
| `EmuCore/Config.cpp` | **(NEW)** CoreTouchConfig load/save via INI, FillDefaultGBATouchLayout, InitDefaultTouchConfigs |
| `EmuCore/EmuCore.h` | + `Type::COUNT` sentinel |
| `UI/CoreTouchLayoutScreen.h` | **(NEW FILE)** Per-core touch editor class |
| `UI/CoreTouchLayoutScreen.cpp` | **(NEW FILE)** Full editor implementation |
| `UI/EmuScreen.cpp` | Migrated AddGBATouchButtons → CoreTouchConfig; Added Android audio bridge |
| `UI/GBASettingsScreen.cpp` | + "Customize On-Screen Controls" button → CoreTouchLayoutScreen |
| `android/AndroidManifest.xml` | + intent filter `.gba/.GB/.gb/.GB/.gbc/.GBC` |
| `android/jni/Android.mk` | + EmuCore sources + GBA UI sources + CoreTouchLayoutScreen |
| `android/jni/Locals.mk` | + `-DPPSSPP_MULTICORE` + `ext/libmgba/include` |

---

## Progress Tracking

```
P0: Build    [███▁] 3/4  🟡  P0-1 BLOCKED (no NDK env)
P0: Audio    [█▁] 1/2     🟡  code done, needs device test
P1: Touch    [█▁] 1/2     🟡  migrated to CoreTouchConfig, needs device test
P1: File     [██▁] 2/3    🟡  intent + java done, needs ROM browser verify
P2: UI       [█▁] 1/2     🟡  settings screen updated, pause menu needs verify
P2: Video    [▁▁] 0/2     🔴  needs device
P3: Polish   [▁▁▁▁] 0/4   🔴  needs device
─────────────────────────
Code tasks: 9/19 ✅  |  Needs device verify: 8  |  Blocked: 1  |  ❌: 2
```

## Blockers

| ID | Blocker | Impact |
|----|---------|--------|
| P0-1 | No Android NDK (proot distro) | Cannot build APK, cannot test ANY task on device |
| — | 8 tasks marked "needs device test" | All verification blocked until build + install possible |
