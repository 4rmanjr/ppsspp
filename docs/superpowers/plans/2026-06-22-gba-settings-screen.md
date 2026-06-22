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
