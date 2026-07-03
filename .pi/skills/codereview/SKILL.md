---
name: codereview
description: Review the latest changes for bugs, regressions, and PSP parity violations across ALL cores (N64, PS1, NDS, etc.). Runs Quality Gate #2 from AGENTS.md.
---

// [PPSSPP-FORK] Codereview: code review skill

# Code Review — Quality Gate Check (All Cores)

When the user types `/codereview`, perform a complete code review of the latest changes. This skill is **core-agnostic** — it applies to any non-PSP emulator (N64, PS1, NDS, SNES, etc.).

## What to do

0. **Build Gate — compile first**
   - **Smart Build Decision — Analyze changes first:**
     * Run `git diff --name-only HEAD` (or `git status --short` if uncommitted) to identify changed files
     * Classify file types:
       - **Docs-only:** `.md`, `.txt`, files in `docs/`, `README`
       - **Safe:** Comment-only changes, whitespace, formatting (non-semantic only — must not change code behavior)
       - **Structural:** `CMakeLists.txt`, `.h` headers, new source files, `#ifdef PPSSPP_*` blocks
       - **Implementation:** `.cpp` files only (no headers)
     * **Decision logic:**
       - **SKIP build** if changes are ONLY docs/comments → Report: "⚠️ Build skipped — documentation-only changes (saved 15-20 min)"
       - **REQUIRE build** if ANY structural changes (CMakeLists, headers, new files, #ifdef blocks)
       - **CONDITIONAL** if only .cpp implementation (no headers) → Default SKIP (low risk), but flag for user awareness
     * **Override:** User can force build with explicit request, or skip with confirmation
   - **Check build capability:** Run `which cmake 2>/dev/null`. If `cmake` is not found → skip the build gate entirely (e.g., Termux or other constrained environments). Note in the report: "⚠️ Build gate skipped — cmake not available."
   - **Auto-detect build directory:** If `cmake` is available, find the most appropriate build directory:
     * Priority 1: `build` (primary dev build).
     * Priority 2: `build-*` with most recent `CMakeCache.txt` (fallback).
     * Verify the detected dir has `CMakeCache.txt`. If none found, skip the build gate with a note.
   - Run `cmake --build <detected_dir> -j$(nproc) 2>&1 | tail -30`. Check for compile errors.
   - If compile errors → **STOP**. Report as P1. Do NOT proceed with further review until fixed.
   - Check total warning count as a baseline (compare with prior review if available). If warnings increased, flag P4.

   - **Test Gate — run relevant tests:** After the build succeeds (or if build was skipped), identify tests related to changed files (`grep -rn 'TestFunc\|<test_name>' unittest/`). Run `python test.py` for headless PSP/core logic tests (uses `<detected_dir>/PPSSPPSDL`; if the binary is missing, skip with a note). For unit tests: check if `<detected_dir>/PPSSPPUnitTest` exists before attempting to run. If tests fail → **STOP**. Report with the full test output.

1. **Determine what to review**
   - If there are uncommitted changes (`git status --short` shows modified files), review the working tree diff.
   - Otherwise, review the latest commit (`git diff HEAD~1`).
   - **Commit message check:** Format must follow one of: `[feature/<name>] <desc>`, `fix(<scope>): <desc>`, `docs: <desc>`, or `feat(<scope>): <desc>`. If not → P5.
   - **Diff health check:**
     * `git diff --check` — whitespace errors? Flag P5.
     * `grep '<<<<<<< \|=======\|>>>>>>> '` — unresolved merge markers? Flag P1.
     * Files changed > 20? Suggest splitting the commit.
     * **Code churn:** If any single file has >200 lines added+removed, flag for careful review. Large diffs have higher probability of hidden bugs.

2. **Trace every code path** in the modified files:
   - Follow function calls from entry point to return
   - Check if variables are properly initialized before use
   - Verify edge cases (null checks, division-by-zero guards, empty bounds, invalid indices)
   - Validate coordinate math (screen space vs parent-relative)
   - **`.size()` type check:** Before reading code that calls `.size()` on a value, verify the type supports it. Plain C arrays (`Type arr[N]`) have NO `.size()` member — this is a P1 compile error. When in doubt, `grep -n 'arr\['` on the declaration to confirm it's a vector, not a C array.
   - **`std::string_view` lifetime check:** When instantiating `ImageID` (which wraps `std::string_view`), verify the target string's lifetime. Creating `ImageID` from a temporary string concatenation (e.g. `std::string(def->bgID) + "_LINE"`) creates a dangling pointer when the temporary string is destroyed. Use static string literals or persistent mappings instead. Flag P1/P2 if found.
   - **Division-by-zero in GLSL:** Scan `.fsh` files for `1.0 /` or `1.0f /` expressions. Verify the divisor is guarded (e.g., `if (gamma > 0.01)` or slider range > 0). If unguarded, flag P4.
    - **Cross-file string consistency:** When code matches on string literals (e.g., `info.section == "SAVEDATA"`), verify the actual value by reading the source that produces it — INI section name, enum stringification, const/define. A mismatch is P2 (silent logic error).
    - **Config field completeness:** For every new field added to `Config.h`, verify:
      * ✅ Saved in `Config.cpp` (Set call)
      * ✅ Loaded in `Config.cpp` (Get call)
      * ✅ Default value matches between declaration and UI slider default
      * ✅ UI control exists (if user-facing) or is intentionally internal
    - **Config migration check:** When removing/renaming existing config fields, existing user config files become stale. Verify forward-compat handling (load old key and migrate, or provide fallback default). Flag P4 if missing.
    - **Implicit narrowing:** Check for `size_t → int`, `u32 → int`, `int → float` in new arithmetic. C++ implicit narrowing causes silent precision loss. Flag P4.
    - **`const` correctness:** New member functions that don't modify `this` should be `const`. Flag P5.
    - **Virtual destructor:** Every new class with `virtual` methods must have a `virtual` destructor. Flag P4 (UB on delete).
    - **`std::move` on const:** `std::move(const T)` silently degrades to copy. Verify the source is non-const. Flag P4.
    - **Noexcept on callbacks:** Audio callbacks, IRQ handlers, signal handlers must be `noexcept`. Flag P4.
    - **Lock ordering / deadlock:** When adding new mutex locks, verify lock order is consistent with existing locks in the same call chain. Inconsistent ordering → deadlock risk. Flag P2.
    - **Static init order:** Global `static X` that depends on `static Y` in another translation unit is undefined (static init order fiasco). Use function-local statics instead. Flag P2.
    - **Semantic field verification:** When modifying a formula that reads a config/struct/bounds field, verify the field's semantics by reading its declaration (`.h` file) AND how the PSP equivalent uses it. Common pitfalls in this codebase:
      * `btn_.w` / `btn_.h` is **normalized width** (0-1 = 0%-100% of screen width), NOT a scale factor or pixel value.
      * `bounds` fields may be **parent-relative** vs **screen-absolute** — verify the coordinate space before math.
      * Config values often have implicit units (pixels, normalized, milliseconds, frames) — always check against the PSP reference.
      * Flag P2 if a formula clearly misinterprets a field's semantics (e.g., multiplying where division is needed, or treating normalized range as pixel).

3. **Memory safety & security checks** — Run these on every modified file:
   - **Buffer bounds:** For every `memcpy`, `memset`, `strcpy`, `sprintf`, verify the destination has enough space. ROM parsing code is especially risky — input is untrusted. Flag any copy where the size comes from ROM data without a `min(size, MAX)` clamp.
   - **Integer overflow:** Check arithmetic on addresses, offsets, and sizes used in emulated memory access. `u32 addr = base + offset` can silently wrap if not guarded. Flag any `P1` if the result indexes into a real buffer.
   - **Use-after-free / dangling pointer:** Look for pointers stored across frame boundaries (e.g., cached `Core*` or `Texture*`). If the pointee can be destroyed while the pointer is live (e.g., core reset, texture cache flush), flag P1.
   - **Signed/unsigned mismatch in comparisons:** `if (idx < arr.size())` where `idx` is `int` silently wraps for negative values. Verify loop indices are unsigned or guarded with `>= 0`.
   - **Undefined behavior (C++ UB):** Flag signed integer overflow, left-shift into sign bit, and `reinterpret_cast` patterns that violate strict aliasing. These are P2 — they compile and run until optimizations break them.
   - **Endianness:** When touching memory read/write helpers (e.g., `Memory::Read32`, `MMU::Write16`), verify the byte-swap macro matches the target CPU's endianness. A missing `bswap` on a big-endian core (N64, GC) silently corrupts data.
   - **`atoi()`/`strtol()`/`strtoull()` from untrusted input without validation:** When parsing numeric data from HTTP, network, or save state metadata with `atoi()` / `strtol()` / `strtoull()`, verify the result is validated (range check, `errno` check, or use `std::from_chars` which reports errors natively). `atoi()` returns 0 on failure silently — indistinguishable from a valid 0. From network/untrusted input, flag P1-SEC. From internal validated data, flag P4.
   - // [PPSSPP-FORK] Learning Loop: atoi()/strtol() from untrusted input without validation

4. **Core-specific checks** — After identifying which core the change targets, run the corresponding checklist:

   | Core | Key checks |
   |------|-----------|
   | **LAN Sync** | Thread safety (network thread vs UI thread). TLS cert lifecycle. mDNS/UDP discovery edge cases. HLC clock skew. HTTP handler error paths. Android JNI local ref cleanup. Save state file atomicity during sync. |
   | **N64** | *(N/A on feature/lan-sync)* TLB miss handling. RDP/RSP command buffer bounds. DMA alignment. |
   | **NDS** | *(N/A on feature/lan-sync)* ARM9/ARM7 dual-CPU sync. FIFO overflow. GPU command FIFO bounds. |
   | **PS1** | *(N/A on feature/lan-sync)* CD-ROM sector buffer. GTE fixed-point overflow. MDEC DMA alignment. |
   | **SNES** | *(N/A on feature/lan-sync)* SA-1/SuperFX clock ratio. DMA register shadowing. HDMA table pointer. |
   | **Generic** | If the core is not listed above, apply: buffer bounds, integer wrap, null pointer, and endianness checks as above. |

5. **Runtime, threading & lifecycle checks:**
   - **Race conditions:** If the modified code accesses shared state (audio ring buffer, save state, video output), verify it holds the appropriate mutex or uses an atomic operation. Emulator cores are often multi-threaded (audio on a separate thread). Flag any shared write without a lock as P1.
   - **C++ Memory Management & Ownership:** Verify that raw pointers instantiated with `new` (especially UI views/components) are properly owned by smart pointers or registered with a parent container (e.g. `root_->Add(...)`, `parent->Add(...)`) which manages their deletion. Unmanaged raw pointers cause leaks. Flag P4 if found.
   - **Orientation Re-creation Safety:** Screen rotation destroys and re-calls `CreateViews()`. Verify that any screen-level raw view pointers (e.g. `resumeButton_` or custom buttons) are either re-bound or reset to `nullptr` to avoid holding dangling references to destroyed views. Flag P2 if crash/UB risk, P3 if only visual parity gap.
   - **Core Shutdown & Resource Cleanup:** Verify that custom emulator cores clean up all raw buffers, release active configuration pointers, and stop pushing audio samples when shutting down to prevent memory leaks and background processes running after exit. For LAN sync: verify server sockets, mDNS announcements, and background threads are properly stopped. Flag P2 if found.
   - **Thread Safety (UI Thread vs Emu Thread):** Emulator cores run on a separate CPU thread, while UI logic runs on the main/graphics thread. Verify that any state/variables queried or shared between the UI thread and CPU thread are thread-safe (e.g. `std::atomic` or mutex-protected). For LAN sync: network discovery, HTTP handlers, and file transfer run on background threads — verify shared state with UI is protected. Flag P2/P4 if found.
   - **No Heap Allocation in Hot Loops:** Hot execution paths (e.g. `update()`, `Draw()`, or per-frame update loops) must NOT perform dynamic memory allocations (`new`, `malloc`, `std::vector::push_back` triggering reallocation, or runtime `std::string` concatenation). Use stack allocation, static buffers, or pre-allocated pools. Flag P4 if found.
   - **Config I/O Write Throttling:** Writing config to disk via `SaveConfig()` or `g_Config.Save()` blocks threads on mobile storage. Verify that config saving is only triggered on explicit UI actions (like screen exit, settings change) and never inside update loops or drag events. Flag P3/P4 if found.
   - **Android JNI & Lifecycle Safety:** Android JNI calls must clean up local references. Verify that JNI actions and background audio push events suspend completely when the emulator screen is paused or minimized, avoiding memory leaks or `DeadObjectException` crashes. Flag P2 if found.
   - **Dangling callback / lambda capture:** If a lambda captures `this` or a raw pointer and is stored for later execution (e.g., a scheduled event), verify the owner outlives the callback. Flag P2 if found.
   - **Reentrancy:** State machines in memory mappers or IRQ handlers can be re-entered via recursive CPU execution. Verify they guard against this (flag or early-out). Flag P2 if found.
   - **Blocking destructor with thread join:** If a class's destructor calls `JoinAllThreads()` or similar blocking join, verify there's a mechanism to unblock stuck threads first (e.g., set cancellation flag, close sockets, add join timeout). A destructor that blocks indefinitely on a stuck network thread hangs the entire process on exit. Flag P2 if found.
   - **TLS generated but not used:** If TLS certificates are generated and stored (`GenerateCertificate()`, `SaveToKeystore()`), verify they're actually used for connections (e.g., `SSL_accept`/`SSL_connect` called). Generating TLS without wrapping sockets defeats the security purpose. Flag P2 if found.
   - **Unbounded dynamic buffer growth:** When reading network data into a `std::vector` that grows (e.g., `buf.resize(buf.size() * 2)`), verify there's an upper bound (e.g., `MAX_UPLOAD_SIZE`). Unbounded growth allows a malicious peer to exhaust memory. Flag P4 if found.
    - **Config dual state:** When a feature has its own config struct (e.g., `LANSyncConfig`) AND is also stored in `g_Config`, verify there's a single source of truth. Two independent config objects for the same feature cause settings to silently diverge. Flag P2 if found.
    - **Platform parity violation:** When custom code uses `#if PPSSPP_PLATFORM(ANDROID)` or `#if PPSSPP_PLATFORM(LINUX)`, verify there's an `#else` branch that handles the other platform. Feature logic that only runs on one platform without justification violates AGENTS.md Platform Parity rule. Flag P2 if found.
    - **Missing platform fallback:** When custom code calls platform-specific APIs (mDNS, key storage, firewall), verify there's a fallback for other platforms. E.g., mDNS on Android (NsdManager) needs mDNS on Linux (Avahi) or UDP broadcast fallback. Flag P3 if missing.
    - **UI dialog gap:** When a UI dialog exists on one platform (Linux/SDL ImGui) but not on Android, flag P4. Core logic must be identical; only UI layer may differ. Check: settings, pairing, progress, conflict dialogs.
    - **Logging framework consistency:** `fprintf(stderr)` or `printf()` should NOT be used for diagnostics in LAN sync or emulator code. Use `INFO_LOG()`, `WARN_LOG()`, `ERROR_LOG()` from `Common/Log.h` instead. `fprintf` bypasses the logging system (no filtering, no log levels, no rotation). Flag P5.
   - // [PPSSPP-FORK] Learning Loop: fprintf(stderr) vs INFO_LOG

6. **Check against AGENTS.md rules:**
   - 🔴 FORBIDDEN: No upstream code deleted/modified/restructured
   - 🔴 FORBIDDEN: No `#else`/`#endif` altering upstream flow
   - 🟢 REQUIRED: `[PPSSPP-FORK]` markers on all fork additions
   - 🟢 REQUIRED: Feature flags wrap custom code (zero upstream leakage)
   - 🟢 REQUIRED: Config isolated in separate sections
   - **New file checklist** — for every new `.cpp`/`.h` file introduced:
     * ✅ Directory: non-core (`UI/`, `Common/`, `ext/`, `SDL/`, `Windows/`, `macOS/`) — NOT `Core/`/`GPU/`/`HLE/`/`MIPS/`
     * ✅ File header: `// [PPSSPP-FORK] <FeatureName>: <description>`
     * ✅ Include guard: `#pragma once` (project convention)
     * ✅ Namespace: use project-appropriate namespaces, none for UI files
     * ✅ No upstream code duplication — if the file parallels an upstream file, use a thin wrapper not a copy

7. **Check PSP parity** (Quality Gate #1):
   - **Auto-lookup PSP reference FIRST:** For every core feature changed, find the PSP equivalent by running:
     ```bash
     grep -n "<function/class/variable>" UI/<psp_file>.cpp | head -20
     ```
     Read the PSP implementation fully BEFORE analyzing the fork's change. If no PSP equivalent exists (new core-specific feature), skip PSP parity check and mark as N/A in report.
   - **Create comparison table EARLY** — before writing code analysis, draft the parity table with actual PSP source lines, not assumptions.
   - **Compare EVERY parameter** in Draw() calls — colors, opacity, scale, rotation — not just structure
   - **Never assume** global state (e.g., `GamepadUpdateOpacity`) affects a rendering API — verify by reading the API implementation or comparing PSP's explicit parameter usage
   - Compare: init values, edge case handling, save/exit paths, z-order, opacity
   - If no PSP equivalent exists (e.g., a core-specific feature like low-level audio), verify against established patterns instead
    - Document any gaps found in `docs/agents/codereview-log.md`

8. **Run anti-hallucination check:**
   - `ImageID("...")` — verify the atlas ID exists
   - `new ClassName(...)` — verify constructor signature via grep
   - `System_*`, `screenManager()`, `GetI18NCategory` — verify return types
   - **Deprecated API guard:** Before confirming a function exists, check if it appears in a `// Deprecated:` comment or was removed in a recent upstream commit. `git log --all -S 'FunctionName' -- path/` can surface this.
   - **Rendering API** — `dc.Draw()->DrawImageRotated(...)` — verify EACH parameter:
     * Color: is opacity/multiply color used? Or hardcoded `0xFFFFFFFF`?
     * Position: same coordinate space as PSP?
     * Scale: same semantics as PSP?
     * Rotation: same angle convention?
   - **Multi-Compiler Portability:** PPSSPP builds using Clang (Android/Apple), GCC (Linux), and MSVC (Windows). Verify that code uses standard header inclusions explicitly (e.g. `<algorithm>` for `std::clamp` or `std::min` to support MSVC) and avoids compiler-specific syntax extensions. Flag P1/P4 if found.

9. **Fix verification** (ONLY if a P1/P2 fix was applied):
   - **Re-build first:** Run `cmake --build <detected_dir> -j$(nproc) 2>&1 | tail -30`. If compile errors → fix immediately (P1).
   - **Re-run tests:** `python test.py` (or `<detected_dir>/PPSSPPUnitTest ALL` if the unit test binary exists). If tests fail → stop, report regression.
   - Re-read the fixed code block with its surrounding context.
   - Check: does the fix introduce ANY new code path not present before? (Expected for functional fixes; unexpected for surface-level fixes like `.size()` → `4`).
   - Check: are the old callers that relied on the buggy behavior now broken? (Trace callers of the fixed function).
   - Check: if loop bounds changed, does the new bound exceed array allocation? `for (s = 0; s < N; s++)` must have `array[N]` or larger.
   - Check: if the fix is in a shader, does the new calculation preserve output range (no NaN/Inf in GLSL)?
   - Check: does the fix maintain PSP parity? Compare with upstream PPSSPP's equivalent code path.
   - **Check: does the fix introduce a new memory safety issue?** (e.g., replacing a static buffer with a heap allocation without checking the return value; replacing `memcpy` with a larger size). If so, flag P1.
   - If any of the above fails, P1 — revert and rethink the fix.

10. **Categorize issues:**
    - P1: Compile error / crash / memory corruption (must fix NOW)
    - P1-SEC: Security/safety issue from untrusted input (ROM, save state, network) — input controlled by user/attacker. If bug is from internal variable already validated at init, use P1 instead.
    - P2: Logic error (wrong behavior, incorrect coordinate math, silent UB, endianness mismatch)
    - P3: PSP parity gap (behavior differs from upstream)
    - P4: Code quality (missing guard, fragile pattern, missing bounds clamp)
    - P5: Nitpick (missing marker, naming, minor)

11. **Double-Verification (Pass 2)** — Before writing the final report, run a second verification pass on all findings from Pass 1 using the "Attack & Defend" technique (Attack: "Is this bug REAL?" → Defend: "Is there compensating logic elsewhere?"):
     - **Verify context (not just the diff):** For every P1, P1-SEC, and P2 issue, read at least 20 lines before and after the changed code to ensure no compensating logic already exists elsewhere.
     - **The "Grep" Mandate (anti-hallucination):** If you flag a function as "Deprecated" or "Wrong API", you MUST run `grep -n` or `git log --all -S 'FunctionName'` on the relevant file(s) to empirically verify the function's status before including it in the report.
     - **False Positive Filter:** Re-evaluate every P1-SEC finding (buffer overflow / integer wrap). Ask yourself: "Can this input really be controlled by the user/ROM, or is it an internal variable already validated at init?" If validated at init, downgrade to P4 or remove.

12. **Fix Re-verification Workflow** — When reviewing ANY bug fix (applied by this agent or submitted as a commit):
     - **Step A: Understand the fix** — Read the commit message and diff. Identify the root cause and what changed.
     - **Step B: Re-build** — `cmake --build <detected_dir>` (use the same auto-detection from Step 0) — does the fix compile cleanly? Compare warning count with baseline from Step 0.
     - **Step C: Re-run tests** — `python test.py` (headless PSP/core tests) or `<detected_dir>/PPSSPPUnitTest ALL` if the unit test binary exists — do existing tests still pass?
     - **Step D: Regression check** — Does the fix change behavior that callers relied on? Trace all callers of the modified function.
     - **Step E: Edge case verification** — Does the fix handle:
       * Empty/null inputs?
       * Maximum values (buffer bounds)?
       * Concurrent access (if multi-threaded)?
       * Platform differences (Android vs Linux vs Windows)?
     - **Step F: PSP parity check** — Does the fixed code match upstream PPSSPP's equivalent code path? If no PSP equivalent exists, verify against established patterns.
     - **Step G: Verify the fix doesn't introduce new issues:**
       * New code paths: any uninitialized variables, missing null checks?
       * New allocations: are they freed/owned properly?
       * New includes: any missing `<algorithm>`, `<cstring>`, `<cstdint>` for MSVC portability?
     - If ANY step fails → P1: Report to human reviewer, do not approve.
     - If all steps pass → ✅ Fix verified.

13. **Report findings** using this exact template:

```
## Review: <commit-hash-or-working-tree>

### Files changed
- <file1> — <lines changed>
- <file2> — <lines changed>

### 🔴 Issues found

| # | File:Line | Category | Description | Fix |
|---|-----------|----------|-------------|-----|
| P1 | `path/file.cpp:123` | Compile error | `var` used before init | Initialize before use |
| P1-SEC | `path/file.cpp:77` | Buffer overflow | `memcpy(dst, romData, romData[0])` — size from untrusted ROM | Clamp: `min(romData[0], sizeof(dst))` |
| P2 | `path/file.cpp:456` | Logic error | Wrong formula X vs Y | Change to match PSP |
| P2 | `path/file.cpp:200` | UB — signed overflow | `int addr = base + offset` wraps silently | Cast to `u32` before arithmetic |
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

### ✅ Memory safety check

| Check | Result |
|-------|--------|
| Buffer bounds (memcpy/memset) | ✅ / ❌ / N/A |
| Integer overflow on addresses | ✅ / ❌ / N/A |
| Use-after-free / dangling pointer | ✅ / ❌ / N/A |
| Endianness (if applicable) | ✅ / N/A |
| Threading / shared state | ✅ / ❌ / N/A |

### ✅ Core-specific check

*(Name the core and list results of its checklist)*

| Core | Check | Result |
|------|-------|--------|
| LAN Sync | Thread safety, TLS lifecycle, mDNS/UDP, HLC, HTTP handlers, JNI cleanup | ✅ / ❌ / N/A |
| GBA / N64 / NDS / PS1 / SNES | *(N/A on feature/lan-sync)* | N/A |

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

**P1-SEC — `path/file.cpp:77` — Buffer overflow**
- Problem: Size taken directly from ROM data without bounding
- Impact: Heap/stack corruption if ROM is malformed or malicious
- Fix: `size_t copyLen = std::min((size_t)romData[0], sizeof(dst)); memcpy(dst, src, copyLen);`

**P4 — `path/file.cpp:101` — Code quality**
- Problem: Missing division-by-zero guard
- Impact: Crash if image not found
- Fix: Add `if (image && image->w > 0)`

### Summary

- **P1:** 0 | **P1-SEC:** 0 | **P2:** 0 | **P3:** 0 | **P4:** 1 | **P5:** 0
- **Verdict:** ✅ Clean (or ❌ Needs fix — P1 issues remain)
```

> **Apply fixes**: After reporting, apply fixes for all P4 and P5 issues found (unless the fix would alter upstream code — AGENTS.md 🔴 FORBIDDEN). P1/P2 fixes are applied immediately per the Important rules below.
>
> **Verify fixes**: After ALL fixes (P1–P5) are applied, run the verification gate:
> 1. **Re-build:** `cmake --build <detected_dir> -j$(nproc) 2>&1 | tail -30` — must succeed with no new warnings.
> 2. If compile errors → P1 — fix immediately before proceeding.
> 3. **Re-run tests:** `python test.py` (or `<detected_dir>/PPSSPPUnitTest ALL` if the unit test binary exists) — must pass.
> 4. If tests fail → stop. Report the regression. Do NOT proceed to commit.
> 5. **Re-check PSP parity:** If any fix changes behavior, verify against PSP equivalent again.
>
> **After fixing**: Ask the user: *"Fixes applied — commit?"* Do NOT commit without explicit confirmation (AGENTS.md rule).

14. **Learning Loop — Auto-improve review patterns**
   - After the report is finalized (and fixes applied if any), extract bug patterns for continuous improvement.
   - **Pattern Extraction:** For each P1/P2/P3 bug found in the report, classify its pattern type (1 pattern = 1 bug category, not per-location). (Also log P4/P5 if the pattern has no detection rule in this skill file yet)
     * **Memory Safety:** buffer overflow, integer wrap, use-after-free, dangling pointer, signed/unsigned mismatch
     * **Logic Error:** wrong formula, incorrect condition, missing guard, off-by-one
     * **PSP Parity Gap:** init value mismatch, missing feature, behavioral difference
     * **Threading:** race condition, missing lock, deadlock risk
     * **Android/JNI:** lifecycle issue, JNI leak, ANR risk
     * **Config/State:** migration missing, save/load mismatch, default inconsistency
     * **Platform:** endianness, compiler portability, narrowing conversion
   - **Pattern Check:** For each extracted pattern, check if the codereview skill already has a detection rule for it:
     * Search the skill file for keywords related to the pattern (e.g., "buffer overflow", "integer wrap", "parity gap")
     * If a detection rule exists → skip (already covered)
     * If NO detection rule exists → this is a NEW pattern that slipped through
   - **Skill Update (if new pattern found):**
     * Add the new detection rule to the appropriate section of this skill file
     * Applies to ALL severity levels (P1-P5) — if pattern is NEW, add it regardless of severity
     * Use surgical edit (NOT full rewrite) — add to the relevant check section (e.g., Step 3 for memory, Step 4 for core-specific, Step 5 for threading)
     * Tag with: `// [PPSSPP-FORK] Learning Loop: <pattern description>`
     * Verify file size stays under 600 lines after addition
   - **Log to docs/agents/codereview-log.md:**
     * Append to the "Code Review Learning Log" section
     * Format: `| <date> | <bug summary> | <pattern type> | <P-level> | <file:line> | <skill updated: yes/no> |`
     * If skill was updated, also note the section and line number where the rule was added
   - **Rolling Window (max 50 entries):**
     * After logging, count entries: `grep -c '^| 20[0-9][0-9]-' docs/agents/codereview-log.md`
     * If entries > 50 → archive oldest entries:
       1. Determine archive file: `docs/agents/codereview-archive/YYYY-HN.md` (H1=Jan-Jun, H2=Jul-Dec)
       2. Extract entries older than newest 20: keep newest 20 in file, move rest to archive
       3. Append to archive with header: `## Entries <start_date> to <end_date>`
        4. Update Statistics Summary in codereview-log.md
      * If archive file doesn't exist → create it with header: `# Code Review Archive — YYYY`
      * Archive command example:
        ```bash
        # Count entries
        entries=$(grep -c '^| 20[0-9][0-9]-' docs/agents/codereview-log.md)
        if [ "$entries" -gt 50 ]; then
          # Get archive filename (H1 or H2 based on current month)
          month=$(date +%m)
          if [ "$month" -le 6 ]; then period="H1"; else period="H2"; fi
          archive="docs/agents/codereview-archive/$(date +%Y)-${period}.md"
          # Create archive if needed
          if [ ! -f "$archive" ]; then
            echo "# Code Review Archive — $(date +%Y)" > "$archive"
          fi
          # Extract oldest entries (skip newest 20)
          grep '^| 20[0-9][0-9]-' docs/agents/codereview-log.md | head -n -20 >> "$archive"
          # Keep newest 20 in main file (rebuild section)
        fi
        ```
   - **Statistics Summary (update after each log):**
     * Total bugs found (all time)
     * Bugs per pattern type (count)
     * Top 5 files with most bugs — calculate with: `grep '^| 20' docs/agents/codereview-log.md | awk -F'|' '{print $6}' | sort | uniq -c | sort -rn | head -5`
     * Skill update count
     * Last archive period
   - **Purpose:** Every bug found makes future reviews stronger. Patterns that slipped through once get caught automatically next time. Rolling window keeps file manageable while archive preserves history.

- Be thorough. Check every changed line, not just the diff overview.
- Trace full code paths, not just isolated changes.
- **Never assume API behavior.** If you think a global state affects a rendering call, verify by:
  1. Reading the PSP reference and checking if PSP explicitly passes the parameter
  2. If PSP passes it explicitly (e.g., `colorAlpha(..., opacity)`), then the forked version MUST also pass it explicitly
  3. Do NOT assume `GamepadUpdateOpacity()` or similar global state automatically applies to all draw calls
- **Parameter-level comparison:** When comparing PSP vs forked Draw() overrides, compare every single argument passed to every API call. "Similar structure" is NOT sufficient — check colors, opacity, scale, rotation individually.
- **ROM/untrusted input is always hostile.** Any value read from ROM, save state, or network that becomes a size, index, or pointer is untrusted. Treat it as attacker-controlled. A missing bounds check in ROM parsing is always P1-SEC, not P4.
- **"Core-agnostic" means run the generic checks first, then the core-specific checklist.** Skip core-specific checks ONLY if the change is purely generic (no core-specific code paths touched). Timing bugs and DMA alignment bugs only manifest on the specific core they target.
- The user should not need to ask follow-up questions — the review should be complete and actionable.
- If you find a P1 bug, fix it immediately and report the fix.
- Fix P4/P5 issues after reporting, before asking to commit. Do not ask to commit with unaddressed P4/P5 items.
- Reference AGENTS.md rules by name when flagging violations.
