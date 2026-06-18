// [PPSSPP-FORK] MultiCore: Per-core config management
// Auto-switches configuration when changing emulator cores.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <string>
#include "EmuCore/EmuCore.h"

namespace EmuCore {

// Load configuration for the specified core type.
void LoadConfig(Type coreType);

// Get the savestate directory for the specified core type.
std::string GetSavestateDir(Type coreType);

// Get the config section name for the specified core type.
const char *GetConfigSection(Type coreType);

}  // namespace EmuCore
