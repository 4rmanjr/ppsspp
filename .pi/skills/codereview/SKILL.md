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

7. **Report findings:**
   - List each issue with file path, line number, category
   - For P1-P3 issues: suggest the fix
   - For P4-P5: describe the concern
   - If no issues found: report "✅ All clear"

## Important rules

- Be thorough. Check every changed line, not just the diff overview.
- Trace full code paths, not just isolated changes.
- The user should not need to ask follow-up questions — the review should be complete and actionable.
- If you find a P1 bug, fix it immediately and report the fix.
- Reference AGENTS.md rules by name when flagging violations.
