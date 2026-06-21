# GBA Support — Progress Report

**Date:** 2026-06-21 (final update)
**Branch:** `feature/lan-sync`
**Last commit:** `59ce8a1` — sinc width 8→16 + direct SDL
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
| **Video** | ✅ **FIXED** | R↔B swap di pixel packing (mColor BGR→RGBA) |
| **Input** | ✅ **WORKING** | A/S/Z/X/Space/arrows + ESC pause |
| **Save RAM** | ✅ **WORKING** | SRAM auto-load/flash via mGBA |
| **ESC pause** | ✅ **WORKING** | Direct handler di UnsyncKey |
| **Audio** | ✅ **FIXED** | 3 bugs berturut-turut (lihat bawah) |
| **Speed control** | ❌ **Belum test** | VIRTKEY handler perlu verifikasi |
| **Save state** | ❌ **Belum test** | F1/F3 + pause menu perlu implement |
| **Config isolation** | ❌ **Belum** | Per-core config blm jalan |

---

## Audio Pipeline (Final)

```
mGBA core → [sinc resampler width=16] → int16 → DC filter → SDL langsung
```

- **Satu** resample step (32768/65536 → 44100 Hz)
- Sinc width **16** (upgrade dari default 8)
- Bypass PPSSPP StereoResampler sepenuhnya
- Format int16 langsung ke SDL (S16 format)

### Bugs yang Pernah Terjadi

| # | Bug | Root Cause | Fix |
|---|-----|------------|-----|
| 1 | **Kasar/clipping total** | `(int32_t)sample << 16` → semua sample 65536x over range → hard-clip | Hapus `<< 16`, simpan int16 langsung |
| 2 | **Mendem/tidak jernih** | Push `int32_t*` ke SDL stream format `S16` → SDL baca tiap int32 sebagai 2 int16 | Push `int16_t*` via `GetRawAudio()` |
| 3 | **Kresek-kresek** | Skip push saat SDL buffer penuh → gap 138ms tanpa audio | Jangan skip, biarkan SDL handle buffering |
| 4 | **Double resample** | sinc 44100 → StereoResampler linear → host | Direct SDL, bypass StereoResampler |
| 5 | **Akumulasi source** | Resampler tinggalkan ~10 sample/frame | Drain ke LOW_WATER=8 |

### Referensi

- **mGBA**: sinc width=8 default. Sama dengan mGBA SDL frontend.
- **SkyEmu**: band-limited square wave synthesis + DC filter (`capacitor *= 0.996`)
- Kita adopsi DC filter dari SkyEmu, sinc resampler dari mGBA

---

## Video Pipeline

```
mGBA render → rawVideoBuffer_ (mColor, format M_RGB5_TO_BGR8)
→ konversi: extract R,G,B → pack sebagai (A<<24)|(B<<16)|(G<<8)|R
→ Thin3D texture upload → fullscreen quad (3:2 aspect)
```

Bug: R↔B terbalik karena `(R<<16)|(G<<8)|B` = memory B,G,R,A (little-endian)
tapi Thin3D expect R,G,B,A. Fix: `(B<<16)|(G<<8)|R` = memory R,G,B,A ✓

---

## Known Issues (Open)

| Issue | Priority | Note |
|-------|----------|------|
| **Audio** masih belum 100% setara mGBA | 🟡 | Mungkin butuh fine-tune sinc atau filter |
| **Speed control** Tab/Backspace | 🟡 | VIRTKEY handler perlu test |
| **Save state** F1/pause menu | 🟡 | Pause menu panggil PSP API, bukan GBACore |
| **Config isolation** | 🟡 | Setting PSP & GBA masih campur |
| **Recent tab** | ❌ **TIDAK BEKERJA** | `g_recentFiles.Add()` tidak pernah dipanggil untuk GBA |
| **Game icon** | ❌ **TIDAK TAMPIL** | PPSSPP download icon dari server untuk PSP game ID, GBA tidak punya |
| **Android build** | 🔴 | Belum dimulai |

---

## Build & Run

```bash
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)
./build-final/PPSSPPSDL "/path/to/game.gba"
```
