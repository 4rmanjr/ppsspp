# GBA Support — Progress Report

**Date:** 2026-06-20 (updated — P1-P3 refactor)
**Branch:** `feature/lan-sync`
**Build:** `PPSSPP_MULTICORE=ON` (feature flag)

> ⚠️ **Progress doc ini STALE sebelum P1-P3 refactor (`fa08833c01`).**
> Berikut adalah status real berdasarkan verifikasi kode (static analysis),
> **belum runtime-test** karena keterbatasan lingkungan (proot, tanpa GPU).

## 🔨 Perlu Runtime Test (di PC sungguhan)

Keempat item di bawah sudah diimplementasi ulang di P1-P3 refactor,
**tapi belum pernah di-run** — status real tidak diketahui:

- 🟡 **Audio clarity** — `GetMixedAudio()`: resample 32768→44100 Hz via linear interpolation, zero silence padding. **Kode sudah fix, perlu test suara.**
- 🟡 **ESC pause menu** — Direct handler di `UnsyncKey()` → `pauseTrigger_` → `UpdateGBA()` push `GamePauseScreen`. **Rantai handler lengkap, perlu test apakah pause muncul.**
- 🟡 **Save state (F1/F3)** — `GBACore::SaveStateToFile(slot)`, file I/O proper, OSD message. **Kode baru total, perlu test.**
- 🟡 **Load state** — `GBACore::LoadStateFromFile(slot)`. **Sama, perlu test.**

---

## ✅ Completed

### 1. Multi-Emulator Architecture (EmuCore)
- [x] Abstract `EmuCore::Core` interface — `EmuCore/EmuCore.h`
- [x] Factory `EmuCore::Create()` + `EmuCore::DetectType()` — `EmuCore/EmuCore.cpp`
- [x] PSPCore wrapper — `EmuCore/PSPCore.h/.cpp`
- [x] GBACore via libmgba — `EmuCore/GBACore.h/.cpp`
- [x] Per-core config isolation — `EmuCore/Config.h/.cpp`

### 2. mGBA Integration
- [x] Submodule `ext/libmgba` (commit `d5e9fae`)
- [x] Static library `libmgba.a` (LIBMGBA_ONLY, --whole-archive to prevent linker stripping)
- [x] `mCoreConfigInit()` called to initialize mGBA config hash table
- [x] `setVideoBuffer()` called to enable mGBA video rendering into our buffer
- [x] Video: XBGR8 → RGBA8888 conversion per frame
- [x] Audio: 44100Hz stereo via mAudioBuffer → int16
- [x] Input: PSP→GBA key mapping via `PSPSKeysToGBA()`
- [x] Savestate: raw buffer via `stateSize/saveState/loadState`
- [x] Save memory: `mDirectorySetInit`, `mCoreAutoloadSave`, auto-flush on destruct

### 3. File Detection & Routing
- [x] `.gba/.gb/.gbc` in game file filter — `UI/GameBrowser.cpp`
- [x] Auto-detect file type in `LaunchFile` — `UI/MainScreen.cpp`
- [x] NativeApp auto-boot GBA detection
- [x] EmuScreen multi-core aware — constructor + update() + render()

### 4. CMake Build System
- [x] `EmuCore/CMakeLists.txt` — EmuCore + libmgba build
- [x] Root `CMakeLists.txt` — `PPSSPP_MULTICORE` feature flag
- [x] `--whole-archive` for mgba to prevent linker stripping
- [x] Dual build verified ON/OFF
- [x] `test_gba_core` CMake target

### 5. Video Rendering
- [x] `InitRendering()` + `Render(DrawContext*)` — thin3d pipeline + texture (lazy init), **pindah ke GBACore** (P1 refactor)
- [x] RGBA8888 texture upload + fullscreen quad with 3:2 aspect
- [x] `VS_TEXTURE_COLOR_2D` + `FS_TEXTURE_COLOR_2D` shader presets
- [x] GBA render path BEFORE `PSP_IsInited()` check
- [x] Cleanup via `DeviceLost()`/`DeviceRestored()` — lazy reinit
- [x] ✅ **VISUALLY WORKING** — Breath of Fire renders correctly

### 6. Audio Routing
- [x] **Root cause slow-mo identified**: mGBA native 32768 Hz ≠ PPSSPP mixer 44100 Hz
- [x] `GetMixedAudio(int32_t*, size_t*)` — **resample 32768→44100 Hz** via linear interpolation (P1 refactor)
- [x] ~546 native stereo pairs/frame → 735 target pairs @ 44100 Hz 60fps
- [x] int16 → int32 conversion (shift left 16 bits) **setelah resample**
- [x] **Zero silence padding** (sebelumnya 25.7% silence per frame)
- [x] Audio conversion **pindah dari EmuScreen ke GBACore**
- [x] `System_AudioPushSamples()` called with stereo pairs count
- [x] ⚠️ **Kode fix sudah diimplementasi, tapi belum runtime-test**

### 7. Input Mapping
- [x] `PSPSKeysToGBA()` — Cross→A, Circle→B, D-Pad, L/R, Start/Select
- [x] Keyboard works via existing PPSSPP ControlMapper
- [x] ESC direct handler in UnsyncKey for GBA mode
- [x] Pause trigger check in GBA update path
- [x] ✅ **Game buttons working** (A, S, Z, X, Space, arrows)
- [x] ⚠️ **ESC pause menu not yet verified working**

### 8. Save Memory (SRAM/Flash/EEPROM)
- [x] `mDirectorySetInit` + `SetSaveDirectory` + `mCoreAutoloadSave`
- [x] Auto-flush on destruct via `mDirectorySetDeinit`
- [x] Files: `PSP/SAVEDATA/GBA/<title>.sav`

### 9. Save State (Snapshot)
- [x] `GBACore::SaveStateToFile(slot)` / `LoadStateFromFile(slot)` — **pindah dari EmuScreen ke GBACore** (P3 refactor)
- [x] `GBACore::GetSavePrefix()` — sanitasi game title (alphanumeric + underscore, max 32 chars)
- [x] Files: `PSP/PPSSPP_STATE/GBA/<prefix>_N.gbast`
- [x] Reuses F1-F4 hotkeys + same slot config, OSD success/fail messages
- [x] EmuScreen VIRTKEY handler call GBACore langsung + OSD messages
- [x] No new UI needed
- [x] ⚠️ **Kode baru total, belum runtime-test**

### 10. Touch Layout
- [x] `UI/TouchLayoutGBA.h/.cpp` — GBA button positions
- [x] A/B/Start/Select/L/R/D-Pad

### 11. Config Isolation
- [x] `SaveCurrentConfig()` — snapshot PSP settings to memory
- [x] `LoadGBAOverrides()` — read `[GBA]` section from `ppsspp.ini`
- [x] `SaveGBAOverrides()` — write `[GBA]` section on exit
- [x] `RestoreSavedConfig()` — restore PSP settings on exit
- [x] GBA defaults: nearest filter, auto resolution
- [x] ✅ **Config isolation working** — PSP and GBA settings don't mix

### 12. Debug Logging
- [x] Boot sequence logging (`[BOOT]` prefix)
- [x] GBA core lifecycle (`[GBA]` prefix)
- [x] Video render debug (first pixel, non-black check)
- [x] Audio stats (samples per frame)
- [x] Input state logging
- [x] Config save/restore logging (`[CONFIG]` prefix)
- [x] All mGBA internal logs (DMA, BIOS SWI) via PPSSPP log system

---

## 🟡 Perlu Runtime Test

| Item | Kode | Runtime | Prioritas |
|------|------|---------|-----------|
| **Audio clarity** | `GetMixedAudio()` resample 32768→44100 Hz ✅ | ❌ Belum di-test | 🔴 Tinggi |
| **ESC pause menu** | UnsyncKey → pauseTrigger → UpdateGBA chain ✅ | ❌ Belum di-test | 🟡 Sedang |
| **Save state** | `SaveStateToFile()` file I/O proper ✅ | ❌ Belum di-test | 🟡 Sedang |
| **Load state** | `LoadStateFromFile()` file I/O proper ✅ | ❌ Belum di-test | 🟡 Sedang |
| **Save memory (SRAM)** | Auto-load via `mCoreAutoloadSave` ✅ | ✅ Sudah jalan (sejak awal) | ❌ |

---

## 🧹 P2: #ifdef Cleanup (tidak tercatat di progress doc sebelumnya)

- [x] 31 dari ~45 `#ifdef PPSSPP_MULTICORE` di call site **dihilangkan**
- [x] Pattern: `if (IsGBA())` bukan `#ifdef + if (coreType_ != PSP)`
- [x] Helper methods: `InitGBA()`, `ShutdownGBA()`, `UpdateGBA()`, `RenderGBA()`
- [x] `IsGBA()` constexpr → `static constexpr bool IsGBA() { return false; }` saat MULTICORE=OFF

---

## 📊 Build Artifacts

| Target | Size | Status |
|--------|------|--------|
| `PPSSPPSDL` (MULTICORE=ON) | 26.7MB | ✅ |
| `PPSSPPSDL` (MULTICORE=OFF) | 25.5MB | ✅ zero impact |
| `test_gba_core` | 1.6MB | ✅ ALL TESTS PASSED |
| `libmgba.a` | 1.8MB | ✅ |
| `libEmuCore.a` | ~25KB | ✅ |

---

## 🐛 Bugs Fixed (Semua Sesi)

| # | Bug | Root Cause | Fix | Sesi |
|---|-----|------------|-----|------|
| 1 | Segfault on boot | Linker stripped mGBA function pointers | `--whole-archive mgba --no-whole-archive` | Sebelum P1 |
| 2 | Crash in `HashTableLookup` | `mCoreConfig` hash table uninitialized | `mCoreConfigInit(&core_->config, "gba")` | Sebelum P1 |
| 3 | Video all black | `getPixels` returns NULL without `setVideoBuffer` | `setVideoBuffer(rawVideoBuffer_, 240)` | Sebelum P1 |
| 4 | Crash creating texture | `texDesc.depth=0`, `initData=nullptr` | `depth=1`, valid zeroed buffer | Sebelum P1 |
| 5 | GBA video never drawn | `!PSP_IsInited()` caught GBA before render path | GBA check moved above PSP check | Sebelum P1 |
| 6 | Audio noise/static | Pushing too many samples (overflow) | Cap at 1470 samples/frame | Sebelum P1 |
| 7 | Audio slow-mo (early) | `numSamples` passed as mono count instead of stereo pairs | `SAMPLES_PER_FRAME / 2` | Sebelum P1 |
| 8 | ESC not working | GBA `update()` returned before pause check | Direct ESC handler + pause check in GBA path | Sebelum P1 |
| 9 | `Unexpected PSP_Shutdown` | Destructor called `PSP_Shutdown` for GBA | Guard with `coreType_ == PSP` check | Sebelum P1 |
| 10 | Config mixing PSP/GBA | No config isolation | SaveCurrentConfig / RestoreSavedConfig + [GBA] INI section | Sebelum P1 |
| --- | --- | --- | --- | --- |
| **11** | **Audio slow-mo root cause** | mGBA native 32768 Hz ≠ PPSSPP 44100 Hz, silence padding 25.7% | `GetMixedAudio()` resample linear + zero silence padding | **P1** |
| **12** | **Code duplication render** | Thin3D pipeline ada di EmuScreen, bukan di core | Pindah ke `GBACore::Render(DrawContext*)` | **P1** |
| **13** | **Code duplication audio** | Audio conversion ada di EmuScreen | Pindah ke `GBACore::GetMixedAudio()` | **P1** |
| **14** | **`#ifdef` clutter** | ~45 `#ifdef PPSSPP_MULTICORE` di call site | 31 dihilangkan, pakai `IsGBA()` pattern | **P2** |
| **15** | **Save state di EmuScreen** | Raw buffer I/O + path logic campur di EmuScreen | Pindah ke `SaveStateToFile/LoadStateFromFile` di GBACore | **P3** |

---

## 🔧 How to Build & Run

```bash
# Build (MULTICORE=ON)
cmake -B build-test2 -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-test2 --target PPSSPPSDL -j$(nproc)

# Run GBA ROM
./build-test2/PPSSPPSDL "/path/to/game.gba"

# Test core directly
./build-test2/test_gba_core "/path/to/game.gba"

# Run with GBA-only log
./build-test2/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"

# Run with config log
./build-test2/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[CONFIG\]"
```
