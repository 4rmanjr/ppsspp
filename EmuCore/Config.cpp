// [PPSSPP-FORK] MultiCore: Per-core config management
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"

namespace EmuCore {

const char *GetConfigSection(Type coreType) {
	switch (coreType) {
	case Type::GBA:
		return "GBA";
	case Type::PSP:
	default:
		return "PSP";
	}
}

void LoadConfig(Type coreType) {
	switch (coreType) {
	case Type::GBA:
		// GBA uses its own section in config file
		// For now, set sensible defaults here
		break;
	case Type::PSP:
	default:
		// PSP config is already loaded — no action needed
		break;
	}
}

std::string GetSavestateDir(Type coreType) {
	switch (coreType) {
	case Type::GBA:
		return (GetSysDirectory(DIRECTORY_SAVESTATE) / "GBA").ToString();
	case Type::PSP:
	default:
		return (GetSysDirectory(DIRECTORY_SAVESTATE) / "PSP").ToString();
	}
}

}  // namespace EmuCore
