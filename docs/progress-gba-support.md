# GBA Support — Progress Report

**Date:** 2026-06-18
**Branch:** `feature/lan-sync`
**Build:** `PPSSPP_MULTICORE=ON` (feature flag), 28-core parallel build

---

## ✅ Completed

### 1. Multi-Emulator Architecture (EmuCore)
- [x] Abstract `EmuCore::Core` interface — `EmuCore/EmuCore.h`
- [x] Factory `EmuCore::Create()` + `EmuCore::DetectType()` — `EmuCore/EmuCore.cpp`
- [x] PSPCore wrapper (delegates to existing system) — `EmuCore/PSPCore.h/.cpp`
- [x] GBACore via libmgba — `EmuCore/GBACore.h/.cpp`
- [x] Per-core config isolation — `EmuCore/Config.h/.cpp`

### 2. mGBA Integration
- [x] Submodule `ext/libmgba` (commit `d5e9fae`)
- [x] Static library `libmgba.a` (LIBMGBA_ONLY, no frontends)
- [x] GBA core: video (240x160 RGBA8888 buffer, XBGR→RGBA conversion)
- [x] GBA core: audio (44100Hz stereo, mAudioBuffer → int16 samples)
- [x] GBA core: input (setKeys/getKeys)
- [x] GBA core: savestate (stateSize/saveState/loadState)
- [x] GBA core: game info (title, code)

### 3. File Detection & Routing
- [x] `.gba/.gb/.gbc` added to game file filter — `UI/GameBrowser.cpp`
- [x] Auto-detect file type in `LaunchFile` — `UI/MainScreen.cpp`
- [x] EmuScreen multi-core aware — constructor + update() + render()

### 4. CMake Build System
- [x] `EmuCore/CMakeLists.txt` — EmuCore static library + libmgba build
- [x] Root `CMakeLists.txt` — `PPSSPP_MULTICORE` feature flag
- [x] Dual build verified: `ON` (28MB PPSSPPSDL) and `OFF` (zero upstream impact)
- [x] `test_gba_core` CMake target (test GBA core directly)

### 5. Input Mapping (PSP → GBA)
- [x] `GBACore::PSPSKeysToGBA()` — static converter
- [x] Cross → A, Circle → B, Triangle → A (alt), Square → B (alt)
- [x] Start/Select/L/R/D-Pad mapped directly
- [x] `SetKeys()` transparently converts PSP bitmask → GBA bitmask

### 6. GBA Touch Layout
- [x] `UI/TouchLayoutGBA.h/.cpp` — button positions for landscape + portrait
- [x] `EmuScreen::AddGBATouchButtons()` — creates PSPButton instances at GBA positions
- [x] GBA mode uses separate AnchorLayout (no PSP buttons visible)
- [x] A/B/Start/Select/L/R/D-Pad touch controls

---

## 🔄 Pending

### 7. Audio Routing
- **Current state:** GBA produces audio samples (int16, 44100Hz stereo), TODO code is in `EmuScreen::update()` but commented out
- **Needed:** Convert int16 → int32, then `System_AudioPushSamples(int32, numSamples, volume)`
- **File:** `UI/EmuScreen.cpp` (~line 1515)

### 8. GBA Video Rendering
- **Current state:** GBACore produces `videoBuffer_[240*160]` RGBA8888 pixels
- **Needed:** Upload buffer as OpenGL texture and display in EmuScreen::renderUI()
- **File:** `UI/EmuScreen.cpp` (~line 2112, already has GBA render stub)

### 9. Test Verification
- [x] `test_gba_core` binary built (2.4MB)
- [ ] Need a GBA ROM to run actual test
- [ ] Verify input mapping works end-to-end
- [ ] Verify savestate serialization

---

## 📊 Build Artifacts

| Target | Size | Status |
|--------|------|--------|
| `PPSSPPSDL` (MULTICORE=ON) | 28MB | ✅ |
| `PPSSPPSDL` (MULTICORE=OFF) | ~28MB | ✅ zero impact |
| `test_gba_core` | 2.4MB | ✅ |
| `libmgba.a` | ~1MB | ✅ |
| `libEmuCore.a` | ~100KB | ✅ |

---

## 🔧 How to Use

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_FFMPEG=ON
cmake --build build -j$(nproc)

# Run GBA ROM
./build/PPSSPPSDL /path/to/game.gba

# Or test core directly
./build/test_gba_core /path/to/game.gba
```
