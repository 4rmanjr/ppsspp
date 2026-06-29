---
name: codereview
description: Review the latest changes for bugs, regressions, and PSP parity violations across ALL cores (GBA, N64, PS1, NDS, etc.). Runs Quality Gate #2 from AGENTS.md.
---

# Code Review — Quality Gate Check (All Cores)

When the user types `/codereview`, perform a complete code review of the latest changes. This skill is **core-agnostic** — it applies to any non-PSP emulator (GBA, N64, PS1, NDS, SNES, etc.).

## What to do

1. **Determine what to review**
   - If there are uncommitted changes (`git status --short` shows modified files), review the working tree diff.
   - Otherwise, review the latest commit (`git diff HEAD~1`).

2. **Trace every code path** in the modified files:
   - Follow function calls from entry point to return
   - Check if variables are properly initialized before use
   - Verify edge cases (null checks, division-by-zero guards, empty bounds, invalid indices)
   - Validate coordinate math (screen space vs parent-relative)
   - **`.size()` type check:** Before reading code that calls `.size()` on a value, verify the type supports it. Plain C arrays (`Type arr[N]`) have NO `.size()` member — this is a P1 compile error. When in doubt, `grep -n 'arr\['` on the declaration to confirm it's a vector, not a C array.
   - **`std::string_view` lifetime check:** When instantiating `ImageID` (which wraps `std::string_view`), verify the target string's lifetime. Creating `ImageID` from a temporary string concatenation (e.g. `std::string(def->bgID) + "_LINE"`) creates a dangling pointer when the temporary string is destroyed. Use static string literals or persistent mappings instead. Flag P1/P2 if found.
   - **C++ Memory Management:** Verify that raw pointers instantiated with `new` (especially UI views/components) are properly owned by smart pointers or registered with a parent container (e.g. `root_->Add(...)`, `parent->Add(...)`) which manages their deletion. Unmanaged raw pointers cause leaks. Flag P4 if found.
   - **Division-by-zero in GLSL:** Scan `.fsh` files for `1.0 /` or `1.0f /` expressions. Verify the divisor is guarded (e.g., `if (gamma > 0.01)` or slider range > 0). If unguarded, flag P4.
   - **Cross-file string consistency:** When code matches on string literals (e.g., `info.section == "GBALCD"`), verify the actual value by reading the source that produces it — INI section name, enum stringification, const/define. A mismatch is P2 (silent logic error).
   - **Config field completeness:** For every new field added to `Config.h`, verify:
     * ✅ Saved in `Config.cpp` (Set call)
     * ✅ Loaded in `Config.cpp` (Get call)
     * ✅ Default value matches between declaration and UI slider default
     * ✅ UI control exists (if user-facing) or is intentionally internal

3. **Check against AGENTS.md rules:**
   - 🔴 FORBIDDEN: No upstream code deleted/modified/restructured
   - 🔴 FORBIDDEN: No `#else`/`#endif` altering upstream flow
   - 🟢 REQUIRED: `[PPSSPP-FORK]` markers on all fork additions
   - 🟢 REQUIRED: Feature flags wrap custom code (zero upstream leakage)
   - 🟢 REQUIRED: Config isolated in separate sections

4. **Check PSP parity** (Quality Gate #1):
   - For every core feature changed, find the PSP equivalent and compare behavior
   - **Compare EVERY parameter** in Draw() calls — colors, opacity, scale, rotation — not just structure
   - **Never assume** global state (e.g., `GamepadUpdateOpacity`) affects a rendering API — verify by reading the API implementation or comparing PSP's explicit parameter usage
   - Compare: init values, edge case handling, save/exit paths, z-order, opacity
   - If no PSP equivalent exists (e.g., a core-specific feature like low-level audio), verify against established patterns instead
   - Document any gaps found

5. **Run anti-hallucination check:**
   - `ImageID("...")` — verify the atlas ID exists
   - `new ClassName(...)` — verify constructor signature via grep
   - `System_*`, `screenManager()`, `GetI18NCategory` — verify return types
   - **Rendering API** — `dc.Draw()->DrawImageRotated(...)` — verify EACH parameter:
     * Color: is opacity/multiply color used? Or hardcoded `0xFFFFFFFF`?
     * Position: same coordinate space as PSP?
     * Scale: same semantics as PSP?
     * Rotation: same angle convention?

6. **Fix verification** (ONLY if a P1/P2 fix was applied):
   - Re-read the fixed code block with its surrounding context.
   - Check: does the fix introduce ANY new code path not present before? (Expected for functional fixes; unexpected for surface-level fixes like `.size()` → `4`).
   - Check: are the old callers that relied on the buggy behavior now broken? (Trace callers of the fixed function).
   - Check: if loop bounds changed, does the new bound exceed array allocation? `for (s = 0; s < N; s++)` must have `array[N]` or larger.
   - Check: if the fix is in a shader, does the new calculation preserve output range (no NaN/Inf in GLSL)?
   - Check: does the fix maintain PSP parity? Compare with upstream PPSSPP's equivalent code path.
   - If any of the above fails, P1 — revert and rethink the fix.

7. **Categorize issues:**
   - P1: Compile error / crash (must fix NOW)
   - P2: Logic error (wrong behavior, incorrect coordinate math)
   - P3: PSP parity gap (behavior differs from upstream)
   - P4: Code quality (missing guard, fragile pattern)
   - P5: Nitpick (missing marker, naming, minor)

8. **Report findings** using this exact template:

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

*(For each feature changed, compare against the equivalent PSP implementation.)*

| Aspect | PSP | Core (this change) | Match? |
|--------|-----|-------------------|--------|
| Init value | `true` | `true` | ✅ |
| Edge case | null check | null check | ✅ |
| Save path | `onFinish()` | `onFinish()` | ✅ |

*(If no PSP equivalent exists for the changed code, mark as N/A.)*

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
- **Never assume API behavior.** If you think a global state affects a rendering call, verify by:
  1. Reading the PSP reference and checking if PSP explicitly passes the parameter
  2. If PSP passes it explicitly (e.g., `colorAlpha(..., opacity)`), then the forked version MUST also pass it explicitly
  3. Do NOT assume `GamepadUpdateOpacity()` or similar global state automatically applies to all draw calls
- **Parameter-level comparison:** When comparing PSP vs forked Draw() overrides, compare every single argument passed to every API call. "Similar structure" is NOT sufficient — check colors, opacity, scale, rotation individually.
- The user should not need to ask follow-up questions — the review should be complete and actionable.
- If you find a P1 bug, fix it immediately and report the fix.
- Reference AGENTS.md rules by name when flagging violations.
