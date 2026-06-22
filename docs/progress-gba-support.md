# GBA Support — Progress Report

**Date:** 2026-06-22
**Branch:** `feature/lan-sync`
**Last commit:** `4846451c` — GBACore.h include fix, PSP filter lambda
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
| **Recent tab** | ✅ **WORKING** | PSP & GBA grouping: sync-fill + ScrollView wrapper fix |
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
| Config isolation (UI) | 🟡 | Setting PSP masih muncul di GBA mode |
| Recent tab (GBA not added) | ✅ **SELESAI** | `g_recentFilesGBA.Add()` di InitGBA |
| Recent tab (group per emulator) | ✅ **SELESAI** | 3 bugs fixed: race condition, data loss cascade, ScrollView single-child render |
| ScrollView single-child render (FIXED) | ✅ | `ScrollView::Measure()` hanya render `views_[0]` — child ke-2 (GBA) tidak pernah di-layout. Fix: LinearLayout wrapper |
| Clear Recent tidak clear GBA (FIXED) | ✅ | `OnRecentClearClick` + `RestoreSettingsBits::RECENT` hanya clear PSP. Fix: tambah `g_recentFilesGBA.Clear()` |
| Race condition GBA recent (FIXED) | ✅ | Thread belum proses Load → HasAny=false + Save timpa INI. Fix: `FillSync()` synchronous fill |
| Data loss cascade (FIXED) | ✅ | GBA recent kosong → `g_Config.Save()` timpa INI → `[GBA Recent]` section hilang. Fix: `FillSync()` cegah data kosong |
| RecentFilesRegistry | ✅ **SELESAI** | Registry terpusat — tambah core baru = 1 Register() call + InitXXX() Add() |
| Game icon/cover | ❌ | GBA tidak punya cover download |
| Key mapping terpisah | 🟡 | Plan siap — GBA VIRTKEY, belum diimplement |
| Android build | 🔴 | Belum dimulai |

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

## Build & Run

```bash
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)
./build-final/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"
```
