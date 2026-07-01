// [PPSSPP-FORK] MultiCore: Emulator core factory
// Factory implementation + file extension detection.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/EmuCore.h"
#include "EmuCore/PSPCore.h"

#include "EmuCore/GBACore.h"

#include <cctype>

#ifdef PPSSPP_MULTICORE
// [PPSSPP-FORK] MultiCore: libzip + shared ZIP helper
#include "ext/libzip/zip.h"
#include "EmuCore/ZipHelper.h"
#endif

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
#ifdef PPSSPP_MULTICORE
	// [PPSSPP-FORK] MultiCore: peek inside .zip archives for GBA ROMs
	if (ext == ".zip") {
		int errcode = 0;
		zip_t *z = ZipHelper::OpenZip(romPath, &errcode);
		if (z) {
			zip_int64_t numEntries = zip_get_num_entries(z, 0);
			for (zip_int64_t i = 0; i < numEntries; i++) {
				struct zip_stat st;
				if (zip_stat_index(z, i, 0, &st) < 0)
					continue;
				const char *name = st.name;
				if (!name)
					continue;
				size_t nameLen = strlen(name);
				if (nameLen < 3)
					continue;
				// Check if entry ends with .gb (case insensitive, 3 chars)
				if (tolower((unsigned char)name[nameLen - 3]) == '.' &&
				    tolower((unsigned char)name[nameLen - 2]) == 'g' &&
				    tolower((unsigned char)name[nameLen - 1]) == 'b')
				{
					zip_close(z);
					return Type::GBA;
				}
				// Check if entry ends with .gba or .gbc (case insensitive, 4 chars)
				if (nameLen >= 4 &&
				    tolower((unsigned char)name[nameLen - 4]) == '.' &&
				    tolower((unsigned char)name[nameLen - 3]) == 'g' &&
				    tolower((unsigned char)name[nameLen - 2]) == 'b' &&
				    (tolower((unsigned char)name[nameLen - 1]) == 'a' ||
				     tolower((unsigned char)name[nameLen - 1]) == 'c'))
				{
					zip_close(z);
					return Type::GBA;
				}
			}
			zip_close(z);
		}
	}
#endif

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
