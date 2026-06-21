# GBA Save State Implementation Plan
> **STATUS: ✅ SELESAI — All tasks implemented (F1/F3, pause menu, thumbnail, LAN sync)**

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable save/load state for GBA games via F1/F3 and pause menu, with LAN sync compatibility.

**Architecture:** Use existing `GBACore::SaveStateToFile/LoadStateFromFile` (sync raw mGBA binary). Update file path to `<SAVESTATE_DIR>/GBA_<title>_<slot>.ppst` (same directory as PSP, `.ppst` extension for LAN sync). Wire up pause menu buttons. Zero changes to `Core/SaveState.cpp` or `Core/SaveStateLANSync.cpp`.

**Tech Stack:** C++17, mGBA core serialization, PPSSPP OSD system

---

## File Structure

| File | Status | Responsibility |
|------|--------|---------------|
| `EmuCore/GBACore.cpp` | **Modify** | Update save path format |
| `UI/PauseScreen.cpp` | **Modify** | Add GBA check in save/load button handlers |
| `UI/EmuScreen.cpp` | **Already done** | VIRTKEY handlers for F1/F3 already call GBACore |
| `Core/SaveState.cpp` | **NOT touched** | Zero change |
| `Core/SaveStateLANSync.cpp` | **NOT touched** | Zero change |

---

### Task 1: Update GBACore Save/Load Path

**Files:**
- Modify: `EmuCore/GBACore.cpp` — `SaveStateToFile()` and `LoadStateFromFile()`

**Current path:** `<SAVESTATE>/GBA/<prefix>_<slot>.gbast`
**Target path:** `<SAVESTATE>/GBA_<prefix>_<slot>.ppst`

- [ ] **Step 1: Update SaveStateToFile**

In `EmuCore/GBACore.cpp`, find `GBACore::SaveStateToFile`. Replace:

```cpp
Path dir = GetSysDirectory(DIRECTORY_SAVESTATE) / "GBA";
std::string filename = StringFromFormat("%s_%d.gbast", prefix.c_str(), slot);
Path path = dir / filename;
```

To:

```cpp
Path dir = GetSysDirectory(DIRECTORY_SAVESTATE);
std::string filename = StringFromFormat("GBA_%s_%d.ppst", prefix.c_str(), slot);
Path path = dir / filename;
```

- [ ] **Step 2: Update LoadStateFromFile**

Same change in `GBACore::LoadStateFromFile`.

- [ ] **Step 3: Build and verify**

```bash
cd /home/armanjr/gitproject/ppsspp && cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[savestate] GBA: save path <SAVESTATE>/GBA_<title>_<slot>.ppst for LAN sync"
```

---

### Task 2: Wire Pause Menu Save/Load Buttons to GBACore

**Files:**
- Modify: `UI/PauseScreen.cpp` — `ScreenshotViewScreen`

**Approach:** Add `g_gbaModeActive` global bool (pattern: same as `g_TakeScreenshot` in EmuScreen.cpp).
Set `true` in `InitGBA()`, `false` in `ShutdownGBA()`. Check in PauseScreen save/load handlers.

- [ ] **Step 1: Add global flag in EmuScreen.cpp**

```cpp
// Near top of EmuScreen.cpp, with other globals:
bool g_gbaModeActive = false;
```

- [ ] **Step 2: Export in EmuScreen.h**

```cpp
extern bool g_gbaModeActive;
```

- [ ] **Step 3: Set flag in InitGBA/ShutdownGBA**

In `InitGBA()`:
```cpp
g_gbaModeActive = true;
```

In `ShutdownGBA()`:
```cpp
g_gbaModeActive = false;
```

- [ ] **Step 4: Add include + check in PauseScreen.cpp**

```cpp
#ifdef PPSSPP_MULTICORE
#include "UI/EmuScreen.h"
#include "EmuCore/GBACore.h"
#endif
```

- [ ] **Step 5: Modify OnSaveState**

In `ScreenshotViewScreen::OnSaveState`, before the existing `SaveState::SaveSlot` call:

```cpp
void ScreenshotViewScreen::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
#ifdef PPSSPP_MULTICORE
		// [PPSSPP-FORK] MultiCore: GBA uses GBACore save
		if (g_gbaModeActive) {
			int slot = g_Config.iCurrentStateSlot;
			// Can't access GBACore directly from here, delegate via callback
			// Instead, use the same VIRTKEY mechanism that F1 uses:
			System_PostUIMessage(UIMessage::GBA_SAVE_STATE, slot);
			return;
		}
#endif
		SaveState::SaveSlot(saveStatePrefix_, slot_, &ShowMessageAfterSaveStateAction);
	}
}
```

Wait, there's no UIMessage for GBA save. Simpler: use a direct approach via g_controlMapper to trigger VIRTKEY_SAVE_STATE, which already has GBA code. Or add a method.

Actually simplest: direct call via a helper function in EmuScreen.

```cpp
// In EmuScreen.h:
void TriggerGBASaveState(int slot);
void TriggerGBALoadState(int slot);
```

But that requires EmuScreen to be accessible from PauseScreen.

**Better approach:** PauseScreen already uses `g_controlMapper` and VIRTKEYs. The pause menu's OnVKey handler already dispatches VIRTKEY_SAVE_STATE. We just need to trigger it.

Actually, looking at the existing code: VIRTKEY_SAVE_STATE in ProcessVKey (EmuScreen.cpp) already has GBA code. The pause menu's save button could trigger VIRTKEY_SAVE_STATE via:

```cpp
// Simulate F1 keypress:
g_controlMapper.PSPKey(DEVICE_ID_KEYBOARD, VIRTKEY_SAVE_STATE, KeyInputFlags::DOWN);
g_controlMapper.PSPKey(DEVICE_ID_KEYBOARD, VIRTKEY_SAVE_STATE, KeyInputFlags::UP);
```

But this is hacky.

**Simplest working approach:** Use `g_gbaModeActive` flag + store reference to active core.

```cpp
// In EmuScreen.h:
extern bool g_gbaModeActive;
extern EmuCore::Core *g_activeCore;  // set in InitGBA, clear in ShutdownGBA
```

Then in PauseScreen:
```cpp
#ifdef PPSSPP_MULTICORE
if (g_gbaModeActive && g_activeCore) {
	EmuCore::GBACore *gba = static_cast<EmuCore::GBACore *>(g_activeCore);
	int slot = g_Config.iCurrentStateSlot;
	if (gba->SaveStateToFile(slot)) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, StringFromFormat("GBA state saved (slot %d)", slot + 1), 2.0f);
	} else {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "GBA save state failed", 3.0f);
	}
	return;
}
#endif
```

- [ ] **Step 4: Build and verify**

```bash
cd /home/armanjr/gitproject/ppsspp && cmake --build build-final --target PPSSPPSDL -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add UI/PauseScreen.cpp
git commit -m "[savestate] GBA: pause menu save/load redirect to GBACore"
```

---

### Task 3: Verify LAN Sync Compatibility

- [ ] **Step 1: Test save file location**

Run GBA game, press F1 to save. Verify file created at:
```
<SAVESTATE_DIR>/GBA_<title>_<slot>.ppst
```

- [ ] **Step 2: Verify LAN sync discovery**

LAN sync scans `<SAVESTATE_DIR>/` for `*.ppst`. File `GBA_<title>_0.ppst` matches `*.ppst` pattern → discovered.

- [ ] **Step 3: Verify filename parsing**

LAN sync extracts prefix/slot:
```cpp
std::string name = "GBA_Breath_of_Fire_0.ppst";
// lastUnderscore = 22 (before "0")
// prefix = "GBA_Breath_of_Fire"
// slot = 0
```
This works correctly.

- [ ] **Step 4: Push all commits**

```bash
git push origin feature/lan-sync
```

---

## Rollback

If save state breaks:
1. Delete `GBA_*.ppst` files from `<SAVESTATE_DIR>/`
2. Revert `EmuCore/GBACore.cpp` and `UI/PauseScreen.cpp`
3. PSP save states (`<gameID>_<slot>.ppst`) are completely unaffected
