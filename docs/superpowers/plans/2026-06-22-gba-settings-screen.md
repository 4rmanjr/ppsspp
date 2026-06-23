# GBA Settings Screen — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a GBA-specific settings screen that only shows relevant options (Controls, Display, Audio) when a GBA game is paused, without modifying any PSP settings code.

**Architecture:** New `GBASettingsScreen` class extends `UIDialogScreen`, added as a Choice in the GBA pause menu. Config stored in `[GBA]` section of `ppsspp.ini`. Display settings applied in `GBACore::Render()`.

**Tech Stack:** C++17, PPSSPP UI framework (UIScreen, CollapsibleSection, Choice, Dropdown, Slider)

**Spec:** `docs/superpowers/specs/2026-06-22-gba-settings-screen-design.md`

---

### Task 1: Add GBA config keys

**Files:**

- Modify: `Core/Config.h` — add GBA config fields to `Config` class
- Modify: `Core/Config.cpp` — add Load/Save for new keys

- [ ] **Step 1: Add config fields to Config class**

In `Core/Config.h`, inside `struct Config` (find location of `iTexFiltering` or similar to match pattern):

```cpp
// [PPSSPP-FORK] MultiCore: GBA display settings
#ifdef PPSSPP_MULTICORE
int iGBAAspectRatio = 0;       // 0=3:2, 1=16:9, 2=1:1, 3=stretch
bool bGBAIntegerScaling = false;
float fGBAVolume = 1.0f;
#endif
```

- [ ] **Step 2: Add load lines in Config::ReadAllSettings**

Find the `[GBA]` section load block in `Config::ReadAllSettings()`:

```cpp
// [PPSSPP-FORK] MultiCore: GBA display/audio settings
#ifdef PPSSPP_MULTICORE
section->Get("iGBAAspectRatio", &iGBAAspectRatio, 0);
section->Get("bGBAIntegerScaling", &bGBAIntegerScaling, false);
section->Get("fGBAVolume", &fGBAVolume, 1.0f);
#endif
```

- [ ] **Step 3: Add save lines in Config::Save()**

Find the `[GBA]` section save block:

```cpp
// [PPSSPP-FORK] MultiCore: GBA display/audio settings
#ifdef PPSSPP_MULTICORE
gba->Set("iGBAAspectRatio", iGBAAspectRatio);
gba->Set("bGBAIntegerScaling", bGBAIntegerScaling);
gba->Set("fGBAVolume", fGBAVolume);
#endif
```

- [ ] **Step 4: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 5: Commit**

```bash
git add Core/Config.h Core/Config.cpp
git commit -m "[config] Add GBA display/audio config keys (iGBAAspectRatio, bGBAIntegerScaling, fGBAVolume)"
```

---

### Task 2: Create GBASettingsScreen files

**Files:**

- Create: `UI/GBASettingsScreen.h`
- Create: `UI/GBASettingsScreen.cpp`
- Modify: `CMakeLists.txt` (root — add files to PPSSPPSDL sources)

- [ ] **Step 1: Create GBASettingsScreen.h**

```cpp
// [PPSSPP-FORK] MultiCore: GBA settings screen (Controls, Display, Audio)
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "Common/UI/UIScreen.h"

class GBASettingsScreen : public UIDialogScreen {
public:
    GBASettingsScreen(const Path &gamePath) : gamePath_(gamePath) {}
    void CreateViews() override;
    void onFinish(DialogResult result) override;

private:
    Path gamePath_;
};
```

- [ ] **Step 2: Create GBASettingsScreen.cpp**

```cpp
// [PPSSPP-FORK] MultiCore: GBA settings screen implementation
// Jangan hapus, jangan ubah kode upstream.

#include "GBASettingsScreen.h"
#include "Core/Config.h"
#include "Common/UI/Context.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Data/Text/I18n.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DisplayLayoutScreen.h"

void GBASettingsScreen::CreateViews() {
    using namespace UI;
    auto gs = GetI18NCategory(I18NCat::GRAPHICSSETTINGS);
    auto co = GetI18NCategory(I18NCat::CONTROLS);
    auto au = GetI18NCategory(I18NCat::AUDIO);
    auto mm = GetI18NCategory(I18NCat::MAINMENU);

    root_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
    LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

    // === Controls ===
    CollapsibleSection *controlsSection = list->Add(new CollapsibleSection(co->T("Controls")));
    controlsSection->Add(new Choice(co->T("Control Mapping")))->OnClick.Add([this](UI::EventParams &) {
        screenManager()->push(new ControlMappingScreen());
    });

    // === Display ===
    CollapsibleSection *displaySection = list->Add(new CollapsibleSection(gs->T("Display")));

    // Aspect Ratio
    static const char *aspectOptions[] = {"3:2", "16:9", "1:1", "Stretch"};
    ChoiceStrip *aspectStrip = displaySection->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
    aspectStrip->SetDisplayNative(false);
    for (int i = 0; i < 4; i++) {
        aspectStrip->AddChoice(aspectOptions[i]);
    }
    aspectStrip->SetSelection(g_Config.iGBAAspectRatio);
    aspectStrip->OnChoice.Add([&](UI::EventParams &e) {
        g_Config.iGBAAspectRatio = e.a;
    });

    // Filter
    static const char *filterOptions[] = {"Nearest", "Linear"};
    displaySection->Add(new PopupSliderChoice(&g_Config.iGBATexFiltering, 0, 1, gs->T("Filter"), 0, screenManager()));

    // Integer Scaling
    displaySection->Add(new CheckBox(&g_Config.bGBAIntegerScaling, gs->T("Integer Scaling")));

    // === Audio ===
    CollapsibleSection *audioSection = list->Add(new CollapsibleSection(au->T("Audio")));
    audioSection->Add(new PopupSliderChoiceFloat(&g_Config.fGBAVolume, 0.0f, 1.0f, au->T("Volume"), 0.05f, screenManager(), ""));

    // === OK ===
    list->Add(new Choice(mm->T("OK")))->OnClick.Add([this](UI::EventParams &) {
        TriggerFinish(DR_OK);
    });
}

void GBASettingsScreen::onFinish(DialogResult result) {
    if (result == DR_OK) {
        g_Config.Save("GBASettingsScreen::onFinish");
    }
}
```

Wait — `PopupSliderChoice` needs checking. Let me verify it exists. Actually, looking at PPSSPP code, the slider widget is `PopupSliderChoiceFloat`. Let me also check `ChoiceStrip` usage.

Actually, for simplicity, let me use simpler widgets that I know exist in PPSSPP:

For Aspect Ratio: Use `PopupSliderChoice` (int 0-3).
For Filter: Use `PopupSliderChoice` (int 0-1).
For Volume: Use `PopupSliderChoiceFloat`.

- [ ] **Step 3: Add files to CMakeLists.txt**

Find the PPSSPPSDL source list and add:

```cmake
UI/GBASettingsScreen.cpp
```

- [ ] **Step 4: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 5: Commit**

```bash
git add UI/GBASettingsScreen.h UI/GBASettingsScreen.cpp CMakeLists.txt
git commit -m "[ui] Create GBASettingsScreen (Controls, Display, Audio)"
```

---

### Task 3: Hook into Pause menu (GBA mode)

**Files:**

- Modify: `UI/PauseScreen.h` — add `OnGBASettings` method declaration
- Modify: `UI/PauseScreen.cpp` — add Choice + handler

- [ ] **Step 1: Add method declaration to GamePauseScreen**

In `UI/PauseScreen.h`, inside `class GamePauseScreen`:

```cpp
// [PPSSPP-FORK] MultiCore: GBA settings screen
#ifdef PPSSPP_MULTICORE
void OnGBASettings(UI::EventParams &e);
#endif
```

- [ ] **Step 2: Add include and Choice in PauseScreen.cpp**

Find the includes and add:

```cpp
// [PPSSPP-FORK] MultiCore: GBA settings screen
#ifdef PPSSPP_MULTICORE
#include "GBASettingsScreen.h"
#endif
```

Find the pause menu button area (around the "Settings" / "Game Settings" button) and add:

```cpp
// [PPSSPP-FORK] MultiCore: GBA Settings button (replaces PSP settings in GBA mode)
#ifdef PPSSPP_MULTICORE
if (g_gbaModeActive) {
    rightColumnItems->Add(new Choice(pa->T("GBA Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGBASettings);
}
#endif
```

- [ ] **Step 3: Add OnGBASettings handler**

```cpp
// [PPSSPP-FORK] MultiCore: open GBA settings screen
#ifdef PPSSPP_MULTICORE
void GamePauseScreen::OnGBASettings(UI::EventParams &e) {
    screenManager()->push(new GBASettingsScreen(gamePath_));
}
#endif
```

- [ ] **Step 4: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 5: Commit**

```bash
git add UI/PauseScreen.h UI/PauseScreen.cpp
git commit -m "[ui] Hook GBASettingsScreen into GBA pause menu"
```

---

### Task 4: Integrate display settings into GBACore::Render()

**Files:**

- Modify: `EmuCore/GBACore.h` — add aspect ratio helper
- Modify: `EmuCore/GBACore.cpp` — update Render() to use config for aspect ratio + integer scaling

- [ ] **Step 1: Add aspect ratio calculation helper**

In `EmuCore/GBACore.h`, add public method:

```cpp
// [PPSSPP-FORK] MultiCore: calculate render rect from aspect ratio config
void GetRenderRect(float &x, float &y, float &w, float &h, float viewW, float viewH) const;
```

- [ ] **Step 2: Implement aspect ratio + integer scaling in Render()**

In `EmuCore/GBACore.cpp`, implement `GetRenderRect`:

```cpp
// [PPSSPP-FORK] MultiCore: calculate GBA render rect based on config
void GBACore::GetRenderRect(float &x, float &y, float &w, float &h, float viewW, float viewH) const {
    float gbaAspect = 240.0f / 160.0f;  // 3:2 native
    float viewAspect = viewW / viewH;

    switch (g_Config.iGBAAspectRatio) {
    case 0:  // 3:2 native
        if (viewAspect > gbaAspect) {
            h = viewH;
            w = h * gbaAspect;
        } else {
            w = viewW;
            h = w / gbaAspect;
        }
        break;
    case 1:  // 16:9
        if (viewAspect > 16.0f/9.0f) {
            h = viewH;
            w = h * 16.0f/9.0f;
        } else {
            w = viewW;
            h = w / (16.0f/9.0f);
        }
        break;
    case 2:  // 1:1
        w = h = std::min(viewW, viewH);
        break;
    case 3:  // Stretch
        w = viewW;
        h = viewH;
        break;
    }

    // Integer scaling: snap to nearest multiple of 240x160
    if (g_Config.bGBAIntegerScaling) {
        int scaleW = (int)(w / 240.0f);
        int scaleH = (int)(h / 160.0f);
        int scale = std::max(1, std::min(scaleW, scaleH));
        w = 240.0f * scale;
        h = 160.0f * scale;
    }

    x = (viewW - w) / 2.0f;
    y = (viewH - h) / 2.0f;
}
```

Then update `Render()` to use `GetRenderRect` instead of hardcoded 3:2 letterbox.

- [ ] **Step 3: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 4: Commit**

```bash
git add EmuCore/GBACore.h EmuCore/GBACore.cpp
git commit -m "[gba] Integrate aspect ratio + integer scaling from GBA settings"
```

---

### Task 5: Verify builds (ON / OFF) + runtime

**Files:** None (verification only)

- [ ] **Step 1: Build MULTICORE=ON**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Success.

- [ ] **Step 2: Build MULTICORE=OFF**

```bash
cmake --build build-no-multicore --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Success (GBASettingsScreen not compiled, PauseScreen hook not active, config keys not defined).

- [ ] **Step 3: Quick runtime smoke test (xvfb)**

```bash
timeout 8 xvfb-run -a ./build-final/PPSSPPSDL --fullscreen --xres 800 --yres 480 2>&1 | grep -E "GBA|sync-fill|Settings"
```

Expected: No crashes, GBA recent loaded.

- [ ] **Step 4: Update progress doc**

```markdown
| **GBA Settings Screen** | ✅ **BARU** | Controls, Display, Audio — tidak ganggu PSP |
```

- [ ] **Step 5: Commit**

```bash
git add docs/progress-gba-support.md
git commit -m "[docs] Update progress: GBA Settings Screen ✅"
git push origin feature/lan-sync
```

---

### Task 6: Filter Control Mapping sections in GBA mode

**Problem:** Opening Control Mapping from GBA Settings menu shows all PSP sections (Standard PSP controls, Control modifiers, Extended PSP controls) which are irrelevant for GBA.

**Solution:** Skip PSP-only sections when `g_gbaModeActive`, show only Emulator controls + GBA controls.

**Files:**
- Modify: `UI/ControlMappingScreen.cpp`

- [ ] **Step 1: Add EmuScreen.h include**

```cpp
// [PPSSPP-FORK] MultiCore: check GBA mode to filter PSP-only sections
#include "UI/EmuScreen.h"
```

- [ ] **Step 2: Wrap PSP-only sections in `!g_gbaModeActive` check**

Change the `cats[]` array initialization. Since it's static const and checked at compile time, instead add a runtime filter in `CreateContentViews()`:

```cpp
for (size_t i = 0; i < numMappableKeys; i++) {
    // [PPSSPP-FORK] MultiCore: skip PSP-only categories in GBA mode
#ifdef PPSSPP_MULTICORE
    if (g_gbaModeActive && cats[curCat + 1].firstKey <= VIRTKEY_AXIS_RIGHT_Y_MAX) {
        curCat++;
        continue;
    }
#endif
    ...
}
```

Or simpler: check the category range in the loop and skip PSP-only categories.

- [ ] **Step 3: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 4: Runtime verify**

Buka GBA ROM → ESC → GBA Settings → Control Mapping — cuma liat "Emulator controls" + "GBA controls".
Buka PSP ROM → Settings → Control Mapping — semua sections normal.

- [ ] **Step 5: Commit**

```bash
git add UI/ControlMappingScreen.cpp
git commit -m "[ui] Filter PSP-only Control Mapping sections in GBA mode"
```

---

### Task 7: Fix left panel items in GBA Control Mapping mode

**Problem:** Left panel menu items (Clear All, Show PSP, combo settings) are PSP-centric, misleading, or dangerous in GBA mode.

**Audit:**

| Menu | Issue in GBA mode |
|------|-------------------|
| **Clear All** | 🚨 Wipes PSP mapping permanently. User expects only GBA mapping cleared. |
| **Default All** | ⚠️ Resets PSP + GBA. Default keyboard map lacks VIRTKEY_GBA_* entries, but GBA still works via PSP→GBA conversion. Acceptable. |
| **Autoconfigure** | 🟡 PSP pad detection only. Harmless (hidden unless pad connected). |
| **Show PSP** | 🟡 Label says "PSP" misleading in GBA mode. Opens VisualMappingScreen showing all keys. |
| **Allow combo mappings** | 🟡 PSP-centric (L+R+Start=menu). Hide in GBA mode. |
| **Strict combo input order** | 🟡 PSP-centric. Hide in GBA mode. |

**Files:**
- Modify: `UI/ControlMappingScreen.cpp`

- [ ] **Step 1: Hide combo settings in GBA mode**

Wrap combo checkboxes with `!g_gbaModeActive`:
```cpp
#ifdef PPSSPP_MULTICORE
if (!g_gbaModeActive) {
#endif
	parent->Add(new CheckBox(&g_Config.bAllowMappingCombos, km->T("Allow combo mappings")));
	parent->Add(new CheckBox(&g_Config.bStrictComboOrder, km->T("Strict combo input order")));
#ifdef PPSSPP_MULTICORE
}
#endif
```

- [ ] **Step 2: Fix "Show PSP" label in GBA mode**

Change label dynamically:
```cpp
parent->Add(new Choice(km->T(g_gbaModeActive ? "Show Key Map" : "Show PSP")))->OnClick.Add(...);
```
Note: "Show Key Map" must exist in KEYMAPPING i18n strings or fallback gracefully.

- [ ] **Step 3: Add confirmation to Clear All in GBA mode**

Instead of direct `ClearAllMappings()`, use `TriggerFinish` or system notice warning user. Or wrap with confirmation screen. Simpler: use `g_OSD.Show()` warning after clear or skip entirely.

Actually, simplest safe approach: **hide Clear All in GBA mode** to avoid accidental PSP mapping wipe. User can switch to PSP mode to Clear All if truly needed.

- [ ] **Step 4: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 5: Commit**

```bash
git add UI/ControlMappingScreen.cpp
git commit -m "[ui] Fix left panel items in GBA Control Mapping mode"
```
```

---

### Task 8: Use PopupMultiChoice for GBA display settings

**Problem:** Aspect Ratio (0-3) and Texture Filtering (0-1) use `PopupSliderChoice` which shows raw numbers instead of readable labels. User awam bingung.

**Solution:** Replace `PopupSliderChoice` with `PopupMultiChoice` for enum-based settings.

**Files:**
- Modify: `UI/GBASettingsScreen.cpp`

- [ ] **Step 1: Replace Aspect Ratio slider with PopupMultiChoice**

```cpp
static const char *aspectOptions[] = {"3:2", "16:9", "1:1", "Stretch"};
list->Add(new PopupMultiChoice(&g_Config.iGBAAspectRatio, gs->T("Aspect Ratio"), aspectOptions, 0, 4, I18NCat::GRAPHICS, screenManager()));
```

Result: shows "3:2", "16:9", "1:1", "Stretch" instead of "0", "1", "2", "3".

- [ ] **Step 2: Replace Texture Filtering slider with PopupMultiChoice**

```cpp
static const char *filterOptions[] = {"Nearest", "Linear"};
list->Add(new PopupMultiChoice(&g_Config.iGBATexFiltering, gs->T("Texture Filtering"), filterOptions, 0, 2, I18NCat::GRAPHICS, screenManager()));
```

Result: shows "Nearest" / "Linear" instead of "0" / "1".

- [ ] **Step 3: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 4: Commit**

```bash
git add UI/GBASettingsScreen.cpp
git commit -m "[ui] Use PopupMultiChoice for GBA display enum settings"
```
```

---

### Task 9: Fix runtime bugs — Texture Filtering + Volume not applied

**Problem 1 — Texture Filtering setting tidak dipakai:**
`InitRendering()` hardcodes `TextureFilter::NEAREST` tanpa baca `g_Config.iGBATexFiltering`.

**Problem 2 — Volume setting tidak dipakai:**
`GetRawAudio()` dan `GetMixedAudio()` tidak pernah apply `fGBAVolume` multiplier.

**Files:**
- Modify: `EmuCore/GBACore.cpp`

- [ ] **Step 1: Fix InitRendering to read iGBATexFiltering**

In `InitRendering()`, change sampler creation to read config:

```cpp
// Sampler — use config texture filtering
SamplerStateDesc samplerDesc{};
if (g_Config.iGBATexFiltering == 0) {
    samplerDesc.magFilter = TextureFilter::NEAREST;
    samplerDesc.minFilter = TextureFilter::NEAREST;
} else {
    samplerDesc.magFilter = TextureFilter::LINEAR;
    samplerDesc.minFilter = TextureFilter::LINEAR;
}
gbaSampler_ = draw->CreateSamplerState(samplerDesc);
```

- [ ] **Step 2: Fix audio pipeline to apply fGBAVolume**

In `GetRawAudio()`, apply volume multiplier after DC filter:

```cpp
float outL = left - dcCapRawL_;
float outR = right - dcCapRawR_;
dcCapRawL_ = (left - outL) * 0.996f;
dcCapRawR_ = (right - outR) * 0.996f;

// [PPSSPP-FORK] MultiCore: apply GBA volume setting
outL *= g_Config.fGBAVolume;
outR *= g_Config.fGBAVolume;

tempBuf[i * 2] = outL;
tempBuf[i * 2 + 1] = outR;
```

Similarly in `GetMixedAudio()`:

```cpp
float outL = left - dcCapL_;
float outR = right - dcCapR_;

// [PPSSPP-FORK] MultiCore: apply GBA volume setting
outL *= g_Config.fGBAVolume;
outR *= g_Config.fGBAVolume;

tempFloat[i * 2] = outL;
tempFloat[i * 2 + 1] = outR;
```

- [ ] **Step 3: Build & verify**

```bash
cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```

Expected: Build success.

- [ ] **Step 4: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[gba] Fix texture filtering + volume not applied from settings"
```
```
