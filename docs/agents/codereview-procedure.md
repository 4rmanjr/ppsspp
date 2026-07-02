# Code Review Procedure — PPSSPP Fork

> **Skill:** `codereview-ppsspp-fork`  
> **Trigger:** `/codereview` command or before commit  
> **Purpose:** Automated compliance verification for PPSSPP fork rules

---

## When to Use

Run this procedure:
- **Before committing** any code changes
- When user invokes `/codereview`
- When reviewing pull requests
- After significant refactoring

Applies to any code change in PPSSPP fork to verify compliance with fork rules:
- No upstream modification
- Proper `[PPSSPP-FORK]` markers
- Correct file locations
- PSP feature parity

---

## Procedure

### PHASE 1 — FAST CHECKS (Always Run, <30s)

Quick static analysis that runs on every code review:

**1a. Git Diff Analysis**
```bash
git diff --name-status HEAD
```
Identify all changed files and classify them.

**1b. Classify Changes**
- New files added
- Modified files
- Deleted files
- `CMakeLists.txt` changes
- Header (`.h`) changes
- `#ifdef` block changes
- Documentation (`.md`) only

**1c. File Location Check**
```bash
git diff --name-only HEAD | grep -E "^(Core|GPU|HLE|MIPS)/"
```
**BLOCKER (P0):** Output MUST be empty. No files allowed in:
- `Core/`
- `GPU/`
- `HLE/`
- `MIPS/`

**Exception:** `Core/Config.h/.cpp` for minimal config hooks (<5 lines, wrapped in `#ifdef`).

**1d. Marker Check**
All new files and `#ifdef` blocks MUST have:
```cpp
// [PPSSPP-FORK] FeatureName: description
```

Verify with:
```bash
# Check new files
git diff --diff-filter=A --name-only HEAD | while read f; do
  grep -q "\[PPSSPP-FORK\]" "$f" || echo "Missing marker: $f"
done

# Check modified files for new #ifdef blocks
git diff HEAD | grep -A 2 "^+#ifdef PPSSPP_" | grep -v "\[PPSSPP-FORK\]"
```

**1e. Upstream Protection**
```bash
git diff HEAD -- Core/ GPU/ HLE/ MIPS/ | grep "^-"
```
**BLOCKER (P0):** No deletions (`-` lines) allowed in upstream directories.  
**Only additions** (`+` lines) are permitted, and they MUST be:
- Wrapped in `#ifdef PPSSPP_<FEATURE>`
- Maximum 5 lines per hook
- Tagged with `[PPSSPP-FORK]` marker

**1f. Naming Convention**
New classes MUST use **Core** prefix (extensibility), not `GBA`/`N64`/`PS1`:
- ✅ `CoreDragDrop`, `CoreLayoutView`, `CoreTouchConfig`
- ❌ `GBADragDrop`, `N64LayoutView`, `PS1TouchConfig`

Check with:
```bash
git diff HEAD | grep "^+class.*GBA\|^+class.*N64\|^+class.*PS1"
```

**1g. Pattern Check**
Verify required patterns:

**IsFeature() Pattern:**
```cpp
#ifdef PPSSPP_FEATURE
    bool IsFeature() const;
#else
    static constexpr bool IsFeature() { return false; }
#endif
```

**Helper Extraction:**
If custom logic >5 lines in upstream file, MUST be extracted to helper:
```cpp
// In upstream file (≤3 lines)
#ifdef PPSSPP_FEATURE
    if (IsFeature()) { UpdateFeature(); return; }
#endif
```

---

### PHASE 2 — SMART BUILD DECISION (Conditional)

Dual-build verification takes **10-30 minutes**. Only run when truly needed.

**2a. SKIP dual-build if ALL conditions are true:**
- ✅ No `CMakeLists.txt` changes
- ✅ No new files added
- ✅ No header (`.h`) changes
- ✅ No `#ifdef PPSSPP_*` block changes
- ✅ Only `.cpp` implementation changes
- ✅ Only documentation (`.md`) changes

**2b. REQUIRE dual-build if ANY condition is true:**
- ⚠️ `CMakeLists.txt` modified
- ⚠️ New source files added
- ⚠️ Header files (`.h`) modified
- ⚠️ New `#ifdef PPSSPP_*` blocks added
- ⚠️ Feature flag usage changed

**2c. Manual Override:**
- `--skip-build` — Force skip (developer override)
- `--force-build` — Force run (pre-merge verification)

**Detection Script:**
```bash
BUILD_REQUIRED=0

# Check CMakeLists
git diff --name-only HEAD | grep -q "CMakeLists.txt" && BUILD_REQUIRED=1

# Check new files
[ $(git diff --diff-filter=A --name-only HEAD | wc -l) -gt 0 ] && BUILD_REQUIRED=1

# Check headers
git diff --name-only HEAD | grep -q "\.h$" && BUILD_REQUIRED=1

# Check #ifdef blocks
git diff HEAD | grep -q "^+#ifdef PPSSPP_" && BUILD_REQUIRED=1

if [ $BUILD_REQUIRED -eq 0 ]; then
    echo "✅ Dual-build SKIPPED (only safe changes)"
else
    echo "⚠️ Dual-build REQUIRED"
fi
```

---

### PHASE 3 — PSP PARITY CHECK (Conditional)

**Only run if** these files changed:
- `EmuCore/`
- `UI/EmuScreen.cpp`
- `UI/CoreTouchLayoutScreen.cpp`
- `UI/GamepadEmu.cpp`

**3a. Trigger Detection**
```bash
git diff --name-only HEAD | grep -E "(EmuCore/|UI/EmuScreen|UI/CoreTouchLayoutScreen|UI/GamepadEmu)"
```

**3b. Identify PSP Equivalent**
From code context, determine PSP equivalent feature:

| Custom Core Feature | PSP Equivalent |
|---------------------|----------------|
| Touch layout editor | `UI/TouchControlLayoutScreen.cpp` |
| Touch button rendering | `UI/GamepadEmu.cpp::CreatePadLayout()` |
| Touch visibility popup | `UI/TouchControlVisibilityScreen.cpp` |
| Save state | `UI/SavedataScreen.cpp` |
| Settings screen | `UI/GameSettingsScreen.cpp` |

**3c. Behavior Checklist**
Compare one-by-one with PSP:

| Aspect | PSP | Custom Core | Match? |
|--------|-----|-------------|--------|
| Initialization | | | |
| Update loop | | | |
| Rendering | | | |
| Save/Load | | | |
| User interaction | | | |
| Edge cases | | | |

**3d. Check Knowledge Base**
```bash
grep -i "<feature_name>" docs/agents/psp-knowledge-base.md
```
Look for known patterns and common pitfalls.

**3e. Flag Mismatches**
Any behavioral difference = **P1 (Major)** issue.

Log new mismatches to `docs/agents/psp-knowledge-base.md`:
```markdown
## <Feature> <aspect> mismatch — ❌ Found / ✅ Fixed

- **File:** `path/to/file.cpp` → `ClassName::Method()`
- **Bug:** <description>
- **Impact:** <user-facing impact>
- **Fix:** <what was changed>
- **Lesson:** <takeaway for future>
- **Date:** YYYY-MM-DD
- **Commit:** `<hash>`
```

---

### PHASE 4 — GENERATE REPORT

**4a. Severity Classification**

| Level | Criteria | Examples | Action |
|-------|----------|----------|--------|
| **P0 (Blocker)** | Breaks fork rules | Upstream modification, `Core/` file placement, missing feature flag | **BLOCK COMMIT** |
| **P1 (Major)** | Functional issue | Memory leak, PSP parity violation, missing `[PPSSPP-FORK]` marker | Fix before merge |
| **P2 (Minor)** | Code quality | Style violation, missing comment, inconsistent naming | Fix when convenient |
| **P3 (Nit)** | Cosmetic | Typo, formatting, extra whitespace | Optional |

**4b. Finding Format**
For each issue:
```
[P0] File: path/to/file.cpp:123
Issue: File in forbidden directory Core/
Recommendation: Move to EmuCore/ or separate directory
```

**4c. Summary**
```
Code Review Summary
===================
Status: PASS / FAIL
P0 Issues: 0 (must be 0 to pass)
P1 Issues: 2
P2 Issues: 5
P3 Issues: 1

Build Requirement: SKIPPED (only .cpp impl changes)
Estimated Fix Time: 30 minutes

Next Steps:
1. Move Core/CustomFile.cpp to EmuCore/
2. Add [PPSSPP-FORK] markers to 2 files
3. Fix PSP parity: nextToggleAll_ init value
```

**4d. Actionable Recommendations**
Each finding MUST have:
- Clear description of the problem
- Specific location (file, line)
- Concrete fix recommendation
- Estimated time to fix

---

### PHASE 5 — OPTIONAL DUAL-BUILD (If Required)

**5a. Inform User**
```
⚠️ Dual-build verification required because:
  - CMakeLists.txt was modified
  - 3 new header files added

Estimated time: 15-20 minutes

Options:
  [1] Run now (recommended before commit)
  [2] Skip (use --skip-build, manual verification needed)
  [3] Defer (run in CI)

Choice:
```

**5b. Ask Confirmation**
Do not proceed without user consent (time-consuming operation).

**5c. Execute Dual-Build**

**Build 1: Feature ON**
```bash
echo "Building with PPSSPP_MULTICORE=ON..."
rm -rf build-verify-on
cmake -B build-verify-on -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=ON
cmake --build build-verify-on --target PPSSPPSDL -j$(nproc) 2>&1 | tee build-on.log

if [ $? -eq 0 ]; then
    echo "✅ Build ON: PASS"
else
    echo "❌ Build ON: FAIL (see build-on.log)"
fi
```

**Build 2: Feature OFF**
```bash
echo "Building with PPSSPP_MULTICORE=OFF..."
rm -rf build-verify-off
cmake -B build-verify-off -DCMAKE_BUILD_TYPE=Release -DPPSSPP_MULTICORE=OFF
cmake --build build-verify-off --target PPSSPPSDL -j$(nproc) 2>&1 | tee build-off.log

if [ $? -eq 0 ]; then
    echo "✅ Build OFF: PASS"
else
    echo "❌ Build OFF: FAIL (see build-off.log)"
fi
```

**5d. Report Results**
```
Dual-Build Verification Results
================================
Build ON (PPSSPP_MULTICORE=ON):  ✅ PASS (3m 42s)
Build OFF (PPSSPP_MULTICORE=OFF): ✅ PASS (3m 38s)

Status: ✅ VERIFIED
Both builds successful. Safe to commit.
```

Or if failed:
```
Build ON:  ✅ PASS
Build OFF: ❌ FAIL

Errors in build-off.log:
  - EmuCore/GBACore.cpp:45: undefined reference to mGBA_init
  - Cause: Missing #ifdef guard around mGBA usage

Action Required: Wrap GBA-specific code in #ifdef PPSSPP_MULTICORE
```

---

## Pitfalls

### 1. Do NOT Run Dual-Build by Default
**Problem:** Dual-build takes 10-30 minutes.  
**Rule:** Only suggest when truly needed (CMakeLists, headers, new files, `#ifdef` changes).  
**Detection:** Use smart build decision (Phase 2).

### 2. Do NOT Treat All Issues as Equal
**Problem:** Style nit treated same as upstream modification.  
**Rule:** P0 blocks commit, P3 is optional.  
**Classification:** Use severity matrix (Phase 4a).

### 3. Do NOT Assume PSP Parity Applies Everywhere
**Problem:** Checking PSP parity for non-UI files wastes time.  
**Rule:** Only check `EmuCore/*` and UI touch/input files.  
**Trigger:** Conditional check (Phase 3a).

### 4. Do NOT Fail on Intentional Hooks
**Problem:** Flagging legitimate `[PPSSPP-FORK]` hooks in upstream files.  
**Rule:** Verify hook is wrapped in `#ifdef` and <5 lines before failing.  
**Exception:** Minimal hooks in `Core/Config.h` are allowed.

### 5. Do NOT Require Build for Docs
**Problem:** Running 20-minute build for `.md` file changes.  
**Rule:** Skip build if only documentation changed.  
**Detection:** `git diff --name-only HEAD | grep -v "\.md$"`

### 6. Do NOT Review Huge Diffs at Once
**Problem:** 50+ files changed, output is overwhelming.  
**Rule:** Ask user to specify subset (e.g., "review only EmuCore/").  
**Threshold:** If `git diff --name-only | wc -l > 50`.

### 7. Do NOT Hallucinate File Locations
**Problem:** Reporting issues in wrong files or non-existent line numbers.  
**Rule:** Always verify with actual `git diff` output.  
**Validation:** Cross-reference every finding with `git diff` before reporting.

---

## Verification

After running code review procedure, verify:

### 1. Git Diff Correctly Identified
```bash
git diff --name-status HEAD
# Output matches classified changes
```

### 2. No Forbidden File Locations
```bash
git diff --name-only HEAD | grep -E "^(Core|GPU|HLE|MIPS)/" | wc -l
# Output: 0 (or only allowed exceptions)
```

### 3. All P0 Issues Identified
Review report for:
- Upstream modifications (deletions in `Core/`, `GPU/`, `HLE/`, `MIPS/`)
- Files in forbidden directories
- Missing feature flags

P0 count MUST be 0 to pass.

### 4. Build Requirement Correct
- If only `.cpp` impl changes → build SKIPPED ✅
- If `CMakeLists.txt` or headers changed → build REQUIRED ✅

### 5. PSP Parity Check Triggered Correctly
- Only runs for `EmuCore/`, `UI/EmuScreen.cpp`, `UI/CoreTouchLayoutScreen.cpp`, `UI/GamepadEmu.cpp`
- Skips for other files

### 6. Report Shows Severity Classification
```
P0: 0 (blocker)
P1: 2 (major)
P2: 5 (minor)
P3: 1 (nit)
```
With clear next steps for each severity level.

### 7. Dual-Build Informed with Reason
If build required, user sees:
```
⚠️ Dual-build required because:
  - CMakeLists.txt modified
```
Not just "build required" without explanation.

---

## Quick Reference

### Common Commands

**Run full code review:**
```bash
/codereview
```

**Run with build override:**
```bash
/codereview --skip-build   # Skip dual-build
/codereview --force-build  # Force dual-build
```

**Check specific files only:**
```bash
/codereview EmuCore/ UI/EmuScreen.cpp
```

**Manual verification:**
```bash
# Check file locations
git diff --name-only HEAD | grep -E "^(Core|GPU|HLE|MIPS)/"

# Check markers
git diff HEAD | grep -A 2 "^+#ifdef PPSSPP_" | grep "\[PPSSPP-FORK\]"

# Check upstream protection
git diff HEAD -- Core/ GPU/ HLE/ MIPS/ | grep "^-"
```

### Severity Quick Reference

| Level | Action | When to Fix |
|-------|--------|-------------|
| P0 | Block commit | Immediately |
| P1 | Fix before merge | Before PR approval |
| P2 | Fix when convenient | Next sprint |
| P3 | Optional | Cleanup task |

---

## Integration

### Pre-Commit Hook
Create `.git/hooks/pre-commit`:
```bash
#!/bin/bash
# Run code review before every commit
/codereview || {
    echo "❌ Code review failed. Commit blocked."
    echo "Fix P0 issues or use --no-verify to skip."
    exit 1
}
```

### CI Integration
Add to `.github/workflows/code-review.yml`:
```yaml
name: Code Review
on: [pull_request]
jobs:
  review:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Run Code Review
        run: /codereview --force-build
      - name: Block if P0 Issues
        run: |
          if grep -q "P0 Issues: [1-9]" review-report.txt; then
            echo "❌ P0 issues found. PR blocked."
            exit 1
          fi
```

---

## Related Documentation

- [AGENTS.md](../AGENTS.md) — Overview and navigation
- [quality-gates.md](quality-gates.md) — Quality Gate #2 (this procedure implements it)
- [psp-knowledge-base.md](psp-knowledge-base.md) — Known PSP parity issues
- [fork-maintenance.md](fork-maintenance.md) — Upstream merge strategy
- [extensibility.md](extensibility.md) — Architecture patterns to verify
