// [PPSSPP-FORK] MultiCore: Emulator core factory
// Factory implementation + file extension detection.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/EmuCore.h"
#include "EmuCore/PSPCore.h"

#include "EmuCore/GBACore.h"

#include <cctype>

#ifdef PPSSPP_MULTICORE
// [PPSSPP-FORK] MultiCore: libzip for detecting GBA ROMs inside .zip archives
#include "ext/libzip/zip.h"
#if defined(__ANDROID__) && defined(ANDROID)
#include "Common/File/AndroidStorage.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
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
		std::string pathStr = romPath.ToString();
		zip_t *z = nullptr;
#if defined(__ANDROID__) && defined(ANDROID)
		if (pathStr.find("content://") == 0) {
			int fd = Android_OpenContentUriFd(pathStr, Android_OpenContentUriMode::READ);
			if (fd >= 0) {
				struct stat st;
				if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size <= 64 * 1024 * 1024) {
					uint8_t *buf = (uint8_t *)malloc(st.st_size);
					if (buf) {
						ssize_t total = 0;
						while (total < st.st_size) {
							ssize_t r = read(fd, buf + total, st.st_size - total);
							if (r <= 0) { if (r < 0 && errno == EINTR) continue; break; }
							total += r;
						}
						close(fd);
						zip_error_t err{};
						zip_source_t *src = zip_source_buffer_create(buf, total, 1, &err);
						if (!src) { free(buf); }
						if (src) {
							zip_error_t ec{};
							z = zip_open_from_source(src, ZIP_RDONLY, &ec);
							if (!z) zip_source_free(src);
						}
					} else {
						close(fd);
					}
				} else {
					close(fd);
				}
			}
		} else {
			int errcode = 0;
			z = zip_open(romPath.c_str(), ZIP_RDONLY, &errcode);
		}
#else
		int errcode = 0;
		z = zip_open(romPath.c_str(), ZIP_RDONLY, &errcode);
#endif
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
