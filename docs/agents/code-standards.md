# Code Standards — C++ & Platform Guidelines

## Language & Standards
- C++17 (follow the standard already used by upstream PPSSPP).
- Follow the code style of surrounding files — don't reformat or change upstream code indentation.

## File Header
Every new custom file MUST have a header:

```cpp
// [PPSSPP-FORK] <FeatureName>
// <Brief description>
// Do not delete or modify upstream code.
```

Files that are **additions to upstream files** (1-5 line hooks) MUST have:
```cpp
// [PPSSPP-FORK] <FeatureName>: <hook explanation>
// Only add new lines. Do not delete/modify upstream lines.
#ifdef PPSSPP_<FEATURE>
// ... minimal hook code ...
#endif
```

## Platform Handling
- Platform-specific code in respective directories (`SDL/`, `Windows/`, `macOS/`, `android/`).
- `Common/` is for cross-platform code only. If platform-specific blocks are needed, use `#ifdef`:

```cpp
// [PPSSPP-FORK] FeatureName
#ifdef _WIN32
// Windows code
#elif defined(__APPLE__)
// macOS code
#else
// Linux/SDL code
#endif
```

## Naming Convention
- New classes/functions: use prefix `LANSync`, `MDNS`, `TLS`, `GBA`, `EmuCore` matching the feature.
- Don't use names that conflict with upstream classes to avoid ambiguity.
- For multi-emulator: all core classes in namespace `EmuCore` (e.g., `EmuCore::GBACore`, `EmuCore::PSPCore`).

## Build Validation — REQUIRED Dual Verification

Every change MUST be verified in **TWO conditions**:

### 1. With feature ON
```bash
cmake -DPPSSPP_<FEATURE>=ON .. && make -j$(nproc)
```
### 2. With feature OFF
```bash
cmake -DPPSSPP_<FEATURE>=OFF .. && make -j$(nproc)
```

**Rules:**
- Both must build successfully with no errors.
- No new warnings in upstream code in either condition.
- All new files must be added to CMakeLists.txt or the appropriate build system.
- Default: feature **ON** for development, **OFF** must still build.

## Commit Messages
- Format: `[feature/<name>] <message>`
  - Example: `[lansync] Fix peer discovery timeout`
- For upstream merges: `chore: merge upstream/master`
