// [PPSSPP-FORK] MultiCore: Emulator core factory
// Factory implementation + file extension detection.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/EmuCore.h"
#include "EmuCore/PSPCore.h"

#include "EmuCore/GBACore.h"

namespace EmuCore {

Type DetectType(const Path &romPath) {
	std::string ext = romPath.GetFileExtension();
	// Convert to lowercase for comparison
	for (auto &c : ext) {
		if (c >= 'A' && c <= 'Z')
			c += 32;
	}

	if (ext == ".gba" || ext == ".gb" || ext == ".gbc") {
		return Type::GBA;
	}

	// Default: PSP (existing behavior)
	return Type::PSP;
}

std::unique_ptr<Core> Create(const Path &romPath) {
	Type type = DetectType(romPath);

	switch (type) {
	case Type::GBA:
		return std::make_unique<GBACore>();
	case Type::PSP:
	default:
		return std::make_unique<PSPCore>();
	}
}

}  // namespace EmuCore
