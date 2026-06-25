# Extensibility Architecture — Menambah Emulator Baru

> **Referenced by:** `AGENTS.md`
> **Applies to:** N64, PS1, NDS, SNES, dan semua emulator masa depan
> **Tujuan:** Zero duplication, PSP parity konsisten, tiap core bisa di-add tanpa mengubah file core lain.

---

## Daftar Isi

- [Prinsip Dasar](#-prinsip-dasar)
- [Arsitektur Component](#-arsitektur-component)
- [CoreButtonRegistry](#️-corebuttonregistry--definisi-tombol-per-core)
- [EmuScreen Core Dispatch](#-emuscreen-core-dispatch--wajib-switch-bukan-binary)
- [Generic Touch Classes](#-generic-touch-classes)
- [Directory Resolver](#-directory--file-path-per-core)
- [Step-by-Step: Menambah Emulator Baru](#-step-by-step-menambah-emulator-baru)
- [Zero Duplication Rule](#-zero-duplication-rule)
- [Generification Roadmap](#-262-gba-references--path-to-generification)

---

## 🔷 Prinsip Dasar

1. **Core-agnostic classes** — Semua class baru WAJIB pakai prefix `Core` (misal `CoreDragDrop`), **bukan** `GBA`/`N64`/`PS1`. GBA-specific classes yang sudah ada akan di-refactor bertahap (lihat roadmap).
2. **Per-core definition** — Yang berbeda per core hanyalah **data** (button list, default layout, image IDs). Yang **sama** (renderer, popup, grid, drag-drop) harus generic.
3. **PSP parity WAJIB** — Setiap core baru WAJIB melewati [Quality Gate #1](quality-gates.md#-quality-gate-1--feature-parity-dengan-psp) yang sama seperti GBA.
4. **Shared system buttons** — Fast-forward dan Pause dibuat OTOMATIS untuk semua non-PSP core.
5. **No binary routing** — `if (coreType_ != PSP) { ... }` **DILARANG** untuk kode baru. WAJIB pakai `switch(coreType_)`.

---

## 🔷 Arsitektur Component

```
EmuCore/
├── EmuCore.h                 → Type enum (PSP, GBA, N64, PS1, ...)
├── EmuCore.cpp               → DetectType() + Factory
├── Config.h                  → TouchConfig loader/saver (generic)
├── Config.cpp                → InitDefaultTouchConfigs() + CoreButtonRegistry
├── GBACore.h/.cpp            → GBA-specific (lifecycle, render, audio)
├── N64Core.h/.cpp            → (future) N64-specific
├── PS1Core.h/.cpp            → (future) PS1-specific
└── ...

UI/
├── CoreTouchLayoutScreen.cpp → Generic layout editor (CoreDragDrop, CoreLayoutView)
│                                Baca button map dari registry, bukan hardcoded switch
├── EmuScreen.cpp             → Core dispatch via switch(coreType_)
└── GamepadEmu.cpp            → CreateSystemTouchButtons() untuk semua core
```

---

## 🗂️ CoreButtonRegistry — Definisi Tombol Per Core

GANTI hardcoded `case CTRL_*` switches dengan registry:

```cpp
// EmuCore/Config.h
struct CoreButtonDef {
    int keyCode;           // unique per core (e.g. N64_A, PS1_CROSS)
    const char *imageID;   // atlas image (e.g. "I_N64_A", "I_PS1_CROSS")
    const char *label;     // display name (e.g. "A", "Cross")
    enum BgType { ROUND, RECT, SHOULDER, ANALOG, CUSTOM };
    BgType bgType;
    const char *bgImageID; // atlas image untuk background (e.g. "I_ROUND", "I_N64_C")
};

// Per-core registration
void RegisterButtonMap(EmuCore::Type type, std::span<const CoreButtonDef> buttons);
const CoreButtonDef *GetButtonDef(EmuCore::Type type, int keyCode);
std::span<const CoreButtonDef> GetButtonDefs(EmuCore::Type type);
```

**WAJIB:**
- [ ] Tiap core daftarkan button map di `InitDefaultTouchConfigs()`
- [ ] `CoreLayoutView::CreateViews()` baca dari `GetButtonDefs(coreType_)`
- [ ] `CoreTouchVisibilityPopup` iterasi `GetButtonDefs(coreType_)`
- [ ] Tidak boleh ada `case CTRL_*` di class renderer/popup — semua data-driven

**Satu-satunya tempat hardcoded per-core adalah di `EmuCore/Config.cpp`** (registrasi button map + default layout).

---

## 🔷 EmuScreen Core Dispatch — Wajib switch, bukan binary

**LARANG:**
```cpp
// ⛔ DILARANG — binary routing, tidak extensible
if (coreType_ != EmuCore::Type::PSP) {
    // GBA-specific (N64 gak bisa masuk sini)
}
```

**WAJIB:**
```cpp
// ✅ WAJIB — switch dispatch
switch (coreType_) {
#ifdef PPSSPP_GBA
case EmuCore::Type::GBA:
    CreateGBATouchLayout(bounds, orientation);
    break;
#endif
#ifdef PPSSPP_N64
case EmuCore::Type::N64:
    CreateN64TouchLayout(bounds, orientation);
    break;
#endif
// ...
default:
    root_ = CreatePadLayout(touch, bounds.w, bounds.h, &pauseTrigger_, &g_controlMapper);
    break;
}
```

**Shared system buttons** — Fast-forward + Pause dipanggil SETELAH dispatch, untuk SEMUA non-PSP core:
```cpp
if (coreType_ != EmuCore::Type::PSP) {
    CreateSystemTouchButtons(root_, bounds, deviceOrientation);
}
```

---

## 🔷 Generic Touch Classes

| Class Saat Ini (GBA-specific) | Class Baru (Generic) |
|------------------------------|----------------------|
| `GBADragDrop` | `CoreDragDrop` — baca button def dari registry, hitung scale dari `buttonDef.imageID` |
| `GBASnapGrid` | `CoreSnapGrid` — sudah generic (tidak ada GBA reference ✅) |
| `GBALayoutView` | `CoreLayoutView` — `CreateViews()` iterasi `GetButtonDefs(coreType_)` |
| `GBACheckBoxChoice` | `CoreCheckBoxChoice` — sudah generic ✅ (hanya wrapper) |
| `GBATouchVisibilityPopup` | `CoreTouchVisibilityPopup` — title dari `EmuCore::GetConfigSection(coreType_)`, button rows dari `GetButtonDefs(coreType_)` |

**Aturan:**
- [ ] Setiap class baru di `CoreTouchLayoutScreen.cpp` WAJIB pakai prefix `Core`, bukan nama core spesifik
- [ ] Semua data per-core (button def, image ID, label) WAJIB dari registry, bukan hardcoded

---

## 🔷 Directory & File Path Per Core

**LARANG:**
```cpp
// ⛔ DILARANG — hardcoded path
File::CreateFullPath(GetSysDirectory(DIRECTORY_SAVEDATA) / "GBA");
```

**WAJIB:**
```cpp
// ✅ WAJIB — resolver
std::string GetCoreSaveDir(EmuCore::Type type) {
    switch (type) {
    case EmuCore::Type::GBA: return "GBA";
    case EmuCore::Type::N64: return "N64";
    case EmuCore::Type::PS1: return "PS1";
    default: return "";
    }
}
```

Resolver WAJIB untuk:
- `DIRECTORY_SAVEDATA / <core>` — save files
- `DIRECTORY_SAVESTATE / <core>` — save states
- `g_gbaSavePrefix` → `GetCoreSavePrefix(type)`
- `[GBA ControlLayout]` → `[<Core> ControlLayout]` (via config section resolver)

---

## 🔷 Step-by-Step: Menambah Emulator Baru

Gunakan checklist ini SETIAP kali menambah core baru:

```
## Menambah Core Baru: <Nama Core>

### [Phase 0] Persiapan
- [ ] Tambah `PPSSPP_<CORE>` flag di CMakeLists.txt
- [ ] Tambah `EmuCore::Type::<CORE>` di EmuCore/EmuCore.h
- [ ] Tambah `DetectType()` + `Create()` di EmuCore/EmuCore.cpp
- [ ] Buat `<Core>Core.h/.cpp` (implementasi Core interface)
- [ ] Update `EmuCore/CMakeLists.txt`

### [Phase 1] Default Layout
- [ ] Daftarkan button map via RegisterButtonMap() di EmuCore/Config.cpp
- [ ] Definisikan default touch layout (positions + sizes)
- [ ] Isolasi config section: [<Core> ControlLayout]
- [ ] Update InitDefaultTouchConfigs()

### [Phase 2] EmuScreen Integration
- [ ] Tambah case di switch(coreType_) dispatch
- [ ] Buat Create<Core>TouchLayout() — panggil CoreLayoutView
- [ ] System buttons otomatis (CreateSystemTouchButtons)
- [ ] Update Is<Core>() pattern (constexpr fallback)
- [ ] Update Init/Update/Shutdown routing

### [Phase 3] Quality Gate #1 — PSP Parity
- [ ] Identifikasi PSP equivalent feature untuk setiap aspek
- [ ] Feature Parity Checklist (toggle all, save on exit, visibility icons, dll)
- [ ] Filter PSP-specific logic
- [ ] Catat mismatch ke PSP Knowledge Base

### [Phase 4] Quality Gate #2 — Code Review
- [ ] Unified diff review (zero upstream change)
- [ ] Edge cases (bounds=0, div-by-zero, null config)
- [ ] Compile ON ✅ | OFF ✅
- [ ] [PPSSPP-FORK] markers — semua file baru
- [ ] Post-commit diff verification
```

---

## 🔷 Zero Duplication Rule

> **Setiap tambahan core baru:**
> ✅ Hanya tambah: `enum Type`, `DetectType`, `Factory`, `CoreFile`, `ButtonMap`, `DefaultLayout`
> ❌ TIDAK perlu ubah: generic classes (`CoreDragDrop`, `CoreLayoutView`, `CoreSnapGrid`, `CoreCheckBoxChoice`, `CoreTouchVisibilityPopup`)
> ❌ TIDAK perlu duplikasi: system buttons, popup rendering, grid drawing, drag-drop logic
> ❌ TIDAK perlu switch baru di: layout view, visibility popup, grid renderer

---

## 🔷 262 GBA References → Path to Generification

Saat ini ada ~262 reference GBA-specific di codebase. Target refactor bertahap:

| Phase | Target | File |
|-------|--------|------|
| 1 | Class rename | `GBADragDrop` → `CoreDragDrop` |
| 2 | Class rename | `GBALayoutView` → `CoreLayoutView` |
| 3 | Class rename | `GBATouchVisibilityPopup` → `CoreTouchVisibilityPopup` |
| 4 | Switch ke registry | Hardcoded `case CTRL_*` → `GetButtonDefs(coreType_)` |
| 5 | System buttons | Inline → `CreateSystemTouchButtons()` |
| 6 | Binary routing | `if (coreType_ != PSP)` → `switch(coreType_)` |
| 7 | Hardcoded paths | String literal → `GetCoreDirectory(type)` |
| 8 | GBA-prefixed methods | `AddGBATouchButtons` → `AddCoreTouchButtons` |

Setiap phase WAJIB:
- ✅ Build ON + OFF
- ✅ Zero upstream change
- ✅ PSP parity verified
