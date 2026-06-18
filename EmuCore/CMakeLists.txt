# [PPSSPP-FORK] MultiCore: EmuCore library
option(PPSSPP_MULTICORE "Enable multi-emulator support (GBA, future cores)" ON)

if(PPSSPP_MULTICORE)
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

    add_subdirectory(${CMAKE_SOURCE_DIR}/ext/libmgba ${CMAKE_BINARY_DIR}/ext/libmgba)

    # EmuCore static library
    add_library(EmuCore STATIC
        EmuCore.cpp
        PSPCore.cpp
        GBACore.cpp
        Config.cpp
    )

    target_include_directories(EmuCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(EmuCore PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(EmuCore PRIVATE mgba)

    # mGBA include paths for EmuCore compilation
    # mGBA needs ENABLE_VFS for VFile/VFileOpen
    target_compile_definitions(EmuCore PRIVATE ENABLE_VFS MINIMAL_CORE=2)
    target_include_directories(EmuCore PRIVATE ${CMAKE_SOURCE_DIR}/ext/libmgba/include)

endif()
