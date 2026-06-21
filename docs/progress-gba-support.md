# GBA Support — Progress Report

**Date:** 2026-06-22 (updated)
**Branch:** `feature/lan-sync`
**Last commit:** `5e3b5e5` — save state fix: CreateFullPath bug
**Build:** `build-final/PPSSPPSDL` — MULTICORE=ON ✅

---

## Target Platform

| Platform | Build System | Status |
|----------|-------------|--------|
| **Linux SDL** | CMake | ✅ **Working** — build + runtime verified |
| **Android** | ndk-build | 🔴 **Belum dimulai** |
| **Qt** | ❌ **Excluded** — Wayland/X11 issues |

---

## Status Runtime

| Item | Status | Catatan |
|------|--------|---------|
| **Video** | ✅ **FIXED** | R↔B swap di pixel packing |
| **Input (keyboard)** | ✅ **WORKING** | A/S/Z/X/Space/arrows |
| **Save RAM (SRAM)** | ✅ **WORKING** | Auto-load/flash via mGBA |
| **ESC pause** | ✅ **WORKING** | Direct handler di UnsyncKey |
| **Audio** | ✅ **FIXED** | sinc resampler + direct SDL |
| **Save state (F1/F3)** | ✅ **WORKING** | `<SAVESTATE>/GBA_<code>_<title>_<slot>.ppst` |
| **Save state (pause menu)** | ✅ **WORKING** | SaveSlotView + ScreenshotViewScreen redirect ke GBACore |
| **Save state thumbnail** | ✅ **WORKING** | `pngSave()` dari `videoBuffer_` ke `.jpg` (auto-detect) |
| **Speed control** | ❌ **Belum test** | |
| **Config isolation** | 🟡 **SEBAGIAN** | Boundary OK, UI settings masih campur |
| **Recent tab** | ❌ **TIDAK BEKERJA** | GBA game tidak tercatat di Recent |
| **Game icon/cover** | ❌ **TIDAK TAMPIL** | PPSSPP download icon untuk PSP game ID |
| **Android build** | 🔴 **Belum dimulai** | |

---

## Audio Pipeline (Final)

```
mGBA core → [sinc resampler width=16, resolusi=8192] → int16 → DC filter → SDL langsung
```

- **Satu** resample step (32768/65536 → 44100 Hz)
- Bypass PPSSPP StereoResampler sepenuhnya
- Format int16 langsung ke SDL (S16 format)
- mGBA's own sinc resampler (`mAudioResampler` + `mINTERPOLATOR_SINC`)

### Audio Bugs Fixed

| # | Bug | Fix |
|---|-----|-----|
| 1 | Clipping total (`<< 16`) | Hapus shift, simpan int16 langsung |
| 2 | Format mismatch (int32→SDL_S16) | Push int16 via `GetRawAudio()` |
| 3 | Skip push → gap audio | Jangan skip, biarkan SDL handle buffer |
| 4 | Double resample (sinc + StereoResampler) | Direct SDL, bypass StereoResampler |
| 5 | Resampler source accumulation | Drain ke LOW_WATER |
| 6 | Sinc width default 8 | Upgrade ke 16 |

---

## Video Pipeline

```
mGBA render → rawVideoBuffer_ (mColor, format M_RGB5_TO_BGR8)
→ extract R,G,B → pack (A<<24)|(B<<16)|(G<<8)|R
→ Thin3D texture upload → fullscreen quad (3:2 aspect)
```

Bug: R↔B terbalik (little-endian byte order). Fix: `(B<<16)|(G<<8)|R`.

---

## Save State

### Format
- **File:** `<SAVESTATE>/GBA_<gameCode>_<sanitizedTitle>_<slot>.ppst`
- **Content:** Raw binary dari `core_->saveState()` (mGBA internal state)
- **Size:** ~388KB (Breath of Fire)
- **Trigger:** F1 (save), F3 (load), pause menu ScreenshotViewScreen

### Limitations
- ❌ Main pause menu slot view (SaveSlotView) masih panggil PSP `SaveState::`
- ❌ Tidak ada screenshot/thumbnail untuk GBA save state
- ❌ Tidak ada undo save / rewind untuk GBA

---

## Known Issues

| Issue | Priority | Note |
|-------|----------|------|
| Pause menu save slot masih PSP | 🟡 | SaveSlotView di GamePauseScreen panggil PSP API |
| Thumbnail save state | 🟡 | Perlu screenshot capture + save sbg JPEG |
| Speed control (Tab/Backspace) | 🟡 | Belum diverifikasi |
| Config isolation (UI) | 🟡 | Setting PSP masih muncul di GBA mode |
| Recent tab (GBA not added) | 🟡 | `g_recentFiles.Add()` tidak dipanggil |
| Recent tab (group per emulator) | 💡 | Saran fitur |
| Game icon/cover | ❌ | GBA tidak punya cover download |
| Key mapping terpisah | 📋 | Plan siap di `docs/plans/gba-keymapping-plan.md` |
| Android build | 🔴 | Belum dimulai |

---

## Build & Run

```bash
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)
./build-final/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"
```
