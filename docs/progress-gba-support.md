# GBA Support — Progress Report

**Date:** 2026-06-21 (updated)
**Branch:** `feature/lan-sync`
**Last commit:** `60b0cd22ea`
**Build (latest):** `build-final/PPSSPPSDL` — MULTICORE=ON ✅

---

## Target Platform

| Platform | Build System | Status |
|----------|-------------|--------|
| **Linux SDL** | CMake | ✅ **Working** — build sukses, runtime verification via user |
| **Android** | ndk-build (`Android.mk`) | 🔴 **Belum dimulai** — lihat [Android Build](#-android-build) |
| **Qt** | ❌ **Excluded** — Wayland/X11 compatibility issues |

---

## Status Runtime

| Item | Status | Terakhir Diverifikasi |
|------|--------|----------------------|
| **Video rendering (gambar)** | 🟡 **KELUAR tapi warna biru/ungu** — color format issue | 2026-06-21 |
| **Input (keyboard)** | ✅ **WORKING** — A/S/Z/X/Space/arrows | 2026-06-21 |
| **Save memory (SRAM)** | ✅ **WORKING** — auto-load/flash via mGBA | 2026-06-21 |
| **ESC pause menu** | ✅ **WORKING** — pause muncul saat ESC | 2026-06-21 |
| **Speed control** | ❌ **TIDAK ADA EFEK** — Tab/Backspace tidak respon | 2026-06-21 |
| **Audio** | 🟡 **BERSUARA tapi masih kasar** — crackling | 2026-06-21 |
| **Save state (F1/F3)** | ❌ **TIDAK BEKERJA** — tombol tidak respon di GBA mode | 2026-06-21 |
| **Config isolation** | ❌ **TIDAK BEKERJA** — setting tidak terisolasi per-core | 2026-06-21 |

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
- [x] Static library `libmgba.a` (LIBMGBA_ONLY, --whole-archive)
- [x] mCoreConfigInit, setVideoBuffer
- [x] Input mapping via `PSPSKeysToGBA()`
- [x] Save memory: `mDirectorySetInit` + `mCoreAutoloadSave`

### 3. File Detection & Routing
- [x] `.gba/.gb/.gbc` di GameBrowser filter
- [x] Auto-detect di LaunchFile (UI/MainScreen.cpp)
- [x] NativeApp auto-boot detection
- [x] EmuScreen multi-core aware

### 4. CMake Build System
- [x] `EmuCore/CMakeLists.txt` with PPSSPP_MULTICORE flag
- [x] Dual build verified: ON (26.7MB) / OFF (25.5MB)
- [x] `--whole-archive mgba` for linker
- [x] `test_gba_core` target (ALL TESTS PASSED)

### 5. Video Rendering
- [x] Thin3D pipeline + RGBA8888 texture (lazy init)
- [x] Fullscreen quad with 3:2 aspect (letterbox)
- [x] DeviceLost/DeviceRestored lifecycle
- [x] ✅ **VISUALLY WORKING**

### 6. Audio Pipeline (sinc resampler)
- [x] **mGBA's own sinc resampler** (`mINTERPOLATOR_SINC`) — replaces old linear interpolation
- [x] `mAudioResampler` reads from core buffer (32768 Hz), outputs at 44100 Hz
- [x] DC blocking filter (high-pass, SkyEmu-inspired)
- [x] Soft clipping to [-32768, 32767]
- [x] `ClearAudio()` — prevents overflow during fast forward
- [x] Audio push per-frame, bukan batch

### 7. Input & Speed Control
- [x] All game buttons mapped
- [x] ESC → pause (direct handler)
- [x] **Tab (hold)** → fast forward (3-8 frame/update)
- [x] **Backspace** → toggle speed NORMAL → CUSTOM1 → CUSTOM2
- [x] **Shift+Tab** → slow motion (timing throttle)

### 8. Config Isolation
- [x] PSP config saved to memory, GBA config isolated
- [x] `[GBA]` section in `ppsspp.ini`
- [x] GBA defaults: nearest filter, auto resolution

### 9. Touch Layout
- [x] `UI/TouchLayoutGBA.h/.cpp` — A/B/D-Pad/L/R/Start/Select

### 10. Debug Logging
- [x] `[GBA]`, `[CONFIG]`, `[BOOT]` prefixes
- [x] Audio frame stats, native pairs, resampler output
- [x] mGBA internal logs (DMA, SWI)

### 11. #ifdef Cleanup (P2)
- [x] 31 dari ~45 #ifdef dihilangkan, pakai `IsGBA()` pattern
- [x] Helper methods: `InitGBA()`, `ShutdownGBA()`, `UpdateGBA()`, `RenderGBA()`
- [x] `static constexpr bool IsGBA() { return false; }` saat OFF

---

## 🟡 Diketahui Bermasalah

### 1. Audio: Suara masih kasar (crackling) — 🔴 PRIORITAS TINGGI
**Gejala:** Suara game keluar tapi terdengar kasar/crackling, kurang jernih.
**Riwayat investigasi:**

| # | Approach | Hasil |
|---|----------|-------|
| 1 | Linear interpolation 32768→44100 | ❌ Kasar |
| 2 | Fresh pair extraction (buang duplicate SOUNDBIAS) | ❌ Malah hilang data valid |
| 3 | DC blocking filter + soft clip (SkyEmu-inspired) | ❌ Tidak cukup |
| 4 | mGBA sinc resampler (`mINTERPOLATOR_SINC`) | 🟡 Masih kasar |
| 5 | Speed control fix (ClearAudio antar frame) | 🟡 Sedikit membaik |

**Teori saat ini:**
- Masalah **bukan** dari speed control
- Masalah **bukan** dari kualitas resampler (sinc = kualitas tinggi)
- Kemungkinan: **SOUNDBIAS oversampling** (65536 Hz) → sinc downsampling ke 44100 Hz masih ada artifact
- Atau: **Aliasing dari square wave harmonics** — PSG square wave harmonics tinggi

---

### 2. Video: Warna kebiru-biruan / ungu — 🔴 PRIORITAS TINGGI
**Gejala:** Gambar game muncul tapi dengan tint biru/ungu, warna tidak akurat.
**Lokasi kode:** Konversi XBGR8 → RGBA8888 di `GBACore::RunFrame()`

```cpp
// Kode saat ini:
uint8_t r = c & 0xFF;
uint8_t g = (c >> 8) & 0xFF;
uint8_t b = (c >> 16) & 0xFF;
uint8_t a = (c >> 24) & 0xFF;
videoBuffer_[y * GBA_WIDTH + x] = (a << 24) | (r << 16) | (g << 8) | b;
```

Asumsi: mGBA output `mColor` format XBGR8 → konversi seperti di atas.
Kenyataan: Mungkin format `mColor` berbeda (ABGR, BGRA, atau RGB565 tergantung backend).

---

### 3. Speed Control: Tab/Backspace tidak ada efek — 🟡 SEDANG
**Gejala:** Menekan Tab (fast forward) atau Backspace (toggle speed) tidak mengubah kecepatan game.
**Lokasi kode:** `UI/EmuScreen.cpp` — `UpdateGBA()`

Kemungkinan penyebab:
- `PSP_CoreParameter().fastForward` tidak di-set untuk GBA mode (VIRTKEY handler mungkin skip GBA)
- Atau: update loop tidak memproses key event dengan benar untuk GBA

---

### 4. Save State: F1/F3 tidak berfungsi — 🟡 SEDANG
**Gejala:** Menekan F1 (save) atau F3 (load) tidak melakukan apa-apa di GBA mode.
**Lokasi kode:** `UI/EmuScreen.cpp` — VIRTKEY handler untuk save/load

VIRTKEY handler mungkin melempar ke PSP save state system, bukan GBACore.

---

### 5. Config Isolation: Setting tidak terisolasi per-core — 🟡 SEDANG
**Gejala:** Pengaturan yang diubah (misal filter, resolusi) diterapkan ke semua game,
tidak peduli PSP atau GBA.
**Lokasi kode:** `EmuCore/Config.cpp` — per-core config

---

### 6. Android Build — BELUM DIMULAI
- Menunggu audio fix + video color fix + save state fix
- Integrasi ndk-build (Android.mk) untuk mgba + EmuCore

---

## 🧪 Log Audio (Reference)

Dari test 2026-06-21 dengan Breath of Fire (USA):

```
05:00:034 [GBA] Audio frame 1: coreAvail=82  coreRate=32768 outPairs=100
05:00:086 [GBA] Audio frame 2: coreAvail=564 coreRate=32768 outPairs=738
05:00:103 [GBA] Audio frame 3: coreAvail=794 coreRate=65536 outPairs=524  ← SOUNDBIAS berubah!
05:00:103 [GBA] Audio mix frame 3: pairs=524 firstOut=[0,0] lastOut=[0,0]  ← boot silence
05:15:159 [GBA] Audio frame 900: coreAvail=1112 coreRate=65536 outPairs=738 first=[1279,1279]
05:15:159 [GBA] Audio mix frame 900: pairs=738 firstOut=[1279,1279] lastOut=[461,461]
05:25:228 [GBA] Audio frame 1500: coreAvail=1111 coreRate=65536 outPairs=737 first=[1344,959]
05:25:228 [GBA] Audio mix frame 1500: pairs=737 firstOut=[1344,959] lastOut=[-1029,2209]
05:30:263 [GBA] Audio frame 1800: coreAvail=1115 coreRate=65536 outPairs=740 first=[-238,-374]
```

Observasi:
- `coreRate` berubah dari 32768 → 65536 setelah boot (game set SOUNDBIAS resolution)
- `outPairs` stabil di 737-741 (target 735) → resampler timing akurat
- Audio data non-zero (`first=[1279,1279]`) → audio mengalir
- `frames=1` → speed control normal, tidak fast forward

---

## 🐛 Bugs Fixed (Total: 15 Bugs)

| # | Bug | Root Cause | Fix | Sesi |
|---|-----|------------|-----|------|
| 1 | Segfault boot | Linker strip mGBA symbols | `--whole-archive` | Pra-P1 |
| 2 | HashTableLookup crash | mCoreConfig uninit | `mCoreConfigInit()` | Pra-P1 |
| 3 | Video all-black | setVideoBuffer not called | Panggil setVideoBuffer | Pra-P1 |
| 4 | Texture crash | desc.depth=0 | depth=1 | Pra-P1 |
| 5 | GBA never drawn | PSP check before GBA path | Reorder render | Pra-P1 |
| 6 | Audio noise | Sample overflow | Cap 1470/frame | Pra-P1 |
| 7 | Audio slow-mo | Mono vs stereo mismatch | `/2` fix | Pra-P1 |
| 8 | ESC not working | Early return | Direct handler | Pra-P1 |
| 9 | PSP_Shutdown on GBA | No guard | coreType check | Pra-P1 |
| 10 | Config mixing | No isolation | Save/Restore config | Pra-P1 |
| 11 | Audio slow-mo (resample) | Silence padding 25.7% | Zero padding removal | P1 |
| 12 | Duplicate render code | Thin3D in EmuScreen | Move to GBACore | P1 |
| 13 | Duplicate audio code | Audio convert in EmuScreen | Move to GBACore | P1 |
| 14 | #ifdef clutter | 45 #ifdefs | IsGBA() pattern | P2 |
| 15 | Save state in EmuScreen | Logic campur | Move to GBACore | P3 |
| **16** | **Audio crackling (current)** | ??? | **Belum fixed** | 🔴 |
| **17** | **Video warna biru/ungu** | Mungkin format mColor berbeda (ABGR vs XBGR) | **Belum fixed** | 🔴 |
| **18** | **Speed control tidak berefek** | VIRTKEY handler mungkin skip GBA | **Belum fixed** | 🟡 |
| **19** | **Save state F1/F3 tidak kerja** | Handler lempar ke PSP system | **Belum fixed** | 🟡 |
| **20** | **Config tidak terisolasi** | Per-core config belum jalan | **Belum fixed** | 🟡 |

---

## 📋 Next Steps (Prioritas)

1. **🔴 Audio quality** — root cause crackling
   - Investigasi sinc width/quality parameter mGBA
   - Bandingkan output dengan mGBA standalone
   - Coba bypass GBA sound channels individually untuk isolasi masalah

2. **🟡 Save state test** — runtime verify F1/F3/F2/F4 works

3. **🟡 Android integration** — setelah audio fix + save state verified

---

## Build Commands

```bash
# Build with GBA support
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)

# Run with GBA log
./build-final/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"
```
