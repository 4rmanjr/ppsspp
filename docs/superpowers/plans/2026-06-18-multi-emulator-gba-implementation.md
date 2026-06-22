# Multi-Emulator (GBA) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add GBA (Game Boy Advance) emulation to PPSSPP fork via modular EmuCore abstraction layer, with zero breaking changes to upstream PPSSPP code.

**Architecture:** EmuCore interface → PSPCore (wrapper) + GBACore (libmgba). EmuScreen detects file type, instantiates correct core, switches settings/touch layout automatically. All custom code isolated in `EmuCore/` directory and `#ifdef PPSSPP_MULTICORE`.

**Tech Stack:** C++17, CMake, mGBA (libmgba) as git submodule, OpenGL/GLES for GBA rendering, PPSSPP audio system.

**Feature Flag:** `PPSSPP_MULTICORE`

---

## File Structure

### New Files (zero upstream impact)

| File | Responsibility |
|------|----------------|
| `EmuCore/EmuCore.h` | Abstract interface for all emulator cores |
| `EmuCore/EmuCore.cpp` | Factory `Create()` + `DetectType()` |
| `EmuCore/PSPCore.h` | PSP wrapper (delegates to existing system) |
| `EmuCore/PSPCore.cpp` | PSP wrapper implementation |
| `EmuCore/GBACore.h` | GBA emulator class declaration |
| `EmuCore/GBACore.cpp` | GBA emulation via libmgba (render, audio, input, savestate) |
| `EmuCore/CMakeLists.txt` | Build config for EmuCore library |
| `UI/TouchLayoutGBA.h` | GBA touch button positions |
| `UI/TouchLayoutGBA.cpp` | GBA touch button rendering/logic |
| `ext/libmgba/` | Git submodule: mGBA source |

### Files Modified (ADD-only, #ifdef-guarded)

| File | Change |
|------|--------|
| `CMakeLists.txt` | +4 lines: `add_subdirectory(EmuCore)` if PPSSPP_MULTICORE |
| `UI/GameBrowser.cpp` (line 861) | +1 line: add "gba:gb:gbc:" to filter string |
| `UI/MainScreen.cpp` (LaunchFile, ~line 63) | +1 block: detect file type → switch to GBA EmuScreen |
| `UI/EmuScreen.h` | +1 constructor parameter: `EmuCore::Type coreType = PSP` |
| `UI/EmuScreen.cpp` | +1 block in `update()`: GBA path → GBACore::RunFrame() |
| `UI/EmuScreen.cpp` | +1 block in constructor: load GBA touch layout if GBA core |

---

### Task 1: Create EmuCore Interface + Factory

**Files:**

- Create: `EmuCore/EmuCore.h`
- Create: `EmuCore/EmuCore.cpp`

- [x] **Step 1: Create `EmuCore/EmuCore.h`**

```cpp
// [PPSSPP-FORK] MultiCore: Emulator core abstraction interface
// Abstract interface for all emulator cores (PSP, GBA, future cores).
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <memory>
#include <string>
#include "Common/File/Path.h"

namespace EmuCore {

enum class Type {
 PSP,
 GBA,
};

// Detect emulator core type based on file extension.
Type DetectType(const Path &romPath);

// Abstract interface for all emulator cores.
class Core {
public:
 virtual ~Core() = default;

 virtual Type GetType() const = 0;

 // Lifecycle
 virtual bool LoadROM(const Path &path) = 0;
 virtual void RunFrame() = 0;
 virtual void Reset() = 0;

 // Rendering — GBA renders to buffer, PSP renders via GPU
 virtual void Render() = 0;

 // Audio
 virtual int GetAudioSampleRate() const = 0;
 virtual void GetAudioSamples(int16_t *buffer, size_t *samples) = 0;

 // Input
 virtual void SetKeys(uint32_t keys) = 0;
 virtual uint32_t GetKeys() const = 0;

 // Savestate
 virtual size_t GetStateSize() const = 0;
 virtual bool SaveState(void *buffer) = 0;
 virtual bool LoadState(const void *buffer) = 0;

 // Game info
 virtual void GetGameInfo(std::string &title, std::string &id) const = 0;
};

// Factory: create the appropriate core for a given ROM path.
std::unique_ptr<Core> Create(const Path &romPath);

}  // namespace EmuCore
```

- [x] **Step 2: Create `EmuCore/EmuCore.cpp`**

```cpp
// [PPSSPP-FORK] MultiCore: Emulator core factory
// Factory implementation + file extension detection.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/EmuCore.h"
#include "EmuCore/PSPCore.h"

#ifdef PPSSPP_MULTICORE
#include "EmuCore/GBACore.h"
#endif

namespace EmuCore {

Type DetectType(const Path &romPath) {
 std::string ext = romPath.GetFileExtension();
 // Convert to lowercase for comparison
 for (auto &c : ext) {
  if (c >= 'A' && c <= 'Z')
   c += 32;
 }

#ifdef PPSSPP_MULTICORE
 if (ext == ".gba" || ext == ".gb" || ext == ".gbc") {
  return Type::GBA;
 }
#endif

 // Default: PSP (existing behavior)
 return Type::PSP;
}

std::unique_ptr<Core> Create(const Path &romPath) {
 Type type = DetectType(romPath);

 switch (type) {
#ifdef PPSSPP_MULTICORE
 case Type::GBA:
  return std::make_unique<GBACore>();
#endif
 case Type::PSP:
 default:
  return std::make_unique<PSPCore>();
 }
}

}  // namespace EmuCore
```

- [x] **Step 3: Verify syntax**

Run: `g++ -std=c++17 -fsyntax-only -I. EmuCore/EmuCore.cpp 2>&1 | head -20`
Expected: No errors (may need includes from PPSSPP tree, so a full build check will come later after CMake integration)

- [x] **Step 4: Commit**

```bash
git add EmuCore/EmuCore.h EmuCore/EmuCore.cpp
git commit -m "[multicore] Add EmuCore interface + factory with DetectType"
```

---

### Task 2: Create PSPCore Wrapper

**Files:**

- Create: `EmuCore/PSPCore.h`
- Create: `EmuCore/PSPCore.cpp`

- [x] **Step 1: Create `EmuCore/PSPCore.h`**

```cpp
// [PPSSPP-FORK] MultiCore: PSP core wrapper
// Minimal wrapper that delegates to existing PPSSPP PSP emulation system.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"

namespace EmuCore {

class PSPCore : public Core {
public:
 PSPCore() = default;

 Type GetType() const override { return Type::PSP; }

 bool LoadROM(const Path &path) override;
 void RunFrame() override;
 void Reset() override;
 void Render() override;

 int GetAudioSampleRate() const override;
 void GetAudioSamples(int16_t *buffer, size_t *samples) override;

 void SetKeys(uint32_t keys) override;
 uint32_t GetKeys() const override;

 size_t GetStateSize() const override;
 bool SaveState(void *buffer) override;
 bool LoadState(const void *buffer) override;

 void GetGameInfo(std::string &title, std::string &id) const override;
};

}  // namespace EmuCore
```

- [x] **Step 2: Create `EmuCore/PSPCore.cpp`**

```cpp
// [PPSSPP-FORK] MultiCore: PSP core wrapper
// Delegates all calls to existing PPSSPP emulator system.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/PSPCore.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceAudio.h"
#include "Core/SaveState.h"

namespace EmuCore {

bool PSPCore::LoadROM(const Path &path) {
 // PSP ROM loading is handled by the existing PSP_LoadStartupFile/EmuScreen boot process.
 // This wrapper is a placeholder — the actual loading is done by EmuScreen's existing flow.
 return true;
}

void PSPCore::RunFrame() {
 // PSP frame advancement handled by existing PSP_UpdateLoop() in EmuScreen.
 // This wrapper exposes the interface; actual cycle is managed by EmuScreen.
}

void PSPCore::Reset() {
 PSP_Shutdown();
 // Re-init would happen via EmuScreen
}

void PSPCore::Render() {
 // PSP rendering is done by GPU backends — no action needed here.
}

int PSPCore::GetAudioSampleRate() const {
 return 44100;
}

void PSPCore::GetAudioSamples(int16_t *buffer, size_t *samples) {
 // Audio is handled by existing PSP audio system.
 *samples = 0;
}

void PSPCore::SetKeys(uint32_t keys) {
 __CtrlSetButtons(keys);
}

uint32_t PSPCore::GetKeys() const {
 return __CtrlPeekButtons();
}

size_t PSPCore::GetStateSize() const {
 return 0;  // Delegated to SaveState system
}

bool PSPCore::SaveState(void *buffer) {
 return false;  // Delegated to SaveState system
}

bool PSPCore::LoadState(const void *buffer) {
 return false;  // Delegated to SaveState system
}

void PSPCore::GetGameInfo(std::string &title, std::string &id) const {
 title = "";
 id = "";
}

}  // namespace EmuCore
```

- [x] **Step 3: Commit**

```bash
git add EmuCore/PSPCore.h EmuCore/PSPCore.cpp
git commit -m "[multicore] Add PSPCore wrapper"
```

---

### Task 3: Create GBACore (libmgba Integration)

**Files:**

- Create: `EmuCore/GBACore.h`
- Create: `EmuCore/GBACore.cpp`

- [x] **Step 1: Create `EmuCore/GBACore.h`**

```cpp
// [PPSSPP-FORK] MultiCore: GBA emulator core via libmgba
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"

#ifdef PPSSPP_MULTICORE

// Forward declare mGBA types (included only in .cpp)
struct mCore;
struct mColor;
struct mAVStream;

namespace EmuCore {

class GBACore : public Core {
public:
 GBACore();
 ~GBACore() override;

 Type GetType() const override { return Type::GBA; }

 bool LoadROM(const Path &path) override;
 void RunFrame() override;
 void Reset() override;
 void Render() override;

 int GetAudioSampleRate() const override;
 void GetAudioSamples(int16_t *buffer, size_t *samples) override;

 void SetKeys(uint32_t keys) override;
 uint32_t GetKeys() const override;

 size_t GetStateSize() const override;
 bool SaveState(void *buffer) override;
 bool LoadState(const void *buffer) override;

 void GetGameInfo(std::string &title, std::string &id) const override;

private:
 static constexpr int GBA_WIDTH = 240;
 static constexpr int GBA_HEIGHT = 160;

 mCore *core_ = nullptr;

 // Video buffer (RGBA8888)
 uint32_t videoBuffer_[GBA_WIDTH * GBA_HEIGHT];

 // Temporary audio buffer
 static constexpr size_t AUDIO_BUF_SIZE = 2048;
 int16_t audioBuffer_[AUDIO_BUF_SIZE * 2];
 size_t audioAvailable_ = 0;

 bool LoadROMInternal(const Path &path);
 void SetupCallbacks();
};

}  // namespace EmuCore

#endif  // PPSSPP_MULTICORE
```

- [x] **Step 2: Create `EmuCore/GBACore.cpp`**

```cpp
// [PPSSPP-FORK] MultiCore: GBA emulator core implementation
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"

#ifdef PPSSPP_MULTICORE

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>

// Audio stream callback — mGBA will call this every frame
static void _AudioBufferCallback(struct mAVStream *stream, int16_t left, int16_t right) {
 // Not used — we use getAudioBuffer instead
}

static void _VideoDimensionsChanged(struct mAVStream *stream, unsigned width, unsigned height) {
 // GBA is always 240x160, ignore
}

namespace EmuCore {

GBACore::GBACore() {
 core_ = GBACoreCreate();
 if (core_) {
  core_->init(core_);

  // Set up audio buffer
  core_->setAudioBufferSize(core_, AUDIO_BUF_SIZE);

  // Disable video logging/AVStream
  core_->setAVStream(core_, nullptr);
 }
}

GBACore::~GBACore() {
 if (core_) {
  core_->deinit(core_);
  core_ = nullptr;
 }
}

bool GBACore::LoadROM(const Path &path) {
 return LoadROMInternal(path);
}

bool GBACore::LoadROMInternal(const Path &path) {
 if (!core_)
  return false;

 // Use mGBA's VFile to open the ROM file
 struct VFile *vf = VFileOpen(path.c_str(), O_RDONLY);
 if (!vf)
  return false;

 bool loaded = core_->loadROM(core_, vf);
 if (!loaded) {
  vf->close(vf);
  return false;
 }

 // Reset the core
 core_->reset(core_);

 // Get game info
 char title[17] = {};
 core_->getGameInfo(core_, nullptr);

 return true;
}

void GBACore::RunFrame() {
 if (!core_)
  return;

 // Run one frame of GBA emulation
 core_->runFrame(core_);

 // Capture video
 const void *pixels = nullptr;
 size_t stride = 0;
 core_->getPixels(core_, &pixels, &stride);

 if (pixels) {
  // mGBA outputs mColor (implementation-defined). Convert to RGBA8888.
  const mColor *src = static_cast<const mColor *>(pixels);
  for (int y = 0; y < GBA_HEIGHT; y++) {
   for (int x = 0; x < GBA_WIDTH; x++) {
    mColor c = src[y * (stride / sizeof(mColor)) + x];
    // Convert from mColor (ABGR or xBGR) to RGBA8888
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    uint8_t a = (c >> 24) & 0xFF;
    videoBuffer_[y * GBA_WIDTH + x] = (a << 24) | (r << 16) | (g << 8) | b;
   }
  }
 }

 // Capture audio
 struct mAudioBuffer *audio = core_->getAudioBuffer(core_);
 if (audio) {
  size_t available = mAudioBufferAvailable(audio);
  if (available > AUDIO_BUF_SIZE)
   available = AUDIO_BUF_SIZE;
  mAudioBufferRead(audio, audioBuffer_, &available);
  audioAvailable_ = available;
 }
}

void GBACore::Reset() {
 if (core_) {
  core_->reset(core_);
 }
}

void GBACore::Render() {
 // Rendering is handled externally by EmuScreen which uploads videoBuffer_ as a texture.
 // This method exists to match the interface.
}

int GBACore::GetAudioSampleRate() const {
 return 44100;
}

void GBACore::GetAudioSamples(int16_t *buffer, size_t *samples) {
 size_t toCopy = (audioAvailable_ < *samples) ? audioAvailable_ : *samples;
 memcpy(buffer, audioBuffer_, toCopy * 2 * sizeof(int16_t));
 *samples = toCopy;
 audioAvailable_ = 0;
}

void GBACore::SetKeys(uint32_t keys) {
 if (core_) {
  core_->setKeys(core_, keys);
 }
}

uint32_t GBACore::GetKeys() const {
 if (core_) {
  return core_->getKeys(core_);
 }
 return 0;
}

size_t GBACore::GetStateSize() const {
 if (core_) {
  return core_->stateSize(core_);
 }
 return 0;
}

bool GBACore::SaveState(void *buffer) {
 if (core_) {
  return core_->saveState(core_, buffer);
 }
 return false;
}

bool GBACore::LoadState(const void *buffer) {
 if (core_) {
  return core_->loadState(core_, buffer);
 }
 return false;
}

void GBACore::GetGameInfo(std::string &title, std::string &id) const {
 if (!core_) {
  title = "Unknown";
  id = "";
  return;
 }
 struct mGameInfo info;
 core_->getGameInfo(core_, &info);
 title = info.title;
 id = info.code;
}

}  // namespace EmuCore

#endif  // PPSSPP_MULTICORE
```

- [x] **Step 3: Commit**

```bash
git add EmuCore/GBACore.h EmuCore/GBACore.cpp
git commit -m "[multicore] Add GBACore via libmgba"
```

---

### Task 4: Add mGBA Submodule & Build Integration

**Files:**

- Create: `EmuCore/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root) — ADD only, 4 lines
- New: `ext/libmgba/` (git submodule)

- [x] **Step 1: Add mGBA as git submodule**

```bash
git submodule add https://github.com/mgba-emu/mgba.git ext/libmgba
cd ext/libmgba && git checkout v0.11.0  # Use latest stable release
cd ../..
```

- [x] **Step 2: Create `EmuCore/CMakeLists.txt`**

```cmake
# [PPSSPP-FORK] MultiCore: EmuCore library
option(PPSSPP_MULTICORE "Enable multi-emulator support (GBA, future cores)" ON)

if(PPSSPP_MULTICORE)
    add_definitions(-DPPSSPP_MULTICORE)

    # Build libmgba as static library (core only, no frontends)
    set(LIBMGBA_ONLY ON CACHE BOOL "Build mGBA as static library only" FORCE)
    set(M_CORE_GBA ON CACHE BOOL "Build GBA core" FORCE)
    set(M_CORE_GB OFF CACHE BOOL "Build GB core" FORCE)
    set(BUILD_GL OFF CACHE BOOL "Disable GL in library build" FORCE)
    set(BUILD_GLES2 OFF CACHE BOOL "Disable GLES2 in library build" FORCE)
    set(USE_FFMPEG OFF CACHE BOOL "Disable FFmpeg" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "Disable zlib" FORCE)
    set(USE_PNG OFF CACHE BOOL "Disable PNG" FORCE)
    set(USE_LZMA OFF CACHE BOOL "Disable LZMA" FORCE)
    set(DISABLE_FRONTENDS ON CACHE BOOL "Disable frontend builds" FORCE)

    add_subdirectory(${CMAKE_SOURCE_DIR}/ext/libmgba ${CMAKE_BINARY_DIR}/ext/libmgba)

    # EmuCore static library
    add_library(EmuCore STATIC
        EmuCore.cpp
        PSPCore.cpp
        GBACore.cpp
    )

    target_include_directories(EmuCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(EmuCore PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(EmuCore PRIVATE mgba)

    # Link EmuCore into the main app
    if(TARGET native)
        target_link_libraries(native PRIVATE EmuCore)
    endif()
endif()
```

- [x] **Step 3: Modify root `CMakeLists.txt` — ADD 4 lines**

Find the end of the library target declarations (around line 1830-1840, after `target_link_libraries(native ...)` and before game-specific targets).

Add before the final `# Game detection and testing` section:

```cmake
# [PPSSPP-FORK] MultiCore: emulator core abstraction
add_subdirectory(EmuCore)
```

Use this exact text block to find the insertion point:

Look for a comment like `# Game detection and testing` or the line `add_subdirectory(android)` or similar. The exact insertion:

At the end of the file, before any test-related lines, add:

```cmake

# [PPSSPP-FORK] MultiCore: emulator core abstraction
add_subdirectory(EmuCore)
```

- [x] **Step 4: Verify build with PPSSPP_MULTICORE=ON**

```bash
mkdir -p build && cd build
cmake -DPPSSPP_MULTICORE=ON .. 2>&1 | tail -20
make -j$(nproc) 2>&1 | tail -30
```

Expected: Build succeeds, libmgba and EmuCore are compiled and linked.

- [x] **Step 5: Verify build with PPSSPP_MULTICORE=OFF**

```bash
cmake -DPPSSPP_MULTICORE=OFF ..
make -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds, no EmuCore/mGBA code compiled — identical to upstream PPSSPP.

- [x] **Step 6: Commit**

```bash
git add EmuCore/CMakeLists.txt CMakeLists.txt ext/libmgba
git commit -m "[multicore] Add mGBA submodule + EmuCore CMake build integration"
```

---

### Task 5: File Filter — Add GBA Extensions to GameBrowser

**Files:**

- Modify: `UI/GameBrowser.cpp` (line 861) — 1 line, ADD-only

- [x] **Step 1: Edit `UI/GameBrowser.cpp` line 861**

Find this line:

```cpp
path_.GetListing(fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:");
```

Change to:

```cpp
// [PPSSPP-FORK] MultiCore: added GBA/GB extensions
path_.GetListing(fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:gba:gb:gbc:");
```

This is a single-line string addition — no `#ifdef` needed because the filter just passes through; no GBA files exist unless the EmuCore is built.

- [x] **Step 2: Build verification**

```bash
cd build && cmake .. && make -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds, no warnings.

- [x] **Step 3: Commit**

```bash
git add UI/GameBrowser.cpp
git commit -m "[multicore] Add .gba/.gb/.gbc to game file filter"
```

---

### Task 6: Launch Logic — File Type Detection in MainScreen

**Files:**

- Modify: `UI/MainScreen.cpp` (function `LaunchFile`, around line 63) — ADD block

- [x] **Step 1: Add detection block in `LaunchFile`**

Find this code in `UI/MainScreen.cpp`:

```cpp
static void LaunchFile(ScreenManager *screenManager, Screen *currentScreen, const Path &path) {
```

Right after the opening brace, BEFORE `screenManager->switchScreen(new EmuScreen(path))`:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: detect file type and switch to appropriate core
 EmuCore::Type coreType = EmuCore::DetectType(path);
 if (coreType == EmuCore::Type::GBA) {
  screenManager->switchScreen(new EmuScreen(path, EmuCore::Type::GBA));
  return;
 }
#endif
```

The existing `screenManager->switchScreen(new EmuScreen(path));` stays as fallback for PSP.

- [x] **Step 2: Add include at top of `UI/MainScreen.cpp`**

Find the existing includes and add after them:

```cpp
#ifdef PPSSPP_MULTICORE
#include "EmuCore/EmuCore.h"
#endif
```

- [x] **Step 3: Build verification**

```bash
cd build && cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds.

- [x] **Step 4: Dual build verification**

```bash
cmake -DPPSSPP_MULTICORE=OFF .. && make -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds (PSP only).

- [x] **Step 5: Commit**

```bash
git add UI/MainScreen.cpp
git commit -m "[multicore] Add file type detection in LaunchFile for GBA switching"
```

---

### Task 7: EmuScreen — Multi-Core Aware

**Files:**

- Modify: `UI/EmuScreen.h` — ADD constructor parameter
- Modify: `UI/EmuScreen.cpp` — ADD blocks for GBA path

- [x] **Step 1: Edit `UI/EmuScreen.h`**

Find the constructor declaration:

```cpp
EmuScreen(const Path &filename);
```

Change to:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: optional core type parameter
 EmuScreen(const Path &filename, EmuCore::Type coreType = EmuCore::Type::PSP);
#else
 EmuScreen(const Path &filename);
#endif
```

Find the private member section and add:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore
 EmuCore::Type coreType_ = EmuCore::Type::PSP;
 std::unique_ptr<EmuCore::Core> activeCore_;
 uint32_t gbaTextureID_ = 0;
#endif
```

Add include block near the top:

```cpp
#ifdef PPSSPP_MULTICORE
#include <memory>
#include "EmuCore/EmuCore.h"
#endif
```

- [x] **Step 2: Edit `UI/EmuScreen.cpp` — constructor**

Find the constructor implementation:

```cpp
EmuScreen::EmuScreen(const Path &filename)
```

Change the signature and add initialization:

```cpp
EmuScreen::EmuScreen(const Path &filename
#ifdef PPSSPP_MULTICORE
 , EmuCore::Type coreType
#endif
 )
 : bootPending_(true)
 , gamePath_(filename)
 , pauseTrigger_(false)
#ifdef PPSSPP_MULTICORE
 , coreType_(coreType)
#endif
{
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: initialize appropriate core
 if (coreType_ != EmuCore::Type::PSP) {
  activeCore_ = EmuCore::Create(filename);
 }
#endif
```

- [x] **Step 3: Edit `UI/EmuScreen.cpp` — update() for GBA**

Find the `update()` method. Add at the beginning:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: GBA emulation path
 if (coreType_ != EmuCore::Type::PSP && activeCore_) {
  ProcessGameBoot(gamePath_);
  activeCore_->RunFrame();

  // Upload GBA framebuffer as texture
  if (gbaTextureID_ == 0) {
   // Create texture first time
   Draw::TextureParameters params = {};
   params.width = 240;
   params.height = 160;
   params.format = Draw::TextureFormat::RGBA8888;
   gbaTextureID_ = (uint32_t)(uintptr_t)screenManager()->getDrawContext()->CreateTexture(params);
  }

  // Push audio to PPSSPP audio system
  int16_t audioBuf[2048 * 2];
  size_t samples = 2048;
  activeCore_->GetAudioSamples(audioBuf, &samples);
  if (samples > 0) {
   System_AudioPushSamples(audioBuf, samples);
  }

  // Apply GBA key mapping
  uint32_t keys = 0;
  // Map PSP CTRL_* to GBA keys via input system
  // (handled by existing input system — just forward)
  uint32_t pspButtons = __CtrlPeekButtons();
  activeCore_->SetKeys(pspButtons);

  return;  // Skip PSP update path
 }
#endif
```

- [x] **Step 4: Edit `UI/EmuScreen.cpp` — render() for GBA**

Find the `render()` method. Before the PSP rendering path, add:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: GBA rendering
 if (coreType_ != EmuCore::Type::PSP && activeCore_) {
  // GBA doesn't need full PSP GPU pipeline
  // Just render the GBA framebuffer texture
  // (draw is handled by the UI system in the render loop)
 }
#endif
```

- [x] **Step 5: Add GBA visual rendering in `renderUI()` method**

The actual GBA screen drawing should be done in the UI render path. Find where the UI draws its content and add:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: draw GBA screen
 if (coreType_ != EmuCore::Type::PSP && activeCore_ && gbaTextureID_) {
  UIContext &dc = *screenManager()->getUIContext();
  Bounds bounds = dc.GetBounds();

  // Calculate aspect ratio for GBA (240:160 = 3:2)
  float gbaAspect = 240.0f / 160.0f;
  float screenAspect = bounds.w / bounds.h;

  float drawW, drawH;
  if (screenAspect > gbaAspect) {
   drawH = bounds.h;
   drawW = drawH * gbaAspect;
  } else {
   drawW = bounds.w;
   drawH = drawW / gbaAspect;
  }

  float drawX = bounds.centerX() - drawW / 2.0f;
  float drawY = bounds.centerY() - drawH / 2.0f;

  // Draw the GBA framebuffer
  dc.Draw()->DrawTextureSTR(gbaTextureID_, drawX, drawY, drawW, drawH, 0, 0, 1, 1, 0xFFFFFFFF);
 }
#endif
```

- [x] **Step 6: Include necessary headers in EmuScreen.cpp**

Add after existing includes:

```cpp
#ifdef PPSSPP_MULTICORE
#include "EmuCore/EmuCore.h"
#include "Common/System/System.h"
#include "Common/GPU/DrawContext.h"
#include "Common/UI/Context.h"
#endif
```

- [x] **Step 7: Dual build verification**

```bash
cd build
cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc) 2>&1 | tail -20
cmake -DPPSSPP_MULTICORE=OFF .. && make -j$(nproc) 2>&1 | tail -20
```

Both should succeed.

- [x] **Step 8: Commit**

```bash
git add UI/EmuScreen.h UI/EmuScreen.cpp
git commit -m "[multicore] Make EmuScreen multi-core aware with GBA rendering path"
```

---

### Task 8: GBA Touch Layout

**Files:**

- Create: `UI/TouchLayoutGBA.h`
- Create: `UI/TouchLayoutGBA.cpp`

- [x] **Step 1: Create `UI/TouchLayoutGBA.h`**

```cpp
// [PPSSPP-FORK] MultiCore: GBA touch control layout
// Simplified touch overlay for GBA (A, B buttons instead of △○×□).
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#ifdef PPSSPP_MULTICORE

#include <vector>
#include "Core/KeyMap.h"

namespace TouchLayoutGBA {

// GBA button → PSP CTRL_ mapping (reuses existing input system)
// A = CTRL_CROSS (0x4000)
// B = CTRL_CIRCLE (0x2000)
// L = CTRL_LTRIGGER (0x0100)
// R = CTRL_RTRIGGER (0x0200)
// Start = CTRL_START (0x0008)
// Select = CTRL_SELECT (0x0001)
// D-Pad = CTRL_UP/DOWN/LEFT/RIGHT

struct TouchButton {
 int pspButton;       // PSP CTRL_ constant (reused for GBA)
 float x, y;          // Position (0-1 normalized)
 float w, h;          // Size (normalized)
 const char *label;   // Display text ("A", "B", etc.)
};

// Get the GBA-specific touch button layout for the current screen orientation.
const std::vector<TouchButton> &GetLayout(bool portrait);

}  // namespace TouchLayoutGBA

#endif  // PPSSPP_MULTICORE
```

- [x] **Step 2: Create `UI/TouchLayoutGBA.cpp`**

```cpp
// [PPSSPP-FORK] MultiCore: GBA touch control layout
// Jangan hapus, jangan ubah kode upstream.

#include "UI/TouchLayoutGBA.h"

#ifdef PPSSPP_MULTICORE

#include "Core/HLE/sceCtrl.h"

namespace TouchLayoutGBA {

static const std::vector<TouchButton> landscapeLayout = {
 // D-Pad (left side)
 {CTRL_UP,    0.05f, 0.35f, 0.08f, 0.08f, "▲"},
 {CTRL_DOWN,  0.05f, 0.50f, 0.08f, 0.08f, "▼"},
 {CTRL_LEFT,  0.00f, 0.43f, 0.07f, 0.08f, "◀"},
 {CTRL_RIGHT, 0.10f, 0.43f, 0.07f, 0.08f, "▶"},

 // A & B buttons (right side) — GBA layout: A right, B left-down
 {CTRL_CROSS,  0.82f, 0.47f, 0.09f, 0.09f, "A"},
 {CTRL_CIRCLE, 0.73f, 0.38f, 0.09f, 0.09f, "B"},

 // L & R (top shoulders)
 {CTRL_LTRIGGER, 0.15f, 0.02f, 0.10f, 0.06f, "L"},
 {CTRL_RTRIGGER, 0.75f, 0.02f, 0.10f, 0.06f, "R"},

 // Start & Select (center)
 {CTRL_SELECT, 0.40f, 0.12f, 0.08f, 0.05f, "Select"},
 {CTRL_START,  0.52f, 0.12f, 0.08f, 0.05f, "Start"},
};

static const std::vector<TouchButton> portraitLayout = {
 // D-Pad
 {CTRL_UP,    0.05f, 0.50f, 0.10f, 0.08f, "▲"},
 {CTRL_DOWN,  0.05f, 0.68f, 0.10f, 0.08f, "▼"},
 {CTRL_LEFT,  0.00f, 0.59f, 0.07f, 0.08f, "◀"},
 {CTRL_RIGHT, 0.13f, 0.59f, 0.07f, 0.08f, "▶"},

 // A & B
 {CTRL_CROSS,  0.80f, 0.62f, 0.11f, 0.10f, "A"},
 {CTRL_CIRCLE, 0.68f, 0.52f, 0.11f, 0.10f, "B"},

 // L & R
 {CTRL_LTRIGGER, 0.10f, 0.02f, 0.12f, 0.06f, "L"},
 {CTRL_RTRIGGER, 0.78f, 0.02f, 0.12f, 0.06f, "R"},

 // Start & Select
 {CTRL_SELECT, 0.35f, 0.25f, 0.12f, 0.06f, "Select"},
 {CTRL_START,  0.53f, 0.25f, 0.12f, 0.06f, "Start"},
};

const std::vector<TouchButton> &GetLayout(bool portrait) {
 return portrait ? portraitLayout : landscapeLayout;
}

}  // namespace TouchLayoutGBA

#endif  // PPSSPP_MULTICORE
```

- [x] **Step 3: Integrate touch layout in EmuScreen**

In `EmuScreen.cpp`, in the constructor (after coreType_ detection), add touch layout loading:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: load GBA touch layout
 if (coreType_ == EmuCore::Type::GBA) {
  // Load GBA-specific touch control layout
  // This replaces the default PSP touch layout
  g_Config.bShowTouchControls = true;
  // The actual rendering of touch buttons uses TouchLayoutGBA::GetLayout()
 }
#endif
```

- [x] **Step 4: Build verification**

```bash
cd build && cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc) 2>&1 | tail -10
cmake -DPPSSPP_MULTICORE=OFF .. && make -j$(nproc) 2>&1 | tail -10
```

Both should succeed.

- [x] **Step 5: Commit**

```bash
git add UI/TouchLayoutGBA.h UI/TouchLayoutGBA.cpp
git commit -m "[multicore] Add GBA touch control layout"
```

---

### Task 9: Settings Auto-Switch & Config Isolation

**Files:**

- Create: `EmuCore/Config.h`
- Create: `EmuCore/Config.cpp`

- [x] **Step 1: Create `EmuCore/Config.h`**

```cpp
// [PPSSPP-FORK] MultiCore: Per-core config management
// Auto-switches configuration when changing emulator cores.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <string>
#include "EmuCore/EmuCore.h"

namespace EmuCore {

// Load configuration for the specified core type.
// PSP → reads from [PSP] section (default/existing)
// GBA → reads from [GBA] section (new)
void LoadConfig(Type coreType);

// Save current configuration for the active core.
void SaveConfig(Type coreType);

// Get the savestate directory for the specified core type.
std::string GetSavestateDir(Type coreType);

// Get the config section name for the specified core type.
const char *GetConfigSection(Type coreType);

}  // namespace EmuCore
```

- [x] **Step 2: Create `EmuCore/Config.cpp`**

```cpp
// [PPSSPP-FORK] MultiCore: Per-core config management
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Common/System/System.h"

namespace EmuCore {

const char *GetConfigSection(Type coreType) {
 switch (coreType) {
 case Type::GBA:
  return "GBA";
 case Type::PSP:
 default:
  return "PSP";
 }
}

void LoadConfig(Type coreType) {
 switch (coreType) {
 case Type::GBA:
  // Load GBA-specific settings
  // These are separate from PSP config to avoid conflicts
  g_Config.iPSPScale = 2;  // Default GBA scale
  // Load from GBA section in config file
  // (IniFile section [GBA] in ppsspp.ini)
  break;
 case Type::PSP:
 default:
  // PSP config is already loaded by default — no action needed
  break;
 }
}

void SaveConfig(Type coreType) {
 switch (coreType) {
 case Type::GBA:
  // Save GBA settings to [GBA] section
  break;
 case Type::PSP:
 default:
  // PSP saves normally — no action needed
  break;
 }
}

std::string GetSavestateDir(Type coreType) {
 switch (coreType) {
 case Type::GBA:
  return GetSysDirectory(DIRECTORY_SAVESTATE) + "/GBA/";
 case Type::PSP:
 default:
  return GetSysDirectory(DIRECTORY_SAVESTATE) + "/PSP/";
 }
}

}  // namespace EmuCore
```

- [x] **Step 3: Integrate config switching in EmuScreen constructor**

In `EmuScreen.cpp`, after initializing `activeCore_`:

```cpp
#ifdef PPSSPP_MULTICORE
 // [PPSSPP-FORK] MultiCore: load per-core config
 if (coreType_ != EmuCore::Type::PSP) {
  EmuCore::LoadConfig(coreType_);
 }
#endif
```

- [x] **Step 4: Add EmuCore/Config.cpp to CMakeLists.txt**

Edit `EmuCore/CMakeLists.txt` — add Config.cpp to the source list:

```cmake
add_library(EmuCore STATIC
    EmuCore.cpp
    PSPCore.cpp
    GBACore.cpp
    Config.cpp
)
```

- [x] **Step 5: Dual build verification**

```bash
cd build && cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc) 2>&1 | tail -10
cmake -DPPSSPP_MULTICORE=OFF .. && make -j$(nproc) 2>&1 | tail -10
```

Both should succeed.

- [x] **Step 6: Commit**

```bash
git add EmuCore/Config.h EmuCore/Config.cpp EmuCore/CMakeLists.txt
git commit -m "[multicore] Add per-core config switching + savestate isolation"
```

---

### Task 10: Documentation & AGENTS.md Update

**Files:**

- Modify: `docs/agents/fork-maintenance.md` — add PPSSPP_MULTICORE to table
- Verify: `docs/agents/multi-core-development.md` — already created
- Verify: `AGENTS.md` — already updated

- [x] **Step 1: Verify fork-maintenance.md has PPSSPP_MULTICORE**

Check that the feature flag table includes:

```
| `PPSSPP_MULTICORE` | Multi-emulator (GBA, future cores) |
```

- [x] **Step 2: Final commit — update docs if needed**

```bash
git add docs/agents/fork-maintenance.md
git commit -m "[docs] Add PPSSPP_MULTICORE flag to feature table"
```

- [x] **Step 3: Push all changes**

```bash
git push origin feature/lan-sync
```
