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
| [`docs/agents/codereview-procedure.md`](docs/agents/codereview-procedure.md) | **Code Review Procedure**: Automated compliance verification, smart build decision, PSP parity checks |
| [`docs/agents/extensibility.md`](docs/agents/extensibility.md) | **Extensibility Architecture**: CoreButtonRegistry, generic classes, step-by-step new core guide |
| [`docs/agents/code-standards.md`](docs/agents/code-standards.md) | C++ coding standards, naming, platform handling |
| [`docs/agents/feature-template.md`](docs/agents/feature-template.md) | Guide for adding new features |
| [`docs/agents/fork-maintenance.md`](docs/agents/fork-maintenance.md) | Upstream merge strategy, conflict handling |
| [`docs/agents/lansync-development.md`](docs/agents/lansync-development.md) | LAN sync specific rules |

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

### 🟢 Platform Parity (Linux & Android)
- ✅ Fitur kustom harus berfungsi di **Linux dan Android** dengan kualitas setara.
- ✅ Jika fitur menggunakan platform-specific API (mDNS, key storage), harus punya **fallback** di platform lain.
- ✅ UI dialogs harus tersedia di kedua platform (native Android UI atau ImGui fallback).
- ✅ Tidak ada kode yang hanya aktif di satu platform tanpa justifikasi tertulis.
- ✅ Core logic (sync protocol, conflict resolution, file transfer) harus **identik** di kedua platform — hanya UI layer yang boleh berbeda.

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

> **Signing:** Gunakan **project-level debug keystore** yang sudah di-commit (`debug.keystore` di root project).
> ⚠️ **JANGAN pernah build `assembleGoldDebug`** — selalu pakai `assembleGoldRelease`.
> Setelah build, align + sign dengan `apksigner` (butuh v2/v3 untuk Android 11+):
> ```bash
> zipalign -f -p 4 \
>   android/build/outputs/apk/gold/release/android-gold-release-unsigned.apk \
>   android/build/outputs/apk/gold/release/android-gold-release-aligned.apk
>
> apksigner sign \
>   --ks debug.keystore \
>   --ks-pass pass:android \
>   --ks-key-alias debug \
>   --key-pass pass:android \
>   android/build/outputs/apk/gold/release/android-gold-release-aligned.apk
> ```
> Alias: `debug` (cek dengan `keytool -list -keystore debug.keystore -storepass android`).
> `zipalign` dan `apksigner` ada di `$ANDROID_HOME/build-tools/<version>/`.

## Navigation — When to Read Which File

| Situation | Read |
|-----------|------|
| About to commit | [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #2 Code Review |
| Adding a new emulator (N64, PS1, etc.) | [`extensibility.md`](docs/agents/extensibility.md) + [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #3 |
| Unsure about constructor/class/ImageID | [`quality-gates.md`](docs/agents/quality-gates.md) — Anti-Hallucination Rule |
| Merging upstream | [`fork-maintenance.md`](docs/agents/fork-maintenance.md) |
| Code review found a bug | [`progress-gba-support.md`](docs/progress-gba-support.md) — Code Review Learning Log (auto-update skill) |
