---
name: codereview
description: Review the latest uncommitted changes or last commit for bugs, regressions, and PSP parity violations. Runs Quality Gate #2 from AGENTS.md.
---

# Code Review — Quality Gate Check

When the user types `/codereview`, perform a complete code review of the latest changes.

## What to do

1. **Determine what to review**
   - If there are uncommitted changes (`git status --short` shows modified files), review the working tree diff.
   - Otherwise, review the latest commit (`git diff HEAD~1`).

2. **Trace every code path** in the modified files:
   - Follow function calls from entry point to return
   - Check if variables are properly initialized before use
   - Verify edge cases (null checks, division-by-zero guards, empty bounds)
   - Validate coordinate math (screen space vs parent-relative)

3. **Check against AGENTS.md rules:**
   - 🔴 FORBIDDEN: No upstream code deleted/modified/restructured
   - 🔴 FORBIDDEN: No `#else`/`#endif` altering upstream flow
   - 🟢 REQUIRED: `[PPSSPP-FORK]` markers on all fork additions
   - 🟢 REQUIRED: Feature flags wrap custom code (zero upstream leakage)
   - 🟢 REQUIRED: Config isolated in separate sections

4. **Check PSP parity** (Quality Gate #1):
   - For every GBA feature changed, find the PSP equivalent and compare behavior
   - Compare: init values, edge case handling, save/exit paths, z-order, opacity
   - Document any gaps found

5. **Run anti-hallucination check:**
   - `ImageID("...")` — verify the atlas ID exists
   - `new ClassName(...)` — verify constructor signature via grep
   - `System_*`, `screenManager()`, `GetI18NCategory` — verify return types

6. **Categorize issues:**
   - P1: Compile error / crash (must fix NOW)
   - P2: Logic error (wrong behavior, incorrect coordinate math)
   - P3: PSP parity gap (behavior differs from upstream)
   - P4: Code quality (missing guard, fragile pattern)
   - P5: Nitpick (missing marker, naming, minor)

7. **Report findings** using this exact template:

```
## Review: <commit-hash-or-working-tree>

### Files changed
- <file1> — <lines changed>
- <file2> — <lines changed>

### 🔴 Issues found

| # | File:Line | Category | Description | Fix |
|---|-----------|----------|-------------|-----|
| P1 | `path/file.cpp:123` | Compile error | `var` used before init | Initialize before use |
| P2 | `path/file.cpp:456` | Logic error | Wrong formula X vs Y | Change to match PSP |
| P3 | `path/file.cpp:789` | PSP parity | Init value `false`, PSP `true` | Change to `true` |
| P4 | `path/file.cpp:101` | Code quality | Missing div-by-zero guard | Add `image->w > 0` check |
| P5 | `path/file.cpp:112` | Nitpick | Missing `[PPSSPP-FORK]` marker | Add comment |

*(If no issues, write: ✅ No issues found.)*

### ✅ PSP parity check

| Aspect | PSP | GBA (this change) | Match? |
|--------|-----|-------------------|--------|
| Init value | `true` | `true` | ✅ |
| Edge case | null check | null check | ✅ |
| Save path | `onFinish()` | `onFinish()` | ✅ |

### ✅ AGENTS.md compliance

| Rule | Status |
|------|--------|
| 🔴 FORBIDDEN #1 — Zero deletion | ✅ |
| 🔴 FORBIDDEN #2 — No #else injection | ✅ |
| 🔴 FORBIDDEN #3 — No refactoring | ✅ |
| 🟢 REQUIRED #1 — Separate files | ✅ |
| 🟢 REQUIRED #2 — Markers present | ✅ |
| 🟢 REQUIRED #3 — Feature flags | ✅ |

### 📋 Detail per issue

**P1 — `path/file.cpp:123` — Compile error**
- Problem: `var` initialized after use
- Impact: Compile crash
- Fix: Move init before the use site

**P4 — `path/file.cpp:101` — Code quality**
- Problem: Missing division-by-zero guard
- Impact: Crash if image not found
- Fix: Add `if (image && image->w > 0)`

### Summary

- **P1:** 0 | **P2:** 0 | **P3:** 0 | **P4:** 1 | **P5:** 0
- **Verdict:** ✅ Clean (or ❌ Needs fix — P1 issues remain)
```

## Important rules

- Be thorough. Check every changed line, not just the diff overview.
- Trace full code paths, not just isolated changes.
- The user should not need to ask follow-up questions — the review should be complete and actionable.
- If you find a P1 bug, fix it immediately and report the fix.
- Reference AGENTS.md rules by name when flagging violations.
