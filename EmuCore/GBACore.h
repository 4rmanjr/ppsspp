// [PPSSPP-FORK] MultiCore: GBA emulator core via libmgba
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"

#ifdef PPSSPP_MULTICORE

// Forward declare mGBA types (included only in .cpp)
struct mCore;

namespace EmuCore {

class GBACore : public Core {
public:
	GBACore();
	~GBACore() override;

	Type GetType() const override { return Type::GBA; }

	bool LoadROM(const Path &path) override;
	void RunFrame() override;
	void Reset() override;
	void Render() override;

	int GetAudioSampleRate() const override;
	void GetAudioSamples(int16_t *buffer, size_t *samples) override;

	void SetKeys(uint32_t keys) override;
	uint32_t GetKeys() const override;

	size_t GetStateSize() const override;
	bool SaveState(void *buffer) override;
	bool LoadState(const void *buffer) override;

	void GetGameInfo(std::string &title, std::string &id) const override;

	// Get the raw video buffer for rendering (RGBA8888, 240x160)
	const uint32_t *GetVideoBuffer() const { return videoBuffer_; }

private:
	static constexpr int GBA_WIDTH = 240;
	static constexpr int GBA_HEIGHT = 160;

	mCore *core_ = nullptr;

	// Video buffer (RGBA8888)
	uint32_t videoBuffer_[GBA_WIDTH * GBA_HEIGHT]{};

	// Temporary audio buffer
	static constexpr size_t AUDIO_BUF_SIZE = 2048;
	int16_t audioBuffer_[AUDIO_BUF_SIZE * 2]{};
	size_t audioAvailable_ = 0;

	bool LoadROMInternal(const Path &path);
};

}  // namespace EmuCore

#endif  // PPSSPP_MULTICORE
