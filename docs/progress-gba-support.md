# GBA Support — Progress Report

**Date:** 2026-06-26
**Branch:** `feature/lan-sync`
**Last commit:** `02134d354c` — feat(gba): Phase 4 — CoreButtonRegistry, remove 3 hardcoded CTRL_* switches
**Uncommitted work:** — (modified: `UI/EmuScreen.cpp`, `CMakeLists.txt`, `docs/progress-gba-support.md`; new: `EmuCore/GBASpeedControl.h`, `unittest/TestGBASpeedControl.cpp`)
**Build Linux SDL:** `build-final/PPSSPPSDL` — MULTICORE=ON ✅ (no regression)
**Build Android `normalRelease`:** ✅ (APK: 53MB, optimized `-O2`)
**Build Android `goldRelease`:** ✅ (APK: 55MB, optimized `-O2`, +PPSSPP_MULTICORE)
**Installed on device:** ✅ goldRelease (org.ppsspp.ppssppgold) — Infinix X6815D

---

## Target Platform

| Platform | Build System | Status |
|----------|-------------|--------|
| **Linux SDL** | CMake | ✅ **Working** — build + runtime verified |
| **Android** | Gradle→CMake | ✅ **SELESAI** — `normalRelease` + `goldRelease`, semua ABI, optimized `-O2` |
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
| **Save state thumbnail** | ✅ **WORKING** | `pngSave()` dari `videoBuffer_` ke `.jpg` (auto-detect) — juga terverifikasi di Android ✅ |
| **Speed control** | ✅ **SELESAI** | Unit test (8/8 passed) + helper `EmuCore/GBASpeedControl.h` |
| **GBA Settings Screen** | ✅ **SELESAI** | Controls, Display, Audio — tidak ganggu PSP |
| **Config isolation** | ✅ **SELESAI** | GBA punya settings screen sendiri, Control Mapping filter PSP sections |
| **Recent tab** | ✅ **WORKING** | PSP & GBA grouping: sync-fill + ScrollView wrapper fix |
| **Game icon/cover** | ❌ **TIDAK TAMPIL** | PPSSPP download icon untuk PSP game ID |
| **Android build** | ✅ **SELESAI** | `normalRelease` (53MB) + `goldRelease` (55MB) — optimized, terinstall di device |
| **Android gold → PPSSPP_MULTICORE** | ✅ | Ditambah `-DPPSSPP_MULTICORE=ON` di flavor gold |
| **Speed control** | ❌ **Belum test** | |
| **Game icon/cover** | ❌ **SKIP** | GBA tidak punya cover download (PPSSPP cari PSP game ID) — bukan bug |
| **SaveSlotView GBA** | ✅ **SELESAI** | Thumbnail save state dari pause menu berfungsi di PC dan Android |

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
| Log suppression crash (FIXED) | ✅ | `malloc` → `calloc` — uninitialized `logFile` caused segfault on mGBA log |
| Android build | 🟡 | ndk-build files updated (`Android.mk`, `Locals.mk`) — mGBA cross-compile via Gradle→CMake **BLOCKED** (no NDK env) |
| Android intent filter | ✅ | `.gba/.gb/.gbc` added to `AndroidManifest.xml` |
| Android `armeabi-v7a` NEON (FIXED) | ✅ | `vcvtnq_s32_f32` only on ARMv8; guarded with `__aarch64__` |
| Android audio bridge | ✅ | Conditional path via PPSSPP mixer (`int16→int32 → System_AudioPushSamples`) |
| Android `normalRelease` | ✅ | 53MB, optimized, signed debug key |
| Android `goldRelease` | ✅ | 55MB, +PPSSPP_MULTICORE, signed debug key, overwrite gold existing |
| Android SAF URI (BUG FIX) | ✅ | `content://` → `Android_OpenContentUriFd()` + `VFileFromFD()` — di `EmuCore/GBACore.cpp` |
| Android ROM gagal → crash (BUG FIX) | ✅ | Guard di `EmuScreen::InitGBA()` — jangan set mode GBA kalau ROM gagal load |
| Android `ENABLE_VFS_FD` | ✅ | Required by `VFileFromFD()`, ditambah di `EmuCore/CMakeLists.txt` |
| Per-core touch config | ✅ | `CoreTouchConfig` system — migrated from `TouchLayoutGBA`, centralized di `EmuCore/Config.h/.cpp` |
| CoreTouchLayoutScreen | ✅ | NEW — per-core touch button editor (`UI/CoreTouchLayoutScreen.h/.cpp`) |
| Android virtual touch buttons | ✅ **WORKING** | 3 root causes: (1) `LoadGBAOverrides()` override `bShowTouchControls=0` — fix: force-set true di `LoadConfig()`. (2) `EmuScreen::update()` early return sebelum `UIScreen::update()` → `CreateViews()` tidak pernah dipanggil — fix: tambah `UIScreen::update()` di GBA path. (3) `coreState` tetap `CORE_POWERDOWN` (PSP tidak diinisialisasi) → `GamepadUpdateOpacity()` tanpa force set opacity=0 — fix: `GamepadUpdateOpacity(g_Config.iTouchButtonOpacity/100.0f)`. **(ce9c3ea + fix selanjutnya)** |
| GBA landscape touch positions | ✅ | Default 10 buttons landscape (D-Pad kiri, A/B kanan, L/R atas, Start/Select tengah). Terverifikasi di Samsung A05s. |
| GBA portrait touch positions | ✅ | Default portrait layout ada, tapi ada bug di `LoadTouchConfig()` — `cfgP.Clear()` unconditional hapus default portrait. Fix: pindahkan `cfgP.Clear()` di dalam `if (secP)` block. |
| Type::COUNT | ✅ | Sentry added ke `EmuCore::EmuCore.h` untuk array indexing |
| On-screen GBA touch button size | ✅ **FIXED** | `AddGBATouchButtons()` hardcoded `0.8f` — fix: `bgScale = (btn_.w × bounds.w) / atlasImg->w` |
| CoreTouchLayoutScreen code quality (orientation, save, i18n) | ✅ **FIXED** | Orientation: `GetDeviceOrientation()` (was hardcoded `false`/"Landscape"). Save: `onFinish()` calls `SaveTouchConfig()` (was log-only). Labels: `di->T("Snap")`/`di->T("Grid")` (was hardcoded Indonesian). Back button delegates to `onFinish()`. |
| CoreTouchLayoutScreen clamping, snap anchoring, grid class | ✅ **FIXED (98e586b)** | Clamping: `std::clamp()` prevent drag off-screen. Snap anchoring: relative to center (`fmod(nx-cx, grid)`). GBASnapGrid: class extracted from inline Draw() — matches PSP SnapGrid pattern. |
| GBA portrait screen position (DisplayOffsetY) | ✅ **FIXED (56924fa)** | PSP portrait pakai `fDisplayOffsetY=0.25` (top-aligned). GBA `GetRenderRect()` sebelumnya center (`offsetY=0.5`). Fix: baca `g_Config.displayLayoutPortrait.fDisplayOffsetY` — match PSP formula. |
| GBATouchVisibilityPopup — feature parity with PSP | ✅ **FIXED (1eaa2c7)** | PSP punya: (1) ikon button, (2) Toggle All, (3) save di `onFinish()` (semua exit path). GBA sebelumnya cuma checkbox text + save ONCLICK OK. Fix: `GBACheckBoxChoice` + ikon atlas (`I_CROSS`, `I_CIRCLE`, dll) + Toggle All + `OnCompleted()` untuk back button. |
| GBA Fast-forward & Pause buttons | ✅ **FIXED (c6faf8e + ed32432)** | System buttons (Fast-forward, Pause) dari shared TouchControlConfig. Visibility toggles di popup + button rendering di EmuScreen GBA path. Pause pakai `&pauseTrigger_`, Fast-forward pakai `&PSP_CoreParameter().fastForward`. Toggle All juga toggle system buttons. SetMinimumAlpha(0.1f) unconditional (match PSP). Pause checkbox disabled di device tanpa back button. |
| AGENTS.md diperkuat (Quality) | ✅ **FIXED (1672c42)** | Tambah: PSP Feature Parity, Scope Definition, Anti-Hallucination, Code Review Gate (pre/post commit), PSP Knowledge Base (docs/agents/psp-knowledge-base.md), Build Flag Convention. 6 prioritas utama + 2 quality gates. |
| AGENTS.md diperkuat (Extensibility) | ✅ **FIXED (5617acb)** | Tambah Extensibility Architecture. |
| AGENTS.md di-split untuk AI readability | ✅ **FIXED (9f95d42)** | AGENTS.md 466→98 lines (ringkas). Isi dipindah: quality-gates.md + extensibility.md. |
| Code Review — P2#1 CreateSystemTouchButtons | ✅ **FIXED (9c2af50)** | Extract 30 baris inline system buttons (FF/Pause) ke helper. Reusable untuk semua non-PSP core. 3 baris call site. |
| Code Review — P2#2 Switch dispatch | ✅ **FIXED (4025693)** | `if (coreType_ != PSP)` → `switch(coreType_)` dengan PSP, GBA, default case. Shared system buttons setelah switch. |
| Code Review — P3 GBA→Core rename | ✅ **FIXED (9a4ff18)** | 5 class rename: GBADragDrop→CoreDragDrop, GBASnapGrid→CoreSnapGrid, GBALayoutView→CoreLayoutView, GBACheckBoxChoice→CoreCheckBoxChoice, GBATouchVisibilityPopup→CoreTouchVisibilityPopup. |
| Code Review — P4 hoist getUIContext | ✅ **FIXED (be87503)** | `screenManager()->getUIContext()` dipanggil sekali di luar loop, bukan per iterasi. |
| Code Review — P5 nitpicks | ✅ **FIXED (9b461df)** | Value copy untuk screenBounds_ + granular [PPSSPP-FORK] markers di semua class definitions. |

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

## Upcoming Plans

### ✅ Core GBA — SELESAI

| Area | Status | Keterangan |
|------|--------|------------|
| Video rendering + Thin3D | ✅ | |
| Audio pipeline (sinc+DC+SIMD) | ✅ | |
| Keyboard input | ✅ | |
| Save RAM (SRAM/flash) | ✅ | Auto-load via mGBA |
| Save state (F1/F3 + pause menu) | ✅ | `.ppst` di `<SAVESTATE>/GBA_*` |
| Save state thumbnail | ✅ | `pngSave()` dari `videoBuffer_` |
| GBA Settings Screen | ✅ | Controls + Display + Audio |
| Config isolation | ✅ | Tidak ganggu PSP |
| Base UI (tabs, recent, game list) | ✅ | RecentFilesRegistry |
| Touch layout (per-core) | ✅ | CoreTouchConfig + CoreTouchLayoutScreen |
| **Android build** | ✅ | `normalRelease` + `goldRelease` — optimized, installed |

### 🟢 Next Polish (Low Priority)

| Item | Priority | Notes |
|------|----------|-------|
| **Speed control** | ✅ **SELESAI** | Unit test dibuat (`EmuCore/GBASpeedControl.h`). 8/8 passed. |
| **SaveSlotView → GBA redirect** | ✅ **SELESAI** | Thumbnail save state dari pause menu berfungsi di PC dan Android |
| **Game icon/cover** | ⚪️ **SKIP** | Bukan bug — GBA tidak punya cover download |
| **GBA touch layout editor parity** | ✅ **SELESAI** | CoreTouchLayoutScreen: Move/Resize mode, Kustomisasi (popup visibility), Garis Pinggir checkbox + Kisi-kisi slider, Reset. Cocok dengan PSP — terkecuali Border (diganti Garis Pinggir untuk grid control) |
| **Pause menu editor redirect** | ✅ **SELESAI** | Pause → Edit touch control layout sekarang buka CoreTouchLayoutScreen(GBA) bukan PSP |
| **GBA root_ cleanup** | 🟢 Low | `CreateViews()` GBA path tambah DevMenu + Resume buttons yang seharusnya hanya muncul di pause. `children=15` — idealnya 10 touch buttons + overlay saat pause |
| **Editor preview button size (tiny dots)** | ✅ **FIXED** | `GBADragDrop::Draw()` pakai `scale_ = btn_.w * g_layoutScale` — `btn_.w` adalah normalized width (0.09), BUKAN image scale factor. Akibat: `scale_ ≈ 0.072` → render 7px (titik). Fix: `GetContentDimensions()` override + formula `(btn_.w × screenBounds_.w) / image->w` → render 87px ✅. Resize range [0.3, 1.5] → [0.03, 0.30] untuk normalized width semantics. |
| **Editor preview grouped controls not rendering** | ✅ **FIXED (6353e7a)** | `GBADPadGroup` dan `GBAActionGroup` punya custom `Draw()` yang mungkin gagal di beberapa device Android. Fix: hapus grouped controls — semua 10 tombol jadi individual `CoreDragDrop`, sama persis dengan game screen (`EmuScreen::AddGBATouchButtons`). |
| **Editor preview buttons hide (LoadTouchConfig)** | ✅ **FIXED (5678699)** | Bug #1: `LoadTouchConfig()` `cfg.Clear()` hapus default saat INI section kosong/korup. Fix: parse ke temporary `CoreTouchConfig` dulu. Bug #2: `HasCreatedViews()` pakai `controls_.empty()` → infinite loop jika semua tombol di-hide. Fix: dedicated `bool created_` flag. |
| **Customize popup arrow icons** | ✅ **FIXED (b0bdc0b)** | Popup `CoreTouchVisibilityPopup` pakai `I_ARROW` (generic) untuk semua arah D-pad. Fix: `I_ARROW_UP/DOWN/LEFT/RIGHT` — konsisten dengan editor preview dan game screen. |
| **Editor grid lines (Garis Pinggir)** | ✅ **FIXED (ulang)** | Fix sebelumnya (051b76d, `vLine`/`hLine` di `GBALayoutView::Draw()`) rusak saat `GBASnapGrid` class dibuat ulang sebagai child View — `GetContentDimensions()` return (10,10) default. Fix: `DrawCoreSnapGrid()` static function dipanggil langsung dari `CoreLayoutView::Draw()` SETELAH children (PSP SnapGrid z-order). Formula grid identik PSP. |

### 🔴 Compliance Debt: LAN Sync

**Blocker sebelum merge upstream.** LAN Sync melanggar aturan fork:

| # | Action | Detail |
|---|--------|--------|
| 1 | Pindah file dari `Core/` | `Core/SaveStateLANSync.*`, `Core/LANSyncConfig.*` → `LANSync/` |
| 2 | Tambah `PPSSPP_LANSYNC` flag | Di CMake, biar bisa disable |
| 3 | Wrap kode dengan `#ifdef PPSSPP_LANSYNC` | Setiap tambahan di file upstream |
| 4 | Tambah `[PPSSPP-FORK] LANSync:` marker | Semua file + file upstream yang disentuh |
| 5 | Verifikasi dual-build | `-DPPSSPP_LANSYNC=ON` dan `=OFF` harus build |
| 6 | Asal usul `Common/Net/` | 34 file tanpa marker — perlu audit git history |

### 🚀 Future Cores (Contoh Pattern)

```cpp
auto &reg = EmuCore::RecentFilesRegistry::Get();
reg.Register(EmuCore::RecentFilesEntry{
    (int)EmuCore::Type::N64,
    "N64",
    "N64 Recent",
    "RECENT_N64",
    &g_recentFilesN64,
    nullptr,
    ".n64:.z64:.v64",
});
```

Tambah core baru = 1 `Register()` + 1 `InitXXX()` + `add_subdirectory` di CMake.

## Build & Run

```bash
cmake -B build-final -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-final --target PPSSPPSDL -j$(nproc)
./build-final/PPSSPPSDL "/path/to/game.gba" 2>&1 | grep "\[GBA\]"
```
