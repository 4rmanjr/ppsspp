// [PPSSPP-FORK] MultiCore: Per-core config management
// Auto-switches configuration when changing emulator cores.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <string>
#include <cstdio>
#include "EmuCore/EmuCore.h"

namespace EmuCore {

// Maximum number of per-core touch buttons (A, B, D-Pad, L, R, Start, Select + spare)
static constexpr int MAX_TOUCH_BUTTONS = 16;

// A single button in a per-core touch layout.
// Uses PSP CTRL_ constants for keycode (converted via PSPSKeysToGBA() in SetKeys()).
struct CoreTouchButton {
	int keyCode = 0;     // PSP CTRL_ constant (CTRL_CROSS = GBA A, etc.)
	float x = 0.0f;      // Normalized X position (0-1)
	float y = 0.0f;      // Normalized Y position (0-1)
	float w = 0.09f;     // Normalized width
	float h = 0.09f;     // Normalized height
	char label[16] = ""; // Display label ("A", "B", etc.)
	bool visible = true;

	bool operator==(const CoreTouchButton &o) const {
		return keyCode == o.keyCode && x == o.x && y == o.y;
	}
	bool operator!=(const CoreTouchButton &o) const { return !(*this == o); }
};

// Per-core touch layout (one landscape + one portrait).
struct CoreTouchConfig {
	CoreTouchButton buttons[MAX_TOUCH_BUTTONS]{};
	int count = 0;

	void Clear() { count = 0; }
	void Add(int keyCode, float x, float y, float w, float h, const char *label, bool visible = true) {
		if (count >= MAX_TOUCH_BUTTONS) return;
		buttons[count].keyCode = keyCode;
		buttons[count].x = x;
		buttons[count].y = y;
		buttons[count].w = w;
		buttons[count].h = h;
		snprintf(buttons[count].label, sizeof(buttons[count].label), "%s", label);
		buttons[count].visible = visible;
		count++;
	}
};

// Per-core touch configs (indexed by Type enum).
extern CoreTouchConfig g_coreTouchLandscape[(int)Type::COUNT];
extern CoreTouchConfig g_coreTouchPortrait[(int)Type::COUNT];

// Get per-core touch config for a specific core type + orientation.
inline const CoreTouchConfig &GetTouchConfig(Type coreType, bool portrait) {
	int idx = (int)coreType;
	if (idx < 0 || idx >= (int)Type::COUNT) idx = 0;
	return portrait ? g_coreTouchPortrait[idx] : g_coreTouchLandscape[idx];
}
inline CoreTouchConfig &GetTouchConfigMutable(Type coreType, bool portrait) {
	int idx = (int)coreType;
	if (idx < 0 || idx >= (int)Type::COUNT) idx = 0;
	return portrait ? g_coreTouchPortrait[idx] : g_coreTouchLandscape[idx];
}

// Get the INI section name for per-core touch layout.
const char *GetTouchConfigSection(Type coreType, bool portrait);

// Populate default GBA touch layout for a CoreTouchConfig.
void FillDefaultGBATouchLayout(CoreTouchConfig &cfg);

// Initialize default touch configs for all cores.
void InitDefaultTouchConfigs();

// Load per-core touch config from INI (populates with defaults if missing).
void LoadTouchConfig(Type coreType);

// Save per-core touch config to INI.
void SaveTouchConfig(Type coreType);

// These functions below are for PSP ↔ GBA config switching — unchanged.

// Save current config state (for PSP → GBA switch)
void SaveCurrentConfig();

// Restore previously saved config state (for GBA → PSP switch)
void RestoreSavedConfig();

// Apply GBA-specific defaults for first-time setup
void ApplyGBADefaults();

// Load configuration for the specified core type.
void LoadConfig(Type coreType);

// Save configuration for the specified core before switch.
void SaveConfig(Type coreType);

// Get the savestate directory for the specified core type.
std::string GetSavestateDir(Type coreType);

// Get the config section name for the specified core type.
const char *GetConfigSection(Type coreType);

// Get directory name for core-specific files (e.g., "GBA", "N64", "PSP").
// Used for save/state folder paths, NOT config sections.
const char *GetCoreDirectory(Type coreType);

// Get save prefix for core-specific save files (e.g., "GBA_", "N64_", "PSP_").
// Prepended to save state filenames for disambiguation.
const char *GetCoreSavePrefix(Type coreType);

}  // namespace EmuCore
