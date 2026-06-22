# GBA Settings Screen — Design Spec

**Date:** 2026-06-22
**Status:** Draft
**Branch:** `feature/lan-sync`

## 1. Tujuan

Menyediakan screen setting khusus untuk GBA mode yang hanya menampilkan opsi relevan:
Control mapping, display, dan audio. Tidak mengganggu PSP settings screen yang sudah ada.

### 1.1 Success Criteria

- Saat GBA game di-pause, tombol "GBA Settings" muncul (bukan "Settings" PSP)
- GBA Settings screen hanya tampilkan 3 section: Controls, Display, Audio
- Setting tersimpan di `[GBA]` section `ppsspp.ini`
- PSP path不改 — zero breaking change

## 2. Arsitektur

### 2.1 File Baru

| File | Isi |
|------|-----|
| `UI/GBASettingsScreen.h` | Class `GBASettingsScreen : public UIDialogScreen` |
| `UI/GBASettingsScreen.cpp` | `CreateViews()` dengan 3 CollapsibleSection |

### 2.2 Hook (di file upstream)

| File | Perubahan |
|------|-----------|
| `UI/PauseScreen.cpp` | +1 Choice "GBA Settings" saat `g_gbaModeActive` (di luar blok PSP) |

### 2.3 Class Hierarchy

```
UIDialogScreen          (Common/UI/UIScreen.h)
  └─ GBASettingsScreen  (UI/GBASettingsScreen.h)
       └─ CreateViews()
            ├─ CollapsibleSection "Controls"
            │    └─ Choice "Control Mapping" → push ControlMappingScreen
            ├─ CollapsibleSection "Display"
            │    ├─ Dropdown: Aspect Ratio
            │    ├─ Dropdown: Filter
            │    └─ Checkbox: Integer Scaling
            ├─ CollapsibleSection "Audio"
            │    └─ Slider: Volume
            └─ Choice "OK" → TriggerFinish(DR_OK)
```

## 3. Layout

```
┌─────────────────────────────────┐
│         GBA Settings            │
├─────────────────────────────────┤
│                                 │
│  ▼ CONTROLS                     │
│    [Control Mapping]            │ → push ControlMappingScreen
│                                 │
│  ▼ DISPLAY                      │
│    Aspect Ratio: [3:2 ▼]        │
│    Filter:       [Nearest ▼]    │
│    Integer Scale: [OFF     ]    │
│                                 │
│  ▼ AUDIO                        │
│    Volume:       ───●───        │
│                                 │
│  [       OK        ]            │
└─────────────────────────────────┘
```

## 4. Data Flow

### 4.1 Config Keys

Semua setting disimpan di `[GBA]` section `ppsspp.ini`:

| Setting | Key | Tipe | Default | Widget |
|---------|-----|------|---------|--------|
| Aspect Ratio | `iGBAAspectRatio` | int (0-3) | 0 (3:2) | Dropdown |
| Filter | `iGBATexFiltering` | int (0-1) | 1 (Linear) | Dropdown |
| Integer Scaling | `bGBAIntegerScaling` | bool | false | Checkbox |
| Volume | `fGBAVolume` | float | 1.0 | Slider |

### 4.2 Save/Load

- `GBASettingsScreen::CreateViews()` baca dari `g_Config` (existing `[GBA]` section)
- Saat OK ditekan, tulis ke `g_Config` + `g_Config.Save()`

### 4.3 Display Pipeline

```
Aspect Ratio + Integer Scaling → GBACore::Render() → Thin3D quad
```

Saat ini GBA render pakai fixed 3:2 aspect. Setting baru akan:

- Mengubah `Draw::Rect` uv/pos calculation di `GBACore::Render()`
- Integer scaling: round up viewport ke multiple pixel sempurna

## 5. PSP Path (Zero Breaking Change)

```cpp
// PauseScreen.cpp — hanya tambah, tidak ubah existing
if (g_gbaModeActive) {
    rightColumnItems->Add(new Choice(pa->T("GBA Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGBASettings);
} else {
    // PSP: existing code — tidak disentuh
}
```

## 6. File yang Diubah

| File | Perubahan | Baris |
|------|-----------|-------|
| `UI/GBASettingsScreen.h` | **BARU** | ~30 |
| `UI/GBASettingsScreen.cpp` | **BARU** | ~150 |
| `UI/PauseScreen.h` | +1 method `OnGBASettings` | +3 |
| `UI/PauseScreen.cpp` | +1 Choice di CreateViews (ada guard) | +10 |
| `EmuCore/GBACore.cpp` | Update render untuk aspect ratio + integer scaling | ~20 |
| `CMakeLists.txt` | Tambah file baru | +1 |

## 7. Risiko & Mitigasi

| Risiko | Mitigasi |
|--------|----------|
| Config key bentrok dengan PSP | Awali dengan prefix `GBA` (`iGBAAspectRatio`, bukan `iAspectRatio`) |
| Render broken setelah aspect ratio change | Reset pipeline di `DeviceRestored()` — lazy init |
| PauseScreen conflict saat merge upstream | Hook minimal, satu blok `#ifdef PPSSPP_MULTICORE` + komentar `[PPSSPP-FORK]` |

## 8. Testing

- ✅ Build MULTICORE=ON — GBASettingsScreen muncul di GBA pause
- ✅ Build MULTICORE=OFF — GBASettingsScreen tidak dikompilasi, PSP path不改
- ✅ Setting display berubah (3:2 ↔ 16:9 ↔ 1:1 ↔ stretch)
- ✅ Volume slider berfungsi
- ✅ Setting persist setelah restart PPSSPP
