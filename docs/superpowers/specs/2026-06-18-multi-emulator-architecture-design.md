# Multi-Emulator Architecture for PPSSPP Fork

**Date:** 2026-06-18
**Status:** Design Document

## Objective

Add GBA (Game Boy Advance) emulation capability to PPSSPP fork via a modular Core Abstraction Layer, without breaking or modifying upstream PPSSPP code.

## Core Library

**mGBA (libmgba)** — MPL 2.0, multi-platform, library mode (`LIBMGBA_ONLY`)

## Architecture

```
EmuCore (abstraction interface)
  ├── PSPCore (wrapper delegating to existing PSP system)
  └── GBACore (libmgba-based GBA emulation)

EmuScreen → detects file type → instantiates correct core → runs emulation
```

## File Changes

### New Files (zero upstream impact)

| File | Purpose |
|------|---------|
| `EmuCore/EmuCore.h` | Abstract interface for emulator cores |
| `EmuCore/EmuCore.cpp` | Factory: Create() + DetectType() |
| `EmuCore/PSPCore.h` | PSP wrapper (delegates to existing system) |
| `EmuCore/PSPCore.cpp` | PSP wrapper implementation |
| `EmuCore/GBACore.h` | GBA emulator class |
| `EmuCore/GBACore.cpp` | GBA via libmgba library |
| `EmuCore/CMakeLists.txt` | Build configuration |
| `UI/TouchLayoutGBA.h` | GBA touch button layout |
| `UI/TouchLayoutGBA.cpp` | GBA touch button layout |

### Upstream Files Touched (ADD-only, #ifdef-guarded)

| File | Change |
|------|--------|
| `UI/GameBrowser.cpp` | +1 line: add "gba:gb:gbc:" to file filter |
| `UI/MainScreen.cpp` | +1 block: detect file type, switch to GBA EmuScreen |
| `UI/EmuScreen.h` | +1 parameter: optional coreType in constructor |
| `UI/EmuScreen.cpp` | +1 block: if GBA core, delegate to GBACore::RunFrame() |
| `CMakeLists.txt` | +4 lines: add_subdirectory(EmuCore) if PPSSPP_MULTICORE |

### External Dependency

| Library | Integration |
|---------|-------------|
| `ext/libmgba/` | Git submodule, built via `LIBMGBA_ONLY` |

## Settings Auto-Switching

When user selects a game:
- PSP game (.iso/.cso/.pbp) → loads PSP configuration (existing)
- GBA game (.gba/.gb/.gbc) → loads GBA configuration (new section in config)

Settings isolated per core:
- Touch control layout
- Display settings (filter, aspect ratio)
- Key mappings (optional, shared by default)
- Audio settings
- Savestate directory

## Key Design Decisions

1. **Shared physical key mappings** — keyboard/gamepad controls for PSP work for GBA too
2. **Separate touch layouts** — GBA overlay has A/B buttons instead of △/○/×/□
3. **Auto-detect** — file extension determines which core to use
4. **Feature flag** — `PPSSPP_MULTICORE` compile-time toggle
5. **Rendering** — GBA renders to buffer → uploaded as texture → displayed in PPSSPP UI
6. **Audio** — mGBA audio samples → pushed to PPSSPP audio system
7. **Savestates** — GBA saves to separate directory (`savestates/GBA/`)
