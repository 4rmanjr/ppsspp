// [PPSSPP-FORK] MultiCore: Emulator core abstraction interface
// Abstract interface for all emulator cores (PSP, GBA, future cores).
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "Common/File/Path.h"
#include "Common/CommonTypes.h"

namespace EmuCore {

enum class Type {
	PSP,
	GBA,
};

// Detect emulator core type based on file extension.
Type DetectType(const Path &romPath);

// Abstract interface for all emulator cores.
class Core {
public:
	virtual ~Core() = default;

	virtual Type GetType() const = 0;

	// Lifecycle
	virtual bool LoadROM(const Path &path) = 0;
	virtual void RunFrame() = 0;
	virtual void Reset() = 0;

	// Rendering — GBA renders to buffer, PSP renders via GPU
	virtual void Render() = 0;

	// Audio
	virtual int GetAudioSampleRate() const = 0;
	virtual void GetAudioSamples(int16_t *buffer, size_t *samples) = 0;

	// Input
	virtual void SetKeys(uint32_t keys) = 0;
	virtual uint32_t GetKeys() const = 0;

	// Savestate
	virtual size_t GetStateSize() const = 0;
	virtual bool SaveState(void *buffer) = 0;
	virtual bool LoadState(const void *buffer) = 0;

	// Game info
	virtual void GetGameInfo(std::string &title, std::string &id) const = 0;
};

// Factory: create the appropriate core for a given ROM path.
std::unique_ptr<Core> Create(const Path &romPath);

}  // namespace EmuCore
