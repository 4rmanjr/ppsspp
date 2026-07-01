// [PPSSPP-FORK] MultiCore: shared ZIP open helper
// Extracts duplicated Android content:// URI handling from:
//   - EmuCore/EmuCore.cpp (DetectType)
//   - EmuCore/GBACore.cpp (LoadROMFromZip)
//   - UI/MainScreen.cpp (OpenZipForRead / HasGBAROM)
#pragma once

#include "Common/File/Path.h"
#include "ext/libzip/zip.h"

namespace ZipHelper {

// Open a ZIP archive from any supported path type.
// Handles both standard file paths and Android content:// URIs.
// On Android content:// URIs, reads the entire file into memory with a 64MB limit
// to prevent OOM. The memory is heap-allocated and owned by libzip (freep=1).
// Returns nullptr on failure; errcode is set to the libzip error code if applicable.
zip_t *OpenZip(const Path &path, int *errcode = nullptr);

}  // namespace ZipHelper
