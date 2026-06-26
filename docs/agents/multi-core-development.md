# Multi-Core Development — Multi-Emulator Specific Rules

## Architecture

All emulator cores (other than PSP) use the **EmuCore abstraction layer**:

```
EmuCore/            → New folder, doesn't touch upstream
├── EmuCore.h       → Abstract interface for all cores
├── EmuCore.cpp     → Factory: Create() + DetectType()
├── PSPCore.h/.cpp  → PSP wrapper (delegates to existing system)
├── GBACore.h/.cpp  → GBA via libmgba
└── CMakeLists.txt
```

## Adding a New Emulator Core — Step-by-Step Guide

### Step 0: Naming Convention

| Aspect | Convention | Example (Core X) |
|--------|------------|------------------|
| Core ID | `UPPER` | `N64`, `PS1` |
| Core Name | `PascalCase` | `N64`, `PS1` |
| Feature flag | `PPSSPP_<ID>` | `PPSSPP_N64` |
| Type enum | `EmuCore::Type::<ID>` | `EmuCore::Type::N64` |
| File prefix | `<CoreName>Core` | `N64Core` |
| Namespace | `EmuCore` | `EmuCore` |
| Config section | `[<CoreName>]` | `[N64]` |
| Recent section | `[<CoreName> Recent]` | `[N64 Recent]` |
| VIRTKEY prefix | `VIRTKEY_<ID>_` | `VIRTKEY_N64_` |
| Log prefix | `[<ID>]` | `[N64]` |
| Save state ext | `.<coreabbv>st` | `.n64st` |
| Save state dir | `DIRECTORY_SAVESTATE / <CoreName> /` | `SAVESTATE/N64/` |
| Dev comment | `// [PPSSPP-FORK] <CoreName>:` | `// [PPSSPP-FORK] N64:` |

> **IMPORTANT:** Naming consistency enables automated verification (grep, script, CI).

### Step 1: Add enum Type

**File:** `EmuCore/EmuCore.h`

```cpp
namespace EmuCore {

enum class Type {
    PSP,
    GBA,
    N64,        // ← add here
};

}  // namespace EmuCore
```

### Step 2: Create core files

**File:** `EmuCore/<CoreName>Core.h`
**File:** `EmuCore/<CoreName>Core.cpp`

MUST follow the `EmuCore::Core` interface:

```cpp
// [PPSSPP-FORK] <CoreName>: <brief description>
// <Details>
#pragma once

#include "EmuCore/EmuCore.h"

namespace EmuCore {

class <CoreName>Core : public Core {
public:
    <CoreName>Core();
    ~<CoreName>Core() override;

    Type GetType() const override { return Type::<CoreName>; }

    // --- Lifecycle ---
    bool LoadROM(const Path &path) override;
    void RunFrame() override;
    void Reset() override;

    // --- Rendering ---
    void Render(Draw::DrawContext *draw) override;
    void DeviceLost() override;
    void DeviceRestored(Draw::DrawContext *draw) override;

    // --- Audio ---
    int GetAudioSampleRate() const override;
    void GetAudioSamples(int16_t *buffer, size_t *samples) override;

    // --- Input ---
    void SetKeys(uint32_t keys) override;
    uint32_t GetKeys() const override;

    // --- Savestate ---
    size_t GetStateSize() const override;
    bool SaveState(void *buffer) override;
    bool LoadState(const void *buffer) override;

    // --- Game Info ---
    void GetGameInfo(std::string &title, std::string &id) const override;

    // --- <CoreName>-specific (optional) ---
    // void GetMixedAudio(int32_t *buffer, size_t *stereoPairs);
    // bool SaveStateToFile(int slot);
    // bool LoadStateFromFile(int slot);

private:
    // ...
};

}  // namespace EmuCore
```

### Step 3: Register in DetectType + Factory

**File:** `EmuCore/EmuCore.cpp`

```cpp
#include "EmuCore/<CoreName>Core.h"

Type DetectType(const Path &romPath) {
    // ...
#ifdef PPSSPP_<CORENAME>
    if (ext == ".n64" || ext == ".z64") {
        return Type::<CoreName>;
    }
#endif
    // ...
}

std::unique_ptr<Core> Create(const Path &romPath) {
    switch (type) {
#ifdef PPSSPP_<CORENAME>
    case Type::<CoreName>:
        return std::make_unique<<CoreName>Core>();
#endif
    // ...
    }
}
```

### Step 4: Add feature flag to CMake

**File:** `EmuCore/CMakeLists.txt`

```cmake
option(PPSSPP_MULTICORE "Enable multi-emulator support" ON)

# --- Existing ---
if(PPSSPP_MULTICORE)
    add_library(EmuCore STATIC
        EmuCore.cpp
        PSPCore.cpp
        GBACore.cpp
        Config.cpp
        RecentFilesRegistry.cpp
    )
    target_include_directories(EmuCore PUBLIC ${CMAKE_SOURCE_DIR}/ext/libmgba/include)
    target_link_libraries(EmuCore PUBLIC mgba)
endif()

# --- Add for new core ---
# If new core needs external library (libn64, etc.):
# if(PPSSPP_MULTICORE)
#     add_subdirectory(${CMAKE_SOURCE_DIR}/ext/lib<corename> ${CMAKE_BINARY_DIR}/ext/lib<corename>)
#     target_sources(EmuCore PRIVATE <CoreName>Core.cpp)
#     target_include_directories(EmuCore PUBLIC ${CMAKE_SOURCE_DIR}/ext/lib<corename>/include)
#     target_link_libraries(EmuCore PUBLIC <corename>)
# endif()
```

### Step 5: Register in RecentFilesRegistry

**File:** `UI/NativeApp.cpp`

```cpp
// [PPSSPP-FORK] <CoreName>: register recent files grouping
#ifdef PPSSPP_MULTICORE
auto &reg = EmuCore::RecentFilesRegistry::Get();
reg.Register(EmuCore::RecentFilesEntry{
    (int)EmuCore::Type::<CoreName>,
    "<CoreName>",                    // displayName
    "<CoreName> Recent",             // iniSection → [<CoreName> Recent] in ppsspp.ini
    "RECENT_<CORENAME>",             // specialPath
    &g_recentFiles<CoreName>,        // RecentFilesManager global
    nullptr,
    ".<ext1>:.<ext2>",               // extensions
});
#endif
```

Then in the init function:

```cpp
// [PPSSPP-FORK] <CoreName>: init recent files
void Init<CoreName>() {
    auto path = GetSysDirectory(DIRECTORY_SAVESTATE);
    g_recentFiles<CoreName>.SetCompatPath(path);
}
```

### Step 6: Add config section

**File:** `Core/Config.h` — add config struct:

```cpp
// [PPSSPP-FORK] <CoreName>: settings
struct <CoreName>DisplayConfig {
    // ...
};
struct <CoreName>AudioConfig {
    // ...
};
extern <CoreName>DisplayConfig g_<lower>DisplayConfig;
extern <CoreName>AudioConfig g_<lower>AudioConfig;
```

**File:** `Core/Config.cpp` — load/save in section `[<CoreName>]`:

```cpp
// [PPSSPP-FORK] <CoreName>: load settings
g_<lower>DisplayConfig.someField = section->Get("someField", defaultVal);
```

### Step 7: Integrate in EmuScreen

**File:** `UI/EmuScreen.h` — add helpers:

```cpp
// [PPSSPP-FORK] <CoreName>: helpers
#ifdef PPSSPP_<CORENAME>
bool Is<CoreName>() const;
void Init<CoreName>();
void Shutdown<CoreName>();
void Update<CoreName>();
void Render<CoreName>(Draw::DrawContext *draw);
#endif
```

**File:** `UI/EmuScreen.cpp` — routing:

```cpp
// In constructor — init core
// In destructor — shutdown core
// In update loop — run frame + audio
// In render — draw
```

Use the `Is<CoreName>()` pattern to avoid `#ifdef` at call sites:

```cpp
// EmuScreen.h — constexpr fallback
#ifdef PPSSPP_<CORENAME>
    bool Is<CoreName>() const { return coreType_ == EmuCore::Type::<CoreName>; }
#else
    static constexpr bool Is<CoreName>() { return false; }
#endif

// Call site — compiler eliminates dead branch
if (Is<CoreName>()) {
    Do<CoreName>Stuff();
}
```

### Step 8: Per-Core Touch Layout (On-Screen Controls)

Every core that needs on-screen touch buttons MUST register its default layout.

**Mechanism:**

1. Define default buttons in `EmuCore::Config.cpp` via `FillDefault<CoreName>TouchLayout()`
2. Call from `EmuCore::InitDefaultTouchConfigs()`
3. `EmuScreen::AddGBATouchButtons()` (generic) automatically reads from config

**Config Storage:**

```ini
; PSP (existing — zero break)
[ControlLayout]
button0=0,0.82,0.47,0.09,0.09,18

; GBA (new)
[GBA ControlLayout]
button0=0,0.82,0.47,0.09,0.09,A
button1=1,0.73,0.38,0.09,0.09,B
...

; New core
[<CoreName> ControlLayout]
button0=<keyCode>,<x>,<y>,<w>,<h>,<label>
...
```

**Line format:** `keyCode,x,y,w,h,label,visible`

**Default layout registration in `EmuCore/Config.cpp`:**

```cpp
void InitDefaultTouchConfigs() {
    // GBA — landscape
    auto &gbaLand = g_coreTouchLandscape[(int)Type::GBA];
    gbaLand.Add(CTRL_CROSS,  0.82f, 0.47f, 0.09f, 0.09f, "A");
    gbaLand.Add(CTRL_CIRCLE, 0.73f, 0.38f, 0.09f, 0.09f, "B");
    
    // <CoreName> — landscape
    auto &nLand = g_coreTouchLandscape[(int)Type::<CORENAME>];
    nLand.Add(...);
}
```

**Configure via Settings:**

Button "Customize On-Screen Controls" → `CoreTouchLayoutScreen(gamePath, Type::<CORENAME>)`
This screen automatically reads/saves from the appropriate config section.

**Files:** `EmuCore/Config.h`, `EmuCore/Config.cpp`, `UI/CoreTouchLayoutScreen.h/.cpp`

### Step 9: Optional files (if needed)

| Component | File | When Needed |
|-----------|------|-------------|
| Touch layout (legacy) | `UI/TouchLayout<CoreName>.h/.cpp` | **DEPRECATED** — use per-core config |
| Settings screen | `UI/<CoreName>SettingsScreen.h/.cpp` | Core has its own settings |
| Android audio | `android/jni/AndroidAudio.cpp` | Audio routing differs by platform |
| Test | `test_<corename>_core.cpp` | Minimal load + run test |

---

## Code Writing Standards

### 1. File Structure Convention

Every core MUST have the same file structure:

```
EmuCore/
├── <CoreName>Core.h      → class declaration + public interface
├── <CoreName>Core.cpp    → implementation
```

Method order in header & implementation MUST BE IDENTICAL:

```cpp
class <CoreName>Core : public Core {
public:
    // 1. Constructor/destructor
    // 2. Type
    // 3. Lifecycle (LoadROM, RunFrame, Reset)
    // 4. Rendering (Render, DeviceLost, DeviceRestored)
    // 5. Audio (GetAudioSampleRate, GetAudioSamples)
    // 6. Input (SetKeys, GetKeys)
    // 7. Savestate (GetStateSize, SaveState, LoadState)
    // 8. Game Info
    // 9. <CoreName>-specific methods
private:
    // Member variables — grouped by concern:
    // Video
    // Audio
    // Input
    // State
};
```

### 2. Comment Convention

All fork code MUST use markers:

```cpp
// [PPSSPP-FORK] <CoreName>: <description>
// <Optional detail — 1 line or multiline>
```

Rules:
- First line: `// [PPSSPP-FORK] <CoreName>: <imperative sentence>`
- Following lines: `// <detail>`
- Language: **English** for markers
- Marker on EVERY custom file (header + implementation)
- Marker on EVERY `#ifdef` block in upstream files

### 3. #ifdef Convention

```cpp
#ifdef PPSSPP_<CORENAME>
    // Core-specific code
    // REQUIRED: comment [PPSSPP-FORK] <CoreName>: ...
#endif
```

Rules:
- **DO NOT** place `#else` with upstream code inside
- **DO NOT** change upstream code indentation to accommodate `#ifdef`
- `#ifdef` blocks only **add**, do not **modify** surrounding code
- Every `#ifdef` MUST be flippable OFF with build still succeeding

### 4. Method Extraction Pattern

If custom logic > 5 lines in an upstream file, extract to a helper:

```cpp
// In upstream file — minimal, just 3 lines
#ifdef PPSSPP_<CORENAME>
    if (Is<CoreName>()) {
        Update<CoreName>();
        return;
    }
#endif

// Helper implementation — all logic here
// [PPSSPP-FORK] <CoreName>: <description>
void EmuScreen::Update<CoreName>() {
    // ... all logic
}
```

Helper naming convention:
| Pattern | Example |
|---------|---------|
| `Init<CoreName>()` | `InitGBA()` |
| `Shutdown<CoreName>()` | `ShutdownGBA()` |
| `Update<CoreName>()` | `UpdateGBA()` |
| `Render<CoreName>()` | `RenderGBA()` |

### 5. Is<CoreName>() Pattern

```cpp
// Header — constexpr fallback for compiler dead branch elimination
#ifdef PPSSPP_<CORENAME>
    bool Is<CoreName>() const;
#else
    static constexpr bool Is<CoreName>() { return false; }
#endif

// Implementation
#ifdef PPSSPP_<CORENAME>
bool EmuScreen::Is<CoreName>() const {
    return coreType_ == EmuCore::Type::<CoreName>;
}
#endif
```

> **Why?** Call site without `#ifdef` = more readable, fewer typos.
> Compiler automatically eliminates `if (false)` branches.

### 6. Config Isolation

Every core MUST have its own config section:

```ini
; ppsspp.ini
[PSP]
renderMode=1
frameskip=0

[GBA]
texFiltering=0
aspectRatio=0

[<CoreName>]
setting1=value1
setting2=value2
```

**FORBIDDEN:** Merge different core configs into one section.
**REQUIRED:** Each core loads/saves from its own section.

### 7. Feature Flag Convention

| Flag | Scope | Status |
|------|-------|--------|
| `PPSSPP_MULTICORE` | Global — enable all multi-core | ✅ Active |
| `PPSSPP_GBA` | GBA-specific (future: separate from MULTICORE) | ⏳ Not yet |
| `PPSSPP_<CORENAME>` | Per-new-core | ✅ Required |

Rules:
- Every new core MUST have its own flag: `PPSSPP_<CORENAME>`
- Flag defined in `EmuCore/CMakeLists.txt`
- Build MUST be verified ON and OFF

---

## Integration Points Map — All Files That Need Changes

Below is the **complete map** of all files that need modification when adding a new core.
Use as a checklist.

### Required Changes (core files)

| # | File | Change |
|---|------|--------|
| 1 | `EmuCore/EmuCore.h` | Add enum `Type::<CoreName>` |
| 2 | `EmuCore/EmuCore.cpp` | Add to `DetectType()` + `Create()` |
| 3 | `EmuCore/<CoreName>Core.h` | **NEW** — class declaration |
| 4 | `EmuCore/<CoreName>Core.cpp` | **NEW** — implementation |
| 5 | `EmuCore/CMakeLists.txt` | Add source file |
| 6 | `Core/Config.h` | Add config struct + extern vars |
| 7 | `Core/Config.cpp` | Add load/save config section |
| 8 | `UI/NativeApp.cpp` | Register RecentFilesRegistry + Init |
| 9 | `EmuCore/Config.h` | Add default touch buttons in `InitDefaultTouchConfigs()` |
| 10 | `EmuCore/Config.cpp` | Add `FillDefault<CoreName>TouchLayout()` |

### Required Changes (UI integration)

| # | File | Change |
|---|------|--------|
| 9 | `UI/EmuScreen.h` | Add helper methods + Is<CoreName>() |
| 10 | `UI/EmuScreen.cpp` | Init, shutdown, update, render routing |
| 11 | `UI/EmuScreen.cpp` | Input handling + save/load routing |
| 12 | `UI/MainScreen.cpp` | Recent tab grouping (auto via registry) |

### Optional

| # | File | When |
|---|------|------|
| 13 | `UI/TouchLayout<CoreName>.h/.cpp` | **NEW** — if touch controls needed |
| 14 | `UI/<CoreName>SettingsScreen.h/.cpp` | **NEW** — if settings screen needed |
| 15 | `UI/PauseScreen.cpp` | If save/load differs from default |
| 16 | `Core/KeyMap.h/.cpp` | If custom VIRTKEY needed |
| 17 | `Util/RecentFiles.h/.cpp` | If separate recent list needed |
| 18 | `ext/lib<corename>/` | Submodule emulator library |
| 19 | `test_<corename>_core.cpp` | **NEW** — minimal test |
| 20 | `CMakeLists.txt` (root) | Link library + target |

### Android-Specific

| # | File | When |
|---|------|------|
| 21 | `android/jni/Android.mk` | Add source (if using ndk-build) |
| 22 | `android/jni/AndroidAudio.cpp` | If audio routing differs |
| 23 | `AndroidManifest.xml` | Intent filter for file extension |
| 24 | `PpssppActivity.java` | Handle open file intent |

---

## Common Patterns — Code Examples

### Factory Pattern (EmuCore.cpp)

```cpp
std::unique_ptr<Core> Create(const Path &romPath) {
    Type type = DetectType(romPath);

    switch (type) {
#ifdef PPSSPP_MULTICORE
    case Type::GBA:
        return std::make_unique<GBACore>();
#endif
#ifdef PPSSPP_<CORENAME>
    case Type::<CoreName>:
        return std::make_unique<<CoreName>Core>();
#endif
    case Type::PSP:
    default:
        return std::make_unique<PSPCore>();
    }
}
```

### Audio Output Pattern (EmuScreen.cpp)

```cpp
// [PPSSPP-FORK] <CoreName>: audio push
if (Is<CoreName>()) {
    size_t stereoPairs = TARGET_PAIRS;
    int32_t audioBuf[TARGET_PAIRS * 2];
    static_cast<<CoreName>Core*>(activeCore_.get())->GetMixedAudio(audioBuf, &stereoPairs);
    System_AudioPushSamples(audioBuf, stereoPairs, 1.0f);
    return;
}
```

### Save State Pattern (PauseScreen.cpp)

```cpp
// [PPSSPP-FORK] <CoreName>: file-based save
if (Is<CoreName>()) {
    static_cast<<CoreName>Core*>(g_GBACore)->SaveStateToFile(slot);
    return;
}
```

### Touch Layout Pattern

```cpp
// [PPSSPP-FORK] <CoreName>: touch layout
if (Is<CoreName>()) {
    CreateTouchButtons<CoreName>(root);
    return;
}
```

---

## Verification Checklist

Before merge, REQUIRED verification:

- [ ] `-DPPSSPP_<CORENAME>=ON` build succeeds
- [ ] `-DPPSSPP_<CORENAME>=OFF` build succeeds (no leaked code)
- [ ] All custom files have `[PPSSPP-FORK] <CoreName>: ` marker
- [ ] Config isolated in its own section (`[<CoreName>]`)
- [ ] Recent files in its own section (`[<CoreName> Recent]`)
- [ ] VIRTKEY prefix differs from other cores
- [ ] `EmuCore/EmuCore.h` — enum `Type::<CoreName>` registered
- [ ] `EmuCore/EmuCore.cpp` — DetectType + Factory routing correct
- [ ] `EmuScreen::Is<CoreName>()` — constexpr fallback for OFF build
- [ ] DeviceLost + DeviceRestored implemented (if using GPU)
- [ ] Lazy init GPU resources (on first Render(), not constructor)
- [ ] Save state path doesn't conflict with other cores
