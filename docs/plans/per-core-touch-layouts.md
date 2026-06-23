# Plan: Per-Core Virtual Gamepad + Customizable Touch Layouts

**Date:** 2026-06-23
**Status:** 🔴 Belum dimulai

## Problem

- PSP touch layout: customizable via `TouchControlLayoutScreen`, stored in `g_Config`
- GBA touch layout: **hardcoded** in `TouchLayoutGBA::GetLayout()`, gak bisa di-customize
- Nanti core baru (N64, PS1) juga butuh layout sendiri
- Kode saat ini pake `PSPButton` langsung di GBA, bukan lewat config system

## Design

Setiap core punya **isolated touch config** yang disimpan di section INI masing-masing.
`TouchControlLayoutScreen` jadi generic — bisa edit layout core mana pun.

```
g_Config.TouchControlsLandscape[PSP]  → [ControlLayout] section
g_Config.TouchControlsPortrait[PSP]   → [ControlLayoutPortrait] section
g_Config.TouchControlsLandscape[GBA]  → [GBA ControlLayout] section
g_Config.TouchControlsPortrait[GBA]   → [GBA ControlLayoutPortrait] section
```

Zero breaking change: PSP config tetap utuh di section `[ControlLayout]`.
Core baru simpan di section `[<CoreName> ControlLayout]`.

---

## Tasks

### Phase 1: Config — Per-Core Touch Storage

- [ ] **1.1: Add per-core TouchControlConfig struct**
  - `EmuCore/Config.h`: tambah `TouchConfig` struct per core type
  - Pake array/index-based biar gak perlu N field
  - ```cpp
    struct CoreTouchConfig {
        TouchControlConfig landscape;
        TouchControlConfig portrait;
        ConfigCustomButton customButtons[CUSTOM_BUTTON_COUNT];
    };
    CoreTouchConfig touchControls[(int)Type::COUNT];
    ```
  - **Files:** `EmuCore/Config.h`, `EmuCore/Config.cpp`

- [ ] **1.2: Add load/save per-core touch section**
  - PSP: tetap `[ControlLayout]` + `[ControlLayoutPortrait]` (zero break)
  - GBA: `[GBA ControlLayout]` + `[GBA ControlLayoutPortrait]`
  - Future core: `[<CoreName> ControlLayout]` + `[<CoreName> ControlLayoutPortrait]`
  - **Files:** `EmuCore/Config.cpp`

- [ ] **1.3: Add GetTouchControlsConfig(coreType, orientation)**
  - Replace PSP-only `g_Config.GetTouchControlsConfig(orientation)` dengan per-core version
  - ```cpp
    TouchControlConfig &GetTouchControlsConfig(Type core, DeviceOrientation orientation);
    // Fallback: kalo core ga punya config, return PSP default
    ```
  - **Files:** `EmuCore/Config.h`, `EmuCore/Config.cpp`

### Phase 2: Default Layouts → Config-Based

- [ ] **2.1: Migrate GBA hardcoded layout to default config**
  - `TouchLayoutGBA::GetLayout()` → pindah jadi default values di `Config.cpp`
  - Saat first-run: populate `[GBA ControlLayout]` dari default GBA layout
  - **Files:** `UI/TouchLayoutGBA.cpp`, `EmuCore/Config.cpp`

- [ ] **2.2: Migrate CreateViews() to use config-based layout**
  - `EmuScreen::CreateViews()` panggil `GetTouchControlsConfig(coreType_, orientation)`
  - GBA: `AddGBATouchButtons()` baca dari config, bukan dari `GetLayout()`
  - PSP: `CreatePadLayout()` tetap sama (config-nya udah ada)
  - **Files:** `UI/EmuScreen.cpp`

- [ ] **2.3: Remove hardcoded TouchLayoutGBA (opsional)**
  - Kalo semua button position udah di config, `GetLayout()` bisa dihapus
  - Tapi keep `TouchLayoutGBA.h` untuk forward compat
  - **Files:** `UI/TouchLayoutGBA.cpp`

### Phase 3: Generic TouchControlLayoutScreen

- [ ] **3.1: Make TouchControlLayoutScreen core-aware**
  - Current: edit PSP layout only
  - New: accept core type param, edit that core's layout
  - ```cpp
    class TouchControlLayoutScreen {
        TouchControlLayoutScreen(EmuCore::Type coreType);
        // ...
    };
    ```
  - **Files:** `UI/TouchControlLayoutScreen.cpp`, `UI/TouchControlLayoutScreen.h`

- [ ] **3.2: Add "Customize Touch Controls" to GBA Settings Screen tab Controls**
  - Pause menu → GBA Settings → tab Controls → button "Customize On-Screen Controls"
  - Opens `TouchControlLayoutScreen(EmuCore::Type::GBA)`
  - UI Flow:
    ```
    GBA mode → pause → GBA Settings → tab Controls
      ├── Button Mapping (VIRTKEY_GBA_*)        ← existing
      └── [Customize On-Screen Controls]        ← NEW button
            → TouchControlLayoutScreen(GBA)
              → drag/resize GBA buttons (A, B, D-Pad, L, R, Start, Select)
              → save ke [GBA ControlLayout] section
    ```
  - **Files:** `UI/GBASettingsScreen.cpp`

- [ ] **3.3: Verify PSP touch layout masih sama**
  - `TouchControlLayoutScreen(EmuCore::Type::PSP)` = same behavior as before
  - Load/save ke `[ControlLayout]` section yang sama
  - **Files:** `UI/TouchControlLayoutScreen.cpp`

### Phase 4: Template for Future Cores

- [ ] **4.1: Document per-core touch pattern in multi-core-development.md**
  - Tambah section "Per-Core Touch Layout" dengan contoh
  - **Files:** `docs/agents/multi-core-development.md`

- [ ] **4.2: Create CoreTouchTemplate default**
  - Helper function: `GetDefaultTouchLayout(Type core, bool portrait)`
  - Return default buttons for that core
  - **Files:** `EmuCore/Config.cpp` or new `EmuCore/TouchLayoutDefaults.cpp`

---

## Architecture

### Config Storage

```ini
; PSP (existing — zero change)
[ControlLayout]
button0=0,0.82,0.47,0.09,0.09,18
...

[ControlLayoutPortrait]
button0=0,0.80,0.62,0.11,0.10,18
...

; GBA (baru)
[GBA ControlLayout]
button0=0,0.82,0.47,0.09,0.09,18  ; A button (CTRL_CROSS)
button1=1,0.73,0.38,0.09,0.09,18  ; B button (CTRL_CIRCLE)
...

[GBA ControlLayoutPortrait]
button0=0,0.80,0.62,0.11,0.10,18
...

; Future core: N64
[N64 ControlLayout]
...
```

### Code Flow

```
EmuScreen::CreateViews()
  → config = GetTouchControlsConfig(coreType_, orientation)
  → if coreType_ == PSP:
      CreatePadLayout(config, ...)
  → else:
      AddCoreTouchButtons(config, coreType_)
```

`AddCoreTouchButtons()` generic — iterasi config buttons, buat PSPButton per entry.
No per-core hardcoded layout in C++.

### Key Classes

```
EmuCore::Config:
  - GetTouchControlsConfig(Type, Orientation) → TouchControlConfig&
  - LoadTouchConfig(Type)  — load from INI section
  - SaveTouchConfig(Type)  — save to INI section

TouchControlLayoutScreen:
  - Now takes Type param
  - Edit layout for that core

TouchLayoutGBA:  (deprecated after migration)
  - Keep GetLayout() as default-generator only
```

---

## Non-Breaking Guarantees

| Aspek | Before | After |
|-------|--------|-------|
| PSP `[ControlLayout]` INI | ✅ | ✅ Sama (zero break) |
| PSP touch screen | ✅ | ✅ `GetTouchControlsConfig(PSP, ...)` return same data |
| GBA touch layout | ✅ hardcoded | ✅ dari config (default = same) |
| GBA Settings → customize touch | ❌ | ✅ baru |
| Keyboard VIRTKEY_GBA binding | ✅ | ✅ unchanged |
| Core template | ❌ | ✅ documented |

---

## Progress Tracking

```
Phase 1: Config    [███] 3/3  ✅  CoreTouchConfig + load/save via IniFile
Phase 2: Migrate   [███] 3/3  ✅  AddGBATouchButtons → GetTouchConfig(), LoadTouchConfig() in InitGBA
Phase 3: Editor    [███] 3/3  ✅  CoreTouchLayoutScreen + GBA Settings button + build files
Phase 4: Template  [▁▁] 0/2   🔴  Document + helper
────────────────────────
Total: 9/11 ✅ (Phase 4 tinggal doc)
```
