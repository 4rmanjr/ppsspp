// [PPSSPP-FORK] MultiCore: Per-core config management
// Auto-switches configuration when changing emulator cores.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <string>
#include "EmuCore/EmuCore.h"

namespace EmuCore {

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

}  // namespace EmuCore
