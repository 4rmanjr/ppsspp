# PSP Knowledge Base — GBA Implementation Mismatches

> **Purpose:** Record every mismatch found between GBA and PSP implementations.
> Other agents MUST read this file before working on any GBA feature.
> Update this file every time a new mismatch is found.

## Usage

- Format: `# File <path> -> <class/function>`
- Each entry: Bug → Impact → Fix → Lesson
- Date + commit hash for traceability
- Don't delete old entries (unless irrelevant)

---

# ======================================================
# UI / Core Touch Layout
# ======================================================

## nextToggleAll_ initial value — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** `nextToggleAll_` started `false` (GBA), must be `true` (PSP)
- **Impact:** First Toggle All HID all buttons instead of showing them (reversed user expectation)
- **Fix:** `false` → `true` (commit `c989dbf886`)
- **Lesson:** PSP value MUST be checked before initializing new fields. Don't assume `false` is "default safe".

## Toggle All didn't cover system buttons — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Toggle All only toggled game buttons (A/B/Select/Start/L/R/D-Pad), Fast-forward and Pause were unaffected
- **Impact:** User toggles all OFF → Fast-forward & Pause stay ON. Inconsistent with PSP.
- **Fix:** Extended handler to also toggle `touchFastForwardKey.show` and `touchPauseKey.show` from `TouchControlConfig` (commit `c6faf8e`)
- **Lesson:** Don't assume Toggle All scope matches PSP. Always verify against PSP reference.

## Pause `SetMinimumAlpha` conditional vs unconditional — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → GBA Pause button creation
- **Bug:** `SetMinimumAlpha(0.1f)` only called on devices without hardware back button. PSP calls it **unconditionally**.
- **Impact:** Pause button could be fully transparent on devices with back button (user can't find it)
- **Fix:** Removed `if (!System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON))` — call unconditional (commit `c6faf8e`)
- **Lesson:** Read PSP code fully. Don't guess conditions based on comments alone.

## System buttons (Fast-forward, Pause) not created for GBA — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → GBA `CreateViews()`
- **Bug:** Only `AddGBATouchButtons()` was called (game buttons: A/B/L/R/Select/Start/D-Pad). No Pause or Fast-forward buttons created.
- **Impact:** GBA users had no access to Pause (except hardware button) and no access to Fast-forward at all.
- **Fix:** Added BoolButton creation for Pause (`&pauseTrigger_`) and Fast-forward (`&PSP_CoreParameter().fastForward`) from shared `TouchControlConfig` (commit `ed32432`)
- **Lesson:** PSP `CreatePadLayout()` is not just game buttons. System buttons (Pause, Fast-forward) must also exist for GBA. PSP→GBA feature mapping must be complete.

## GBA touch button size tiny dots (preview editor) — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBADragDrop::Draw()`
- **Bug:** Formula `scale_ = btn_.w * g_layoutScale` (0.09 * 0.8 = 0.072) — treated `btn_.w` as image scale factor, but it's normalized width
- **Impact:** Buttons rendered at ~7px (tiny dots) instead of ~87px
- **Fix:** `scale_ = (btn_.w * screenBounds_.w) / image->w` — normalized width → pixels → atlas tile scale (commit `af19ea7c03`)
- **Lesson:** Understand the semantics of each field. `btn_.w` is `normalized width` (0-1), not a scale factor.

## GBA gameplay touch button size hardcoded — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → `AddGBATouchButtons()`
- **Bug:** `bgScale = 0.8f` (hardcoded, ignores `btn.w`)
- **Impact:** All buttons same size, can't be customized via layout editor
- **Fix:** `bgScale = (btn.w * bounds.w) / (float)atlasImg->w` — matches PSP behavior (commit `a94d8f5ed5`)
- **Lesson:** Avoid hardcoded values. Every button size must come from config.

## Resize range didn't match new semantics — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBALayoutView::Touch()`
- **Bug:** Resize range `[0.3, 1.5]` was designed for "image scale factor" semantics (old broken formula). After fixing to "normalized width", this range was too large.
- **Impact:** Resize slider was unproportional. 1.5 normalized width = 150% screen → button overflow.
- **Fix:** Range `[0.03, 0.30]` = 3%-30% screen width. Movement factor `0.02f` → `0.0015f` for a 133px drag to span default→max (commit `af19ea7c03`)
- **Lesson:** Every semantic field change MUST be followed by range and sensitivity updates.

## GBA portrait screen position didn't use DisplayOffsetY — ✅ Fixed

- **File:** `EmuCore/GBACore.cpp` → `GBACore::GetRenderRect()`
- **Bug:** `y = (viewH - h) / 2.0f` (centered). PSP portrait uses `CalculateDisplayOutputRect` which reads `fDisplayOffsetY = 0.25f` (top-aligned).
- **Impact:** GBA portrait centered on screen, PSP portrait top-aligned. User's custom offset didn't affect GBA.
- **Fix:** `y = viewH * offsetY - h * 0.5f` with `offsetY = g_Config.displayLayoutPortrait.fDisplayOffsetY` (0.25 default) (commit `56924fa019`)
- **Lesson:** Layout formulas differ per core and must be checked. PSP portrait logic is in `GPU/Common/PresentationCommon.cpp`, GBA is in `EmuCore/GBACore.cpp` — both must be consistent.

## GBATouchVisibilityPopup had no button icons — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Only text label checkboxes, no button icons (PSP uses I_CROSS, I_CIRCLE, etc.)
- **Impact:** Less user-friendly, hard to visually distinguish A from B
- **Fix:** Added `GBACheckBoxChoice` class + atlas images for every button row (commit `1eaa2c77be`)
- **Lesson:** Visual parity with PSP matters — icons aid quick identification.

## GBATouchVisibilityPopup only saved on OK OnClick — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Config saved only in OK click handler. PSP saves in `onFinish()` which covers all exit paths (OK, Cancel, system back button).
- **Impact:** Android back button → data loss (visibility changes not saved)
- **Fix:** Moved save to `OnCompleted()` override (commit `1eaa2c77be`)
- **Lesson:** Save MUST be in `onFinish`/`OnCompleted` to cover all exit paths, not in the OK button handler.

---

# ======================================================
# Shared Config / TouchControlConfig
# ======================================================

## TouchControlConfig ownership not explicit

- **File:** `UI/GamepadEmu.cpp`, `UI/EmuScreen.cpp`
- **Issue:** PSP and GBA share the SAME `g_Config.GetTouchControlsConfig(orientation)`. But initialization (`InitPadLayout`) is only for PSP. GBA uses values already initialized by the PSP path.
- **Impact:** No current issue because `InitPadLayout` is called unconditionally before core branching. But fragile if changes occur.
- **Lesson:** Shared config must be explicit about who initializes it and when. Don't rely on side effects from another core's init.

## Default Pause position differs between PSP InitPadLayout and GBA

- **File:** `UI/GamepadEmu.cpp` → `InitPadLayout()`
- **PSP:** `Pause_button_center_X = halfW`, `Pause_button_center_Y = 28.0f`
- **GBA:** No `InitGBAPadLayout()` — GBA uses values already initialized by PSP
- **Impact:** Pause button position in GBA is the same as PSP (top center). May not be ideal for GBA layout.
- **Lesson:** If different default positions are needed per core, create a separate `Init<Core>PadLayout()` or override in `EmuCore/Config.cpp`.
