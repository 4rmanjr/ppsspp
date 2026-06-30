# [PPSSPP-FORK] MultiCore: EmuCore library
# PPSSPP_MULTICORE is always ON — no conditional guard
add_definitions(-DPPSSPP_MULTICORE)

# Build libmgba as static library (core only, no frontends)
set(LIBMGBA_ONLY ON CACHE BOOL "Build mGBA as static library only" FORCE)
set(M_CORE_GBA ON CACHE BOOL "Build GBA core" FORCE)
set(M_CORE_GB OFF CACHE BOOL "Build GB core" FORCE)
set(BUILD_GL OFF CACHE BOOL "Disable GL in library build" FORCE)
set(BUILD_GLES2 OFF CACHE BOOL "Disable GLES2 in library build" FORCE)
set(USE_FFMPEG OFF CACHE BOOL "Disable FFmpeg" FORCE)
set(USE_ZLIB OFF CACHE BOOL "Disable zlib" FORCE)
set(USE_PNG OFF CACHE BOOL "Disable PNG" FORCE)
set(USE_LZMA OFF CACHE BOOL "Disable LZMA" FORCE)
set(DISABLE_FRONTENDS ON CACHE BOOL "Disable frontend builds" FORCE)

# Pre-set HAVE_CRC32 so mGBA uses zlib's crc32 instead of its own
# (avoids duplicate symbol at link time with PPSSPP's zlib)
set(HAVE_CRC32 ON CACHE INTERNAL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/ext/libmgba ${CMAKE_BINARY_DIR}/ext/libmgba)

# [PPSSPP-FORK] MultiCore: suppress mGBA -Wformat-truncation warnings (newer GCC)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mgba PRIVATE -Wno-format-truncation)
endif()

# EmuCore static library
add_library(EmuCore STATIC
    EmuCore.cpp
    PSPCore.cpp
    GBACore.cpp
    GBAPostProcessor.cpp
    Config.cpp
    RecentFilesRegistry.cpp
    ZipHelper.cpp
)

target_include_directories(EmuCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(EmuCore PRIVATE ${CMAKE_SOURCE_DIR})
# [PPSSPP-FORK] MultiCore: --whole-archive prevents linker from stripping
# mGBA function pointers (init, deinit, loadROM, etc.) that are only called
# indirectly through the mCore vtable.
target_link_libraries(EmuCore PRIVATE -Wl,--whole-archive mgba -Wl,--no-whole-archive)

# [PPSSPP-FORK] MultiCore: link libzip for .zip ROM loading
target_link_libraries(EmuCore PRIVATE libzip)

# mGBA include paths for EmuCore compilation
target_compile_definitions(EmuCore PRIVATE ENABLE_VFS ENABLE_VFS_FD ENABLE_DIRECTORIES)
target_include_directories(EmuCore PRIVATE ${CMAKE_SOURCE_DIR}/ext/libmgba/include)
