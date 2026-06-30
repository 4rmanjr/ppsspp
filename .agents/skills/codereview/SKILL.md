---
name: codereview
description: Review the latest changes for bugs, regressions, and PSP parity violations across ALL cores (GBA, N64, PS1, NDS, etc.). Runs Quality Gate #2 from AGENTS.md.
---

// [PPSSPP-FORK] Codereview: code review skill

# Code Review — Quality Gate Check (All Cores)

When the user types `/codereview`, perform a complete code review of the latest changes. This skill is **core-agnostic** — it applies to any non-PSP emulator (GBA, N64, PS1, NDS, SNES, etc.).

## What to do

0. **Build Gate — compile first**
   - Run `cmake --build build -j$(nproc) 2>&1 | tail -30`. Check for compile errors.
   - If compile errors → **STOP**. Report as P1. Do NOT proceed with further review until fixed.
   - Note the warning count as a baseline for fix verification.

   - **Test Gate — run relevant tests:** After the build succeeds, identify tests related to changed files (`grep -rn 'TestFunc\|<test_name>' unittest/`). Run `./build/PPSSPPUnitTest ALL` or the specific test (requires `-DUNITTEST=ON` at cmake configure time; if the binary is missing, skip this step). For core logic changes: `python test.py` (headless PSP tests). If tests fail → STOP. Report with the test output.

1. **Determine what to review**
   - If there are uncommitted changes (`git status --short` shows modified files), review the working tree diff.
   - Otherwise, review the latest commit (`git diff HEAD~1`).
   - **Commit message check:** Format must follow `[feature/<name>] <desc>` or `fix(<scope>): <desc>`. If not → P5.
   - **Diff health check:**
     * `git diff --check` — whitespace errors? Flag P5.
     * `grep '<<<<<<< \|=======\|>>>>>>> '` — unresolved merge markers? Flag P1.
     * Files changed > 20? Suggest splitting the commit.

2. **Trace every code path** in the modified files:
   - Follow function calls from entry point to return
   - Check if variables are properly initialized before use
   - Verify edge cases (null checks, division-by-zero guards, empty bounds, invalid indices)
   - Validate coordinate math (screen space vs parent-relative)
   - **`.size()` type check:** Before reading code that calls `.size()` on a value, verify the type supports it. Plain C arrays (`Type arr[N]`) have NO `.size()` member — this is a P1 compile error. When in doubt, `grep -n 'arr\['` on the declaration to confirm it's a vector, not a C array.
   - **`std::string_view` lifetime check:** When instantiating `ImageID` (which wraps `std::string_view`), verify the target string's lifetime. Creating `ImageID` from a temporary string concatenation (e.g. `std::string(def->bgID) + "_LINE"`) creates a dangling pointer when the temporary string is destroyed. Use static string literals or persistent mappings instead. Flag P1/P2 if found.
   - **Division-by-zero in GLSL:** Scan `.fsh` files for `1.0 /` or `1.0f /` expressions. Verify the divisor is guarded (e.g., `if (gamma > 0.01)` or slider range > 0). If unguarded, flag P4.
    - **Cross-file string consistency:** When code matches on string literals (e.g., `info.section == "GBALCD"`), verify the actual value by reading the source that produces it — INI section name, enum stringification, const/define. A mismatch is P2 (silent logic error).
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

3. **Memory safety & security checks** — Run these on every modified file:
   - **Buffer bounds:** For every `memcpy`, `memset`, `strcpy`, `sprintf`, verify the destination has enough space. ROM parsing code is especially risky — input is untrusted. Flag any copy where the size comes from ROM data without a `min(size, MAX)` clamp.
   - **Integer overflow:** Check arithmetic on addresses, offsets, and sizes used in emulated memory access. `u32 addr = base + offset` can silently wrap if not guarded. Flag any `P1` if the result indexes into a real buffer.
   - **Use-after-free / dangling pointer:** Look for pointers stored across frame boundaries (e.g., cached `Core*` or `Texture*`). If the pointee can be destroyed while the pointer is live (e.g., core reset, texture cache flush), flag P1.
   - **Signed/unsigned mismatch in comparisons:** `if (idx < arr.size())` where `idx` is `int` silently wraps for negative values. Verify loop indices are unsigned or guarded with `>= 0`.
   - **Undefined behavior (C++ UB):** Flag signed integer overflow, left-shift into sign bit, and `reinterpret_cast` patterns that violate strict aliasing. These are P2 — they compile and run until optimizations break them.
   - **Endianness:** When touching memory read/write helpers (e.g., `Memory::Read32`, `MMU::Write16`), verify the byte-swap macro matches the target CPU's endianness. A missing `bswap` on a big-endian core (N64, GC) silently corrupts data.

4. **Core-specific checks** — After identifying which core the change targets, run the corresponding checklist:

   | Core | Key checks |
   |------|-----------|
   | **GBA** | Prefetch buffer timing (code in ROM vs IWRAM has different cycle counts). BIOS call stubs. Cartridge save type detection from ROM header — verify bounds on header field reads. |
   | **N64** | TLB miss handling in virtual→physical address translation. RDP/RSP command buffer bounds. DMA transfer size must be a multiple of 8 bytes — flag any `dmaLen` set from game data without alignment check. |
   | **NDS** | ARM9/ARM7 dual-CPU sync — shared WRAM access must be atomic or protected. FIFO queue overflow guard. GPU command FIFO — verify no write past end of 256-entry buffer. |
   | **PS1** | CD-ROM sector buffer (2048 bytes) — verify all sector reads clamp to this size. GTE fixed-point overflow on coordinate transforms. MDEC DMA transfer alignment. |
   | **SNES** | SA-1/SuperFX co-processor clock ratio must stay in sync with main CPU. DMA general purpose register shadowing — a write to `$420B` triggers DMA, verify the HDMA table pointer is valid. |
   | **Generic** | If the core is not listed above, apply: buffer bounds, integer wrap, null pointer, and endianness checks as above. |

5. **Runtime, threading & lifecycle checks:**
   - **Race conditions:** If the modified code accesses shared state (audio ring buffer, save state, video output), verify it holds the appropriate mutex or uses an atomic operation. Emulator cores are often multi-threaded (audio on a separate thread). Flag any shared write without a lock as P1.
   - **C++ Memory Management & Ownership:** Verify that raw pointers instantiated with `new` (especially UI views/components) are properly owned by smart pointers or registered with a parent container (e.g. `root_->Add(...)`, `parent->Add(...)`) which manages their deletion. Unmanaged raw pointers cause leaks. Flag P4 if found.
   - **Orientation Re-creation Safety:** Screen rotation destroys and re-calls `CreateViews()`. Verify that any screen-level raw view pointers (e.g. `resumeButton_` or custom buttons) are either re-bound or reset to `nullptr` to avoid holding dangling references to destroyed views. Flag P2/P3 if found.
   - **Core Shutdown & Resource Cleanup:** Verify that custom emulator cores clean up all raw buffers, release active configuration pointers, and stop pushing audio samples when shutting down (e.g. `ShutdownGBA()`) to prevent memory leaks and background processes running after exit. Flag P2 if found.
   - **Thread Safety (UI Thread vs Emu Thread):** Emulator cores run on a separate CPU thread, while UI logic runs on the main/graphics thread. Verify that any state/variables queried or shared between the UI thread and CPU thread are thread-safe (e.g. `std::atomic` or mutex-protected). Flag P2/P4 if found.
   - **No Heap Allocation in Hot Loops:** Hot execution paths (e.g. `UpdateGBA()`, `Draw()`, or per-frame update loops) must NOT perform dynamic memory allocations (`new`, `malloc`, `std::vector::push_back` triggering reallocation, or runtime `std::string` concatenation). Use stack allocation, static buffers, or pre-allocated pools. Flag P4 if found.
   - **Config I/O Write Throttling:** Writing config to disk via `SaveConfig()` or `g_Config.Save()` blocks threads on mobile storage. Verify that config saving is only triggered on explicit UI actions (like screen exit, settings change) and never inside update loops or drag events. Flag P3/P4 if found.
   - **Android JNI & Lifecycle Safety:** Android JNI calls must clean up local references. Verify that JNI actions and background audio push events suspend completely when the emulator screen is paused or minimized, avoiding memory leaks or `DeadObjectException` crashes. Flag P2 if found.
   - **Dangling callback / lambda capture:** If a lambda captures `this` or a raw pointer and is stored for later execution (e.g., a scheduled event), verify the owner outlives the callback. Flag P2 if found.
   - **Reentrancy:** State machines in memory mappers or IRQ handlers can be re-entered via recursive CPU execution. Verify they guard against this (flag or early-out). Flag P2 if found.

6. **Check against AGENTS.md rules:**
   - 🔴 FORBIDDEN: No upstream code deleted/modified/restructured
   - 🔴 FORBIDDEN: No `#else`/`#endif` altering upstream flow
   - 🟢 REQUIRED: `[PPSSPP-FORK]` markers on all fork additions
   - 🟢 REQUIRED: Feature flags wrap custom code (zero upstream leakage)
   - 🟢 REQUIRED: Config isolated in separate sections
   - **New file checklist** — for every new `.cpp`/`.h` file introduced:
     * ✅ Directory: non-core (`UI/`, `EmuCore/`, `Common/`, `ext/`, `SDL/`) — NOT `Core/`/`GPU/`/`HLE/`/`MIPS/`
     * ✅ File header: `// [PPSSPP-FORK] <FeatureName>: <description>`
     * ✅ Include guard: `#pragma once` (project convention)
     * ✅ Namespace: `EmuCore::` for multi-emu, none for UI files
     * ✅ No upstream code duplication — if the file parallels an upstream file, use a thin wrapper not a copy

7. **Check PSP parity** (Quality Gate #1):
   - For every core feature changed, find the PSP equivalent and compare behavior
   - **Compare EVERY parameter** in Draw() calls — colors, opacity, scale, rotation — not just structure
   - **Never assume** global state (e.g., `GamepadUpdateOpacity`) affects a rendering API — verify by reading the API implementation or comparing PSP's explicit parameter usage
   - Compare: init values, edge case handling, save/exit paths, z-order, opacity
   - If no PSP equivalent exists (e.g., a core-specific feature like low-level audio), verify against established patterns instead
   - Document any gaps found

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
    - P1-SEC: Security/safety issue in ROM parsing or untrusted input path (flagged separately, never downgraded)
    - P2: Logic error (wrong behavior, incorrect coordinate math, silent UB, endianness mismatch)
    - P3: PSP parity gap (behavior differs from upstream)
    - P4: Code quality (missing guard, fragile pattern, missing bounds clamp)
    - P5: Nitpick (missing marker, naming, minor)

11. **Double-Verification (Pass 2)** — Before writing the final report, run a second verification pass on all findings from Pass 1 using the "Attack & Defend" technique:
     - **Verify context (not just the diff):** For every P1, P1-SEC, and P2 issue, read at least 20 lines before and after the changed code to ensure no compensating logic already exists elsewhere.
     - **The "Grep" Mandate (anti-hallucination):** If you flag a function as "Deprecated" or "Wrong API", you MUST run `grep -n` or `git log --all -S 'FunctionName'` on the relevant file(s) to empirically verify the function's status before including it in the report.
     - **False Positive Filter:** Re-evaluate every P1-SEC finding (buffer overflow / integer wrap). Ask yourself: "Can this input really be controlled by the user/ROM, or is it an internal variable already validated at init?" If validated at init, downgrade to P4 or remove.

12. **Fix Re-verification Workflow** — When reviewing a submitted bug fix (not applied by this agent, but presented as a commit to review):
     - **Step A: Understand the fix** — Read the commit message and diff. Identify the root cause and what changed.
     - **Step B: Re-build** — `cmake --build build` — does the fix compile cleanly? Compare warning count with baseline from Step 0.
     - **Step C: Re-run tests** — `./build/PPSSPPUnitTest ALL` — do existing tests still pass?
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
| GBA / N64 / NDS / PS1 / SNES | <checklist item description> | ✅ / ❌ / N/A |

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

> **After reporting**: If any fixes were applied during this review, ask the user: *"Fixes applied — commit?"* Do NOT commit without explicit confirmation (AGENTS.md rule).

## Important rules

- Be thorough. Check every changed line, not just the diff overview.
- Trace full code paths, not just isolated changes.
- **Never assume API behavior.** If you think a global state affects a rendering call, verify by:
  1. Reading the PSP reference and checking if PSP explicitly passes the parameter
  2. If PSP passes it explicitly (e.g., `colorAlpha(..., opacity)`), then the forked version MUST also pass it explicitly
  3. Do NOT assume `GamepadUpdateOpacity()` or similar global state automatically applies to all draw calls
- **Parameter-level comparison:** When comparing PSP vs forked Draw() overrides, compare every single argument passed to every API call. "Similar structure" is NOT sufficient — check colors, opacity, scale, rotation individually.
- **ROM/untrusted input is always hostile.** Any value read from ROM, save state, or network that becomes a size, index, or pointer is untrusted. Treat it as attacker-controlled. A missing bounds check in ROM parsing is always P1-SEC, not P4.
- **"Core-agnostic" means run the generic checks first, then the core-specific checklist.** Never skip the core-specific table — timing bugs and DMA alignment bugs only manifest on the specific core they target.
- The user should not need to ask follow-up questions — the review should be complete and actionable.
- If you find a P1 bug, fix it immediately and report the fix.
- Reference AGENTS.md rules by name when flagging violations.
