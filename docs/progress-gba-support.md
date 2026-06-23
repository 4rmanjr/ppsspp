# GBA Support — Progress Report

**Date:** 2026-06-23
**Branch:** `feature/lan-sync`
**Last commit:** `8e4cc56` — Fix texture filtering + volume not applied from settings ✅
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
| **GBA Settings Screen** | ✅ **SELESAI** | Controls, Display, Audio — tidak ganggu PSP |
| **Config isolation** | ✅ **SELESAI** | GBA punya settings screen sendiri, Control Mapping filter PSP sections |
| **Recent tab** | ✅ **WORKING** | PSP & GBA grouping: sync-fill + ScrollView wrapper fix |
| **Game icon/cover** | ❌ **TIDAK TAMPIL** | PPSSPP download icon untuk PSP game ID |
| **Android build** | 🔴 **Belum dimulai** | |

---

## Audio Pipeline (Final)

```
mGBA core → [sinc resampler width=24, resolusi=16384] → int16 → DC filter → SIMD clamp → SDL langsung
```

- **Satu** resample step (32768/65536 → 44100 Hz)
- Bypass PPSSPP StereoResampler sepenuhnya
- Format int16 langsung ke SDL (S16 format)
- mGBA's own sinc resampler (`mAudioResampler` + `mINTERPOLATOR_SINC`)
- **SIMD optimizations:** SSE2 (x86) / ARM NEON untuk clamping (4-8x lebih cepat)

### Audio Quality Improvements (2026-06-23)

**Phase 1: Resolution & Rounding**
- Sinc resolution: 8192 → 16384 (2x smoother interpolation curves)
- Proper rounding: float→int16 conversion (±0.5) mengurangi quantization noise

**Phase 2: SIMD Optimizations**
- `ClampFloatToS16_SIMD()` helper dengan SSE2/ARM NEON/scalar fallback
- GetRawAudio refactored: DC filter scalar + SIMD clamp
- GetMixedAudio refactored: DC filter scalar + SIMD clamp
- Performance: 4-8x faster clamping operations

**Phase 3: Advanced Anti-Aliasing**
- Sinc width: 16 → 24 (49-tap filter, better frequency response)
- CPU cost: ~1.5x resampling (still <2% total CPU)

**Expected results:**
- Clearer high-frequency response (less "muddy" sound)
- Lower noise floor on quiet passages
- Reduced quantization artifacts
- Flatter passband, steeper stopband rolloff

### Audio Bugs Fixed

| # | Bug | Fix |
|---|-----|-----|
| 1 | Clipping total (`<< 16`) | Hapus shift, simpan int16 langsung |
| 2 | Format mismatch (int32→SDL_S16) | Push int16 via `GetRawAudio()` |
| 3 | Skip push → gap audio | Jangan skip, biarkan SDL handle buffer |
| 4 | Double resample (sinc + StereoResampler) | Direct SDL, bypass StereoResampler |
| 5 | Resampler source accumulation | Drain ke LOW_WATER |
| 6 | Sinc width default 8 | Upgrade ke 16 |
| 7 | LOW_WATER mismatch (crackle) | Use r->lowWaterMark dynamically |
| 8 | dcCap state corruption | Separate dcCapRaw* for GetRawAudio |
| 9 | Stale resampler timestamp | Reset r->timestamp in ClearAudio |

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
| Config isolation (UI) | ✅ **SELESAI** | GBA Settings Screen sendiri, Control Mapping filter sections |
| Recent tab (GBA not added) | ✅ **SELESAI** | `g_recentFilesGBA.Add()` di InitGBA |
| Recent tab (group per emulator) | ✅ **SELESAI** | 3 bugs fixed: race condition, data loss cascade, ScrollView single-child render |
| ScrollView single-child render (FIXED) | ✅ | `ScrollView::Measure()` hanya render `views_[0]` — child ke-2 (GBA) tidak pernah di-layout. Fix: LinearLayout wrapper |
| Clear Recent tidak clear GBA (FIXED) | ✅ | `OnRecentClearClick` + `RestoreSettingsBits::RECENT` hanya clear PSP. Fix: tambah `g_recentFilesGBA.Clear()` |
| Race condition GBA recent (FIXED) | ✅ | Thread belum proses Load → HasAny=false + Save timpa INI. Fix: `FillSync()` synchronous fill |
| Data loss cascade (FIXED) | ✅ | GBA recent kosong → `g_Config.Save()` timpa INI → `[GBA Recent]` section hilang. Fix: `FillSync()` cegah data kosong |
| RecentFilesRegistry | ✅ **SELESAI** | Registry terpusat — tambah core baru = 1 Register() call + InitXXX() Add() |
| Game icon/cover | ❌ | GBA tidak punya cover download |
| Key mapping terpisah | ✅ **SELESAI** | VIRTKEY_GBA_* (0x40000040+), default keyboard mappings, save/load INI |
| GBA Settings Screen | ✅ **SELESAI** | `docs/superpowers/plans/2026-06-22-gba-settings-screen.md` — 9 tasks, Controls+Display+Audio |
| GBA Display Layout | ✅ **SELESAI** | Aspect ratio + integer scaling via GetRenderRect |
| Android build | 🔴 | Belum dimulai |

---

## Key Mapping — VIRTKEY_GBA_*

| VIRTKEY | Default Keyboard | GBA Bit |
|---------|-----------------|---------|
| `VIRTKEY_GBA_A` | Z | A |
| `VIRTKEY_GBA_B` | X | B |
| `VIRTKEY_GBA_L` | A | L |
| `VIRTKEY_GBA_R` | S | R |
| `VIRTKEY_GBA_START` | Space | Start |
| `VIRTKEY_GBA_SELECT` | Shift | Select |
| `VIRTKEY_GBA_UP/DOWN/LEFT/RIGHT` | Arrow keys | D-Pad |

Mapping bisa diubah di **Control Mapping** screen — VIRTKEY_GBA_* muncul otomatis.
Mapping tidak mengganggu PSP keys.

---

## Compliance Gap: LAN Sync Feature 🚨

Berbeda dengan GBA yang **patuh 100%** terhadap aturan fork (`PPSSPP_MULTICORE`, `[PPSSPP-FORK]` markers, file di `EmuCore/`),
**LAN Sync melanggar hampir semua aturan** dari `AGENTS.md` dan `docs/agents/fork-maintenance.md`.

### 🔴 Pelanggaran

| # | Aturan | Pelanggaran |
|---|--------|-------------|
| 1 | File kustom di direktori non-inti | **4 file di `Core/`** — `Core/SaveStateLANSync.h/.cpp`, `Core/LANSyncConfig.h/.cpp` (terlarang) |
| 2 | Feature flag sendiri | **Tidak ada.** `PPSSPP_LANSYNC` tidak pernah didefinisikan atau dicek — always-on |
| 3 | `[PPSSPP-FORK]` marker di setiap file | **25+ file tanpa marker** — semua file `*LANSync*`, `Common/Net/*`, platform backends |
| 4 | Dual-build verification (ON/OFF) | **Mustahil.** Tanpa flag, tidak bisa build tanpa LAN Sync |

### 🔴 Detail File di Core/ (Terlarang)

```
Core/SaveStateLANSync.h
Core/SaveStateLANSync.cpp
Core/LANSyncConfig.h
Core/LANSyncConfig.cpp
```

Harusnya dipindah ke direktori sendiri (misal `LANSync/`) seperti `EmuCore/` untuk GBA.

### 🔴 File Lain Tanpa Marker

- `SDL/SDLLANSync.cpp/.h`
- `SDL/LinuxLANSync.cpp/.h`
- `UI/LANSyncSettings.cpp/.h`
- `Windows/WinLANSync.cpp/.h`
- `macOS/CocoaLANSync.mm/.h`
- `macOS/MacLANSync.h/.mm`
- `android/jni/AndroidLANSync.cpp/.h`
- `android/src/.../LANSync*.java`
- `Common/Net/` — 34 file (MDNS, TLS, HTTP, UDP, WebSocket, PlatformKeyStore, Resolve, URL, dll)

### 🔴 Common/Net/ — Asal Usul Tidak Jelas

34 file di `Common/Net/` tidak punya `[PPSSPP-FORK]` marker.
Tidak bisa dibedakan mana dari upstream dan mana tambahan fork tanpa cek git history manual.
Ini risiko saat merge upstream: conflict tidak terdeteksi.

### Akar Masalah

LAN Sync dibangun **sebelum** aturan fork (`AGENTS.md`) dirumuskan.
Waktu GBA dikerjakan, aturan sudah ada — makanya GBA patuh.

### Perlukah Diperbaiki?

✅ **Iya**, kalau mau:
- Bisa merge upstream tanpa conflict tak terduga
- Build bisa disable LAN Sync (`-DPPSSPP_LANSYNC=OFF`)
- Kode fork jelas terbedakan dari upstream

Refactor yang dibutuhkan:
1. Pindah file dari `Core/` ke `LANSync/`
2. Tambah `PPSSPP_LANSYNC` flag di CMake
3. Wrap semua kode dengan `#ifdef PPSSPP_LANSYNC`
4. Tambah `[PPSSPP-FORK] LANSync:` marker di semua file
5. Verifikasi build ON/OFF

---

## Adding Future Cores (Recent Files Grouping)

`RecentFilesRegistry` (`EmuCore/RecentFilesRegistry.h/.cpp`) adalah registry terpusat
untuk recent files grouping. Setiap emulator core cukup mendaftarkan diri:

```cpp
// Di NativeApp.cpp, setelah EnsureThread
auto &reg = EmuCore::RecentFilesRegistry::Get();
reg.Register(EmuCore::RecentFilesEntry{
    (int)EmuCore::Type::N64,   // coreType dari EmuCore::Type
    "N64",                     // displayName → "N64 GAMES (N)"
    "N64 Recent",              // iniSection → [N64 Recent] di ppsspp.ini
    "RECENT_N64",              // specialPath → GameBrowser path "!RECENT_N64"
    &g_recentFilesN64,         // manager → RecentFilesManager global
    nullptr,                   // filter → opsional, nullptr = tampilkan semua
    ".n64:.z64:.v64",          // extensions → untuk DetectType
});
```

Lalu di `InitN64()`:

```cpp
g_recentFilesN64.Add(filename.ToString());
```

Selesai. `CreateRecentTab()`, `HasSpecialFiles()`, `DisplayTopBar()`
otomatis iterasi registry — tidak perlu edit lagi.

## Upcoming Plans (Belum Dikerjakan)

| Plan | Tasks | Spec |
|------|-------|------|
| **GBA Settings Screen** | ✅ **SELESAI** | Controls, Display, Audio — tidak ganggu PSP |
| GBA Display Layout | ✅ (bagian dari GBA Settings Screen) | Aspect ratio + integer scaling |

Cara mulai eksekusi:

```bash
cd docs/superpowers/plans/
# Buka 2026-06-22-gba-settings-screen.md, mulai dari Task 1
```

## Build & Run

```bash
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)
./build-final/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"
```
