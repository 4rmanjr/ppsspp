# Quality Gates — PSP Parity & Code Review

> **Referenced by:** `AGENTS.md`
> **Applies to:** All GBA features and new emulators (N64, PS1, etc.)
> **Purpose:** Guarantee no behavioral gap with PSP, no hallucination, and clean code.

---

## Table of Contents

- [Gate #1: Feature Parity with PSP](#-quality-gate-1-feature-parity-with-psp)
- [Gate #2: Code Review Gate](#-quality-gate-2-code-review-gate)
- [Gate #3: PSP Parity Required for Every New Core](#-quality-gate-3-psp-parity-required-for-every-new-core)
- [Supporting Rules](#supporting-rules)

---

## 🔵 Quality Gate #1 — Feature Parity with PSP

Every GBA (or other core) feature that has a PSP equivalent MUST follow these rules.

### 1. Read PSP Reference (Required)

BEFORE writing code, read the equivalent PSP implementation:

```bash
grep -n "<function/class_name>" UI/<psp_file>.cpp | head -20
```

Understand:
- How PSP does init, update, render
- How PSP handles edge cases (null, invalid state, boundary)
- How PSP handles user interaction (Touch, Click)

### 2. Feature Parity Checklist (Required)

Compare behavior one-by-one with PSP. Example template:

| Aspect | PSP (reference) | Core (this change) | Match? |
|--------|--------------------------------------|----------------------------------|--------|
| nextToggleAll_ init | `true` | `true` | ✅ |
| Toggle All scope | All buttons (game + system) | All buttons | ✅ |
| SetMinimumAlpha | Unconditional | Unconditional | ✅ |
| Pause disable logic | Via `System_GetPropertyBool` | Same | ✅ |
| Save on exit | `onFinish()` (all paths) | `OnCompleted()` (all paths) | ✅ |

**Record every gap as a task** — do not skip even small ones.

### 3. Identify PSP-Specific Logic (Required)

Not everything in PSP applies to other cores. Filter:

| ❌ Don't Copy | ✅ Do Copy |
|---------------|-----------|
| PSP-only buttons (Square, Triangle, Analog Stick) | Shared behavior (Toggle All, save on exit, visibility toggles) |
| Analog deadzone, pressure sensitivity | Shared config (`TouchControlConfig`: Fast-forward, Pause) |

### 4. Log Mismatches to PSP Knowledge Base

Every time you find an unintentional behavioral difference (bug), log it in [`psp-knowledge-base.md`](psp-knowledge-base.md):

```
# File: UI/CoreTouchLayoutScreen.cpp
# Bug: nextToggleAll_ started false, PSP starts true
# Fix: false → true
# Date: 2026-06-25
```

---

## 🟠 Quality Gate #2 — Code Review Gate

MUST be run before every commit.

### Pre-Commit Checklist

```
[ ] 1. Unified diff review — no upstream changes
[ ] 2. Edge cases:
     - Null/empty bounds → w=0, h=0
     - Division-by-zero → image->w == 0 check
     - Uninitialized config → fallback values
     - Platform-specific → System_GetPropertyBool check
[ ] 3. PSP reference — all behaviors match?
[ ] 4. Compile — feature ON ✅ | feature OFF ✅
[ ] 5. Naming convention — [PPSSPP-FORK] marker present?
[ ] 6. IsFeature() pattern — call site without #ifdef?
[ ] 7. Helper method extraction — logic >5 lines separated?
```

### Post-Commit Diff Verification

```bash
git diff HEAD~1 -- UI/ UI/EmuCore/ EmuCore/
# Verify no upstream files changed unintentionally
```

---

## 🔷 Quality Gate #3 — PSP Parity Required for Every New Core

Every new emulator (N64, PS1, NDS, SNES, etc.) MUST be treated the SAME as GBA:

| Rule | GBA | N64 (future) | PS1 (future) |
|------|-----|-------------|-------------|
| Quality Gate #1 (Feature Parity) | ✅ | ✅ REQUIRED | ✅ REQUIRED |
| Quality Gate #2 (Code Review) | ✅ | ✅ REQUIRED | ✅ REQUIRED |
| Scope Definition | ✅ | ✅ REQUIRED | ✅ REQUIRED |
| Log mismatches to Knowledge Base | ✅ | ✅ REQUIRED | ✅ REQUIRED |
| Verify PSP equivalent | ✅ | ✅ REQUIRED | ✅ REQUIRED |
| Build dual verification | ✅ | ✅ REQUIRED | ✅ REQUIRED |

**Example:** If N64 has an equivalent PSP feature (e.g., analog stick), its behavior MUST match one-by-one. If PS1 has L1/L2/R1/R2, match PSP L/R trigger behavior.

---

## Supporting Rules

### 🔵 Scope Definition (Required Before Implementation)

Before writing code, write a scope definition:

```
## Scope: <Feature Name>

### What will be built
- [ ] Pause button on non-PSP game screen
- [ ] Fast-forward toggle in visibility popup

### PSP equivalent
- UI/GamepadEmu.cpp::CreatePadLayout() — Pause button
- UI/TouchControlVisibilityScreen — system button toggles

### Boundaries (What will NOT be built)
- ❌ Not creating PSP-only buttons (Square, Triangle, Analog)
- ❌ Not creating a new settings screen (use existing)

### Edge cases to handle
- [ ] Device without back button → Pause button minimum alpha
- [ ] Uninitialized TouchControlConfig → InitPadLayout
```

### 🟠 Anti-Hallucination Rule

> **NEVER** guess API signatures without verification.

Whenever writing:

| If writing... | MUST check... |
|---------------|---------------|
| `new ClassName(...)` | Constructor in `.h` file via `grep` / `read` |
| `ImageID("...")` | Whether the ID is registered in the texture atlas |
| `#include "..."` | Whether the file exists |
| `System_*` / `GetI18NCategory` / `screenManager()` | Return type from header |

If verification is impossible (file too large), **ask the user**. Don't guess.
