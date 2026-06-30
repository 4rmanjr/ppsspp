// [PPSSPP-FORK] MultiCore: shared ZIP open helper implementation
// Consolidates duplicated Android content:// URI handling from:
//   - EmuCore/EmuCore.cpp (DetectType)
//   - EmuCore/GBACore.cpp (LoadROMFromZip)
//   - UI/MainScreen.cpp (OpenZipForRead / HasGBAROM)
//
// Usage:
//   int errcode = 0;
//   zip_t *z = ZipHelper::OpenZip(path, &errcode);
//   if (!z) { /* handle error */ }
//   // ... use z ...
//   zip_close(z);

#include "EmuCore/ZipHelper.h"

#include "Common/Log.h"

#include "ext/libzip/zip.h"

#if defined(__ANDROID__) && defined(ANDROID)
#include "Common/File/AndroidStorage.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ZipHelper {

zip_t *OpenZip(const Path &path, int *errcode) {
	std::string pathStr = path.ToString();

#if defined(__ANDROID__) && defined(ANDROID)
	if (pathStr.find("content://") == 0) {
		int fd = Android_OpenContentUriFd(pathStr, Android_OpenContentUriMode::READ);
		if (fd < 0) {
			ERROR_LOG(Log::System, "[ZipHelper] Failed to open content URI: %s", path.c_str());
			if (errcode) *errcode = -1;
			return nullptr;
		}

		struct stat st;
		if (fstat(fd, &st) != 0 || st.st_size == 0) {
			ERROR_LOG(Log::System, "[ZipHelper] fstat failed or empty file: %s", path.c_str());
			close(fd);
			if (errcode) *errcode = -1;
			return nullptr;
		}

		// Limit to 64MB to prevent OOM DOS
		if (st.st_size > 64 * 1024 * 1024) {
			ERROR_LOG(Log::System, "[ZipHelper] Content URI exceeds 64MB limit: %s (%llu bytes)", path.c_str(), (unsigned long long)st.st_size);
			close(fd);
			if (errcode) *errcode = -1;
			return nullptr;
		}

		uint8_t *buf = (uint8_t *)malloc(st.st_size);
		if (!buf) {
			ERROR_LOG(Log::System, "[ZipHelper] Failed to allocate %llu bytes for content URI: %s", (unsigned long long)st.st_size, path.c_str());
			close(fd);
			if (errcode) *errcode = -1;
			return nullptr;
		}

		// Read entire file into buffer (EINTR-safe)
		ssize_t total = 0;
		while (total < st.st_size) {
			ssize_t r = read(fd, buf + total, st.st_size - total);
			if (r <= 0) {
				if (r < 0 && errno == EINTR)
					continue;
				ERROR_LOG(Log::System, "[ZipHelper] Error reading content URI: %s (read returned %zd)", path.c_str(), r);
				free(buf);
				close(fd);
				if (errcode) *errcode = -1;
				return nullptr;
			}
			total += r;
		}
		close(fd);

		// Create zip from memory buffer — libzip takes ownership (freep=1)
		zip_error_t err{};
		zip_source_t *src = zip_source_buffer_create(buf, total, 1, &err);
		if (!src) {
			ERROR_LOG(Log::System, "[ZipHelper] Failed to create zip source from memory: %s", zip_error_strerror(&err));
			free(buf);
			if (errcode) *errcode = err.zip_err;
			return nullptr;
		}

		zip_error_t ec{};
		zip_t *z = zip_open_from_source(src, ZIP_RDONLY, &ec);
		if (!z) {
			ERROR_LOG(Log::System, "[ZipHelper] Failed to open zip from content URI: %s", zip_error_strerror(&ec));
			zip_source_free(src);
			if (errcode) *errcode = ec.zip_err;
			return nullptr;
		}

		DEBUG_LOG(Log::System, "[ZipHelper] Opened zip from content URI: %s (%llu bytes)", path.c_str(), (unsigned long long)st.st_size);
		return z;
	}
#endif

	// Standard file path
	int ec = 0;
	zip_t *z = zip_open(path.c_str(), ZIP_RDONLY, &ec);
	if (!z) {
		DEBUG_LOG(Log::System, "[ZipHelper] Failed to open zip file: %s (errcode=%d)", path.c_str(), ec);
		if (errcode) *errcode = ec;
		return nullptr;
	}
	return z;
}

}  // namespace ZipHelper
