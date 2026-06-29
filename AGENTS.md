# PPSSPP Fork — Agent Instructions

This is a PPSSPP fork with custom features that **must remain compatible** with the official upstream [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp).

## Core Priorities

1. **Zero breaking change** — Never delete, modify, or restructure existing PPSSPP code.
2. **Upstream compatibility** — All changes must be mergeable with upstream without permanent conflicts.
3. **Modular** — Custom features in separate files, not interleaved with the main codebase.
4. **Preserve existing behavior** — Do not alter how the core emulator works.

## File Guidelines

| File | Contents |
|------|---------|
| **`AGENTS.md`** (this) | Priorities + FORBIDDEN/REQUIRED rules + navigation to detail rules |
| [`docs/agents/quality-gates.md`](docs/agents/quality-gates.md) | **3 Quality Gates**: PSP Feature Parity, Code Review, PSP Parity for new cores |
| [`docs/agents/extensibility.md`](docs/agents/extensibility.md) | **Extensibility Architecture**: CoreButtonRegistry, generic classes, step-by-step new core guide |
| [`docs/agents/psp-knowledge-base.md`](docs/agents/psp-knowledge-base.md) | **Catalog** of PSP-GBA mismatches that have been fixed |
| [`docs/agents/code-standards.md`](docs/agents/code-standards.md) | C++ coding standards, naming, platform handling |
| [`docs/agents/multi-core-development.md`](docs/agents/multi-core-development.md) | Multi-emulator rules (GBA, future cores) |
| [`docs/agents/feature-template.md`](docs/agents/feature-template.md) | Guide for adding new features |
| [`docs/agents/fork-maintenance.md`](docs/agents/fork-maintenance.md) | Upstream merge strategy, conflict handling |
| [`docs/agents/lansync-development.md`](docs/agents/lansync-development.md) | LAN sync specific rules |
| [`docs/progress-gba-support.md`](docs/progress-gba-support.md) | GBA feature progress & status |

**REQUIRED** — Read `quality-gates.md` before committing and `extensibility.md` before adding a new core.

**Slash command:** `/codereview` — Review latest changes for bugs, regressions, and PSP parity violations.

## Global Rules

### 🔴 FORBIDDEN
- ❌ Deleting, modifying, or restructuring existing upstream code.
- ❌ Placing custom files in core directories (`Core/`, `GPU/`, `HLE/`, `MIPS/`).
- ❌ Altering upstream control flow via `#else`/`#endif` outside custom blocks.
- ❌ Refactoring upstream code to accommodate custom code.

### 🟢 REQUIRED
- ✅ Custom code in **separate files** in non-core directories.
- ✅ Every addition to upstream files:
  1. Wrapped in `#ifdef PPSSPP_<FEATURE>` (feature-specific flag)
  2. Tagged with `// [PPSSPP-FORK] FeatureName: description`
  3. Only **adds** new lines (zero deletion)
- ✅ Build verified in **TWO conditions**: `-DPPSSPP_<FEATURE>=ON` and `=OFF` — both must succeed.
- ✅ Every new feature has its own feature flag (`PPSSPP_<NAME>`).
- ✅ On upstream merge conflict: **upstream code wins** — custom code is moved/adjusted, not the other way around.
- ✅ Per-feature settings isolated in separate config sections (don't mix with PSP).

### 🟢 Build Flag Convention

| Flag | Scope | Note |
|------|-------|------|
| `PPSSPP_MULTICORE` | Top-level — enables all multi-core | Required ON for GBA |
| `PPSSPP_GBA` | (future) | Currently still uses `PPSSPP_MULTICORE` |

- All custom files in `UI/` using `#ifdef PPSSPP_MULTICORE`: MUST have `// [PPSSPP-FORK]` marker + zero deletion + verify `#ifndef` (PSP path) still builds.
- If a feature needs separation from the multi-core umbrella, create a new flag.

## Patterns

### 🟢 IsFeature() Pattern — Reduce #ifdef at Call Site

```cpp
// Header — outside #ifdef
#ifdef PPSSPP_FEATURE
    bool IsFeature() const { return flag_ != Default; }
#else
    static constexpr bool IsFeature() { return false; }
#endif

// Call site — no #ifdef (compiler eliminates dead branch)
if (IsFeature()) { DoFeatureStuff(); }
```

### 🟢 Helper Method Extraction

If custom logic >5 lines in an upstream file, extract to a helper:

```cpp
// In upstream file (minimal call site, ≤3 lines)
#ifdef PPSSPP_FEATURE
    if (IsFeature()) { UpdateFeature(); return; }
#endif

// Helper in separate file
void EmuScreen::UpdateFeature() { /* all logic */ }
```

Convention: `Init<Feature>()`, `Shutdown<Feature>()`, `Update<Feature>()`, `Render<Feature>()`.

## Build Android APK

### Gold Release (arm64-v8a only)

```bash
cd android && ./gradlew assembleGoldRelease
```

Output APK: `android/build/outputs/apk/gold/release/android-gold-release-unsigned.apk`

> **Signing:** Gunakan **debug signing** (default Android debug keystore `~/.android/debug.keystore`).
> Cukup jalankan `jarsigner` atau gunakan `apksigner`:
> ```bash
> cd android
> ./gradlew assembleGoldDebug
> ```
> Atau untuk release unsigned + sign manual:
> ```bash
> apksigner sign --ks ~/.android/debug.keystore \
>   --ks-pass pass:android \
>   build/outputs/apk/gold/release/android-gold-release-unsigned.apk
> ```

## Navigation — When to Read Which File

| Situation | Read |
|-----------|------|
| About to commit | [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #2 Code Review |
| Implementing a new feature (GBA) | [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #1 Feature Parity |
| Adding a new emulator (N64, PS1, etc.) | [`extensibility.md`](docs/agents/extensibility.md) + [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #3 |
| Unsure about constructor/class/ImageID | [`quality-gates.md`](docs/agents/quality-gates.md) — Anti-Hallucination Rule |
| Viewing fixed PSP mismatches | [`psp-knowledge-base.md`](docs/agents/psp-knowledge-base.md) |
| Merging upstream | [`fork-maintenance.md`](docs/agents/fork-maintenance.md) |
