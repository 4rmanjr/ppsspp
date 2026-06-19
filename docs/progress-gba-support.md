# GBA Support — Progress Report

**Date:** 2026-06-19
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

### 7. GBA Video Rendering
- [x] thin3d pipeline + texture created via `InitGBARendering()` — `UI/EmuScreen.cpp`
- [x] Lazy init pattern: pipeline created on first frame (DrawContext not available during construction)
- [x] `DrawGBAVideo()` — uploads `videoBuffer_[240*160]` RGBA8888, draws fullscreen with aspect ratio (3:2)
- [x] Uses `VS_TEXTURE_COLOR_2D` + `FS_TEXTURE_COLOR_2D` shader presets (same as UIContext)
- [x] Letterboxed with black background in `render()` before `renderUI()`
- [x] Cleanup in destructor + deviceLost, recreate in deviceRestored
- [x] Build verified: `MULTICORE=ON` (26.6MB) and `MULTICORE=OFF` (25.5MB)

### 8. Audio Routing
- [x] Convert int16 GBA audio → int32 PPSSPP format (shift left 16 bits) — `UI/EmuScreen.cpp` ~line 1738
- [x] `GetAudioSamples()` provides bytes of int16 stereo; converted to int32 then `System_AudioPushSamples()`
- [x] Buffer: 4096 samples (int16), converted on the fly each frame
- [x] Build verified: `MULTICORE=ON` (26.6MB) and `MULTICORE=OFF` (25.5MB)

### 9. Save Memory (SRAM/Flash/EEPROM)
- [x] `mDirectorySetInit(&core_->dirs)` called in GBACore constructor
- [x] `mDirectorySetDeinit(&core_->dirs)` called in GBACore destructor (flushes save data)
- [x] `SetSaveDirectory()` configures `DIRECTORY_SAVEDATA/GBA/` via PPSSPP's `GetSysDirectory()`
- [x] `mCoreAutoloadSave()` called after `loadROM()` — auto-loads `.sav` if exists, creates new if not
- [x] mGBA manages dirty tracking + writeback automatically — no manual flush needed
- [x] Save memory files: `PSP/SAVEDATA/GBA/<title>.sav`

### 10. Save State (Snapshot)
- [x] `DoGBAState()` — raw buffer save/load using `GBACore::SaveState()`/`LoadState()`/`GetStateSize()`
- [x] File format: `PSP/PPSSPP_STATE/GBA/<prefix>_N.gbast` (binary, no PSP chunk format)
- [x] Prefix from game title + game code for dedup (e.g., `AGB-POKE_Pokemon_Emerald`)
- [x] Reuses existing hotkeys: F1 (save), F3 (load), F2/F4 (prev/next slot)
- [x] Reuses existing `iCurrentStateSlot` (0-4) — same config as PSP
- [x] Reuses existing slot display OSD (`SAVESTATE_DISPLAY_SLOT`)
- [x] **No new UI** — same pause menu, same hotkeys, same slot indicator
- [x] Build verified: `MULTICORE=ON` and `MULTICORE=OFF`

### 11. Test Verification
- [x] `test_gba_core` binary built (2.4MB)
- [ ] Need a GBA ROM to run actual test
- [ ] Verify input mapping works end-to-end
- [ ] Verify savestate serialization

---

## 📊 Build Artifacts

| Target | Size | Status |
|--------|------|--------|
| `PPSSPPSDL` (MULTICORE=ON) | 26.6MB | ✅ |
| `PPSSPPSDL` (MULTICORE=OFF) | 25.5MB | ✅ zero impact |
| `test_gba_core` | 1.6MB | ✅ |
| `libmgba.a` | 1.8MB | ✅ |
| `libEmuCore.a` | 25KB | ✅ |

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
