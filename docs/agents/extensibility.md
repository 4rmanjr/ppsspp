# Extensibility Architecture — Adding New Emulators

> **Referenced by:** `AGENTS.md`
> **Applies to:** N64, PS1, NDS, SNES, and all future emulators
> **Purpose:** Zero duplication, consistent PSP parity, each core addable without modifying other core files.

---

## Table of Contents

- [Core Principles](#-core-principles)
- [Component Architecture](#-component-architecture)
- [CoreButtonRegistry](#-corebuttonregistry-per-core-button-definitions)
- [EmuScreen Core Dispatch](#-emuscreen-core-dispatch-must-use-switch-not-binary)
- [Generic Touch Classes](#-generic-touch-classes)
- [Directory Resolver](#-directory-file-path-per-core)
- [Step-by-Step: Adding a New Emulator](#-step-by-step-adding-a-new-emulator)
- [Zero Duplication Rule](#-zero-duplication-rule)
- [262 GBA References — Path to Generification](#-262-gba-references--path-to-generification)

---

## 🔷 Core Principles

1. **Core-agnostic classes** — All new classes MUST use the `Core` prefix (e.g., `CoreDragDrop`), **not** `GBA`/`N64`/`PS1`. Existing GBA-specific classes will be refactored incrementally (see roadmap).
2. **Per-core definition** — Only **data** varies per core (button list, default layout, image IDs). What's **shared** (renderer, popup, grid, drag-drop) must be generic.
3. **PSP parity REQUIRED** — Every new core MUST pass the same [Quality Gate #1](quality-gates.md#-quality-gate-1-feature-parity-with-psp) as GBA.
4. **Shared system buttons** — Fast-forward and Pause are created AUTOMATICALLY for all non-PSP cores.
5. **No binary routing** — `if (coreType_ != PSP) { ... }` is **FORBIDDEN** for new code. MUST use `switch(coreType_)`.

---

## 🔷 Component Architecture

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
│                                Reads button map from registry, not hardcoded switch
├── EmuScreen.cpp             → Core dispatch via switch(coreType_)
└── GamepadEmu.cpp            → CreateSystemTouchButtons() for all cores
```

---

## 🗂️ CoreButtonRegistry — Per-Core Button Definitions

REPLACE hardcoded `case CTRL_*` switches with a registry:

```cpp
// EmuCore/Config.h
struct CoreButtonDef {
    int keyCode;           // unique per core (e.g. N64_A, PS1_CROSS)
    const char *imageID;   // atlas image (e.g. "I_N64_A", "I_PS1_CROSS")
    const char *label;     // display name (e.g. "A", "Cross")
    enum BgType { ROUND, RECT, SHOULDER, ANALOG, CUSTOM };
    BgType bgType;
    const char *bgImageID; // atlas image for background (e.g. "I_ROUND", "I_N64_C")
};

// Per-core registration (C++17 — use const reference, not std::span)
void RegisterButtonMap(EmuCore::Type type, const std::vector<CoreButtonDef> &buttons);
const CoreButtonDef *GetButtonDef(EmuCore::Type type, int keyCode);
const std::vector<CoreButtonDef> &GetButtonDefs(EmuCore::Type type);
```

**REQUIRED:**
- [ ] Each core registers its button map in `InitDefaultTouchConfigs()`
- [ ] `CoreLayoutView::CreateViews()` reads from `GetButtonDefs(coreType_)`
- [ ] `CoreTouchVisibilityScreen` iterates `GetButtonDefs(coreType_)`
- [ ] No `case CTRL_*` in renderer/popup classes — all data-driven

**The only place for per-core hardcoded data is `EmuCore/Config.cpp`** (button map registration + default layout).

---

## 🔷 EmuScreen Core Dispatch — Must Use switch, Not Binary

**FORBIDDEN:**
```cpp
// ⛔ FORBIDDEN — binary routing, not extensible
if (coreType_ != EmuCore::Type::PSP) {
    // GBA-specific (N64 can't enter here)
}
```

**REQUIRED:**
```cpp
// ✅ REQUIRED — switch dispatch
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

**Shared system buttons** — Fast-forward + Pause called AFTER dispatch, for ALL non-PSP cores:
```cpp
if (coreType_ != EmuCore::Type::PSP) {
    CreateSystemTouchButtons(root_, bounds, deviceOrientation);
}
```

---

## 🔷 Generic Touch Classes

| Current Class (GBA-specific) | New Class (Generic) |
|------------------------------|----------------------|
| `GBADragDrop` | `CoreDragDrop` — reads button def from registry, computes scale from `buttonDef.imageID` |
| `GBASnapGrid` | `CoreSnapGrid` — already generic (no GBA reference ✅) |
| `GBALayoutView` | `CoreLayoutView` — `CreateViews()` iterates `GetButtonDefs(coreType_)` |
| `GBACheckBoxChoice` | `CoreCheckBoxChoice` — already generic ✅ (wrapper only) |
| `GBATouchVisibilityPopup` | `CoreTouchVisibilityScreen` — title from `EmuCore::GetConfigSection(coreType_)`, button rows from `GetButtonDefs(coreType_)` |

**Rules:**
- [ ] Every new class in `CoreTouchLayoutScreen.cpp` MUST use the `Core` prefix, not a core-specific name
- [ ] All per-core data (button def, image ID, label) MUST come from the registry, not hardcoded

---

## 🔷 Directory & File Path Per Core

**FORBIDDEN:**
```cpp
// ⛔ FORBIDDEN — hardcoded path
File::CreateFullPath(GetSysDirectory(DIRECTORY_SAVEDATA) / "GBA");
```

**REQUIRED:**
```cpp
// ✅ REQUIRED — resolver
std::string GetCoreSaveDir(EmuCore::Type type) {
    switch (type) {
    case EmuCore::Type::GBA: return "GBA";
    case EmuCore::Type::N64: return "N64";
    case EmuCore::Type::PS1: return "PS1";
    default: return "";
    }
}
```

Resolver REQUIRED for:
- `DIRECTORY_SAVEDATA / <core>` — save files
- `DIRECTORY_SAVESTATE / <core>` — save states
- `g_gbaSavePrefix` → `GetCoreSavePrefix(type)`
- `[GBA ControlLayout]` → `[<Core> ControlLayout]` (via config section resolver)

---

## 🔷 Step-by-Step: Adding a New Emulator

Use this checklist EVERY time you add a new core:

```
## Adding New Core: <Core Name>

### [Phase 0] Preparation
- [ ] Add `PPSSPP_<CORE>` flag to CMakeLists.txt
- [ ] Add `EmuCore::Type::<CORE>` to EmuCore/EmuCore.h
- [ ] Add `DetectType()` + `Create()` to EmuCore/EmuCore.cpp
- [ ] Create `<Core>Core.h/.cpp` (Core interface implementation)
- [ ] Update `EmuCore/CMakeLists.txt`

### [Phase 1] Default Layout
- [ ] Register button map via RegisterButtonMap() in EmuCore/Config.cpp
- [ ] Define default touch layout (positions + sizes)
- [ ] Isolate config section: [<Core> ControlLayout]
- [ ] Update InitDefaultTouchConfigs()

### [Phase 2] EmuScreen Integration
- [ ] Add case to switch(coreType_) dispatch
- [ ] Create Create<Core>TouchLayout() — uses CoreLayoutView
- [ ] System buttons automatic (CreateSystemTouchButtons)
- [ ] Update Is<Core>() pattern (constexpr fallback)
- [ ] Update Init/Update/Shutdown routing

### [Phase 3] Quality Gate #1 — PSP Parity
- [ ] Identify PSP equivalent feature for every aspect
- [ ] Feature Parity Checklist (toggle all, save on exit, visibility icons, etc.)
- [ ] Filter PSP-specific logic
- [ ] Log mismatches to PSP Knowledge Base

### [Phase 4] Quality Gate #2 — Code Review
- [ ] Unified diff review (zero upstream change)
- [ ] Edge cases (bounds=0, div-by-zero, null config)
- [ ] Compile ON ✅ | OFF ✅
- [ ] [PPSSPP-FORK] markers — all new files
- [ ] Post-commit diff verification
```

---

## 🔷 Zero Duplication Rule

> **Every new core addition:**
> ✅ Only adds: `enum Type`, `DetectType`, `Factory`, `CoreFile`, `ButtonMap`, `DefaultLayout`
> ❌ Does NOT modify: generic classes (`CoreDragDrop`, `CoreLayoutView`, `CoreSnapGrid`, `CoreCheckBoxChoice`, `CoreTouchVisibilityScreen`)
> ❌ Does NOT duplicate: system buttons, popup rendering, grid drawing, drag-drop logic
> ❌ Does NOT need new switch in: layout view, visibility popup, grid renderer

---

## 🔷 262 GBA References — Path to Generification

Currently ~262 GBA-specific references in the codebase. Incremental refactoring target:

| Phase | Target | File | Status |
|-------|--------|------|--------|
| 1 | Class rename | `GBADragDrop` → `CoreDragDrop` | ✅ Done |
| 2 | Class rename | `GBALayoutView` → `CoreLayoutView` | ✅ Done |
| 3 | Class rename | `GBATouchVisibilityPopup` → `CoreTouchVisibilityScreen` | ✅ Done |
| 4 | Switch to registry | Hardcoded `case CTRL_*` → `GetButtonDefs(coreType_)` | ✅ Done |
| 5 | System buttons | Inline → `CreateSystemTouchButtons()` | ✅ Done |
| 6 | Binary routing | `if (coreType_ != PSP)` → `switch(coreType_)` | ✅ Done |
| 7 | Hardcoded paths | String literal → `GetCoreDirectory(type)` | ✅ Done |
| 8 | GBA-prefixed methods | `AddGBATouchButtons` → `AddCoreTouchButtons` | ✅ Done |

Each phase REQUIRED:
- ✅ Build ON + OFF
- ✅ Zero upstream change
- ✅ PSP parity verified
