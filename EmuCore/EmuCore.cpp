// [PPSSPP-FORK] MultiCore: Emulator core factory
// Factory implementation + file extension detection.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/EmuCore.h"
#include "EmuCore/PSPCore.h"

#ifdef PPSSPP_MULTICORE
#include "EmuCore/GBACore.h"
#endif

namespace EmuCore {

Type DetectType(const Path &romPath) {
	std::string ext = romPath.GetFileExtension();
	// Convert to lowercase for comparison
	for (auto &c : ext) {
		if (c >= 'A' && c <= 'Z')
			c += 32;
	}

#ifdef PPSSPP_MULTICORE
	if (ext == ".gba" || ext == ".gb" || ext == ".gbc") {
		return Type::GBA;
	}
#endif

	// Default: PSP (existing behavior)
	return Type::PSP;
}

std::unique_ptr<Core> Create(const Path &romPath) {
	Type type = DetectType(romPath);

	switch (type) {
#ifdef PPSSPP_MULTICORE
	case Type::GBA:
		return std::make_unique<GBACore>();
#endif
	case Type::PSP:
	default:
		return std::make_unique<PSPCore>();
	}
}

}  // namespace EmuCore
