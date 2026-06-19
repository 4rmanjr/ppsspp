# GBA Support — Progress Report

**Date:** 2026-06-19 (updated)
**Branch:** `feature/lan-sync`
**Build:** `PPSSPP_MULTICORE=ON` (feature flag)

## 🔨 Currently Working On

- 🔴 **Audio fix**: still distorted/slow-mo — need to investigate PPSSPP audio backend integration
- 🔴 **ESC pause menu**: direct handler added but not yet responding — need debug
- 🔴 **Save state (F1/F3)**: code in place but not working — `DoGBAState()` may have path or data issue
- 🔴 **Load state**: same as save state — needs end-to-end debug

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
- [x] `InitGBARendering()` — thin3d pipeline + texture (lazy init)
- [x] `DrawGBAVideo()` — RGBA8888 texture upload + fullscreen quad with 3:2 aspect
- [x] `VS_TEXTURE_COLOR_2D` + `FS_TEXTURE_COLOR_2D` shader presets
- [x] GBA render path BEFORE `PSP_IsInited()` check
- [x] Cleanup in destructor + deviceLost/deviceRestored
- [x] ✅ **VISUALLY WORKING** — Breath of Fire renders correctly

### 6. Audio Routing
- [x] int16 → int32 conversion (shift left 16 bits)
- [x] Fixed sample rate: 1470 mono samples/frame = 735 stereo pairs (44100Hz @ 60fps)
- [x] `System_AudioPushSamples()` called with stereo pair count (not mono count)
- [x] Silence padding when mGBA produces fewer samples
- [x] ⚠️ **Audio plays but sounds "slow-mo"** — sample rate mismatch suspected

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
- [x] `DoGBAState()` — raw buffer I/O
- [x] Files: `PSP/PPSSPP_STATE/GBA/<prefix>_N.gbast`
- [x] Reuses F1-F4 hotkeys + same slot config
- [x] No new UI needed

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

## 🟡 In Progress / Needs Verification

| Item | Status | Notes |
|------|--------|-------|
| **Audio clarity** | 🟡 Slow-mo | Fixed sample count (735 stereo pairs), but still distorted |
| **ESC pause menu** | 🟡 Unverified | Direct handler added, needs user test |
| **Save state end-to-end** | 🟡 Untested | Code in place, needs ROM test |
| **Save memory end-to-end** | 🟡 Untested | Code in place, needs ROM test |

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

## 🐛 Bugs Fixed This Session

| # | Bug | Root Cause | Fix |
|---|-----|------------|-----|
| 1 | Segfault on boot | Linker stripped mGBA function pointers | `--whole-archive mgba --no-whole-archive` |
| 2 | Crash in `HashTableLookup` | `mCoreConfig` hash table uninitialized | `mCoreConfigInit(&core_->config, "gba")` |
| 3 | Video all black | `getPixels` returns NULL without `setVideoBuffer` | `setVideoBuffer(rawVideoBuffer_, 240)` |
| 4 | Crash creating texture | `texDesc.depth=0`, `initData=nullptr` | `depth=1`, valid zeroed buffer |
| 5 | GBA video never drawn | `!PSP_IsInited()` caught GBA before render path | GBA check moved above PSP check |
| 6 | Audio noise/static | Pushing too many samples (overflow) | Cap at 1470 samples/frame |
| 7 | Audio slow-mo | `numSamples` passed as mono count instead of stereo pairs | `SAMPLES_PER_FRAME / 2` |
| 8 | ESC not working | GBA `update()` returned before pause check | Direct ESC handler + pause check in GBA path |
| 9 | `Unexpected PSP_Shutdown` | Destructor called `PSP_Shutdown` for GBA | Guard with `coreType_ == PSP` check |
| 10 | Config mixing PSP/GBA | No config isolation | SaveCurrentConfig / RestoreSavedConfig + [GBA] INI section |

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
