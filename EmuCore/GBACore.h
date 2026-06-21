// [PPSSPP-FORK] MultiCore: GBA emulator core via libmgba
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"
#include "Common/GPU/thin3d.h"

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
	void Render(Draw::DrawContext *draw) override;
	void DeviceLost() override;
	void DeviceRestored(Draw::DrawContext *draw) override;

	int GetAudioSampleRate() const override;
	void GetAudioSamples(int16_t *buffer, size_t *samples) override;

	void SetKeys(uint32_t keys) override;
	uint32_t GetKeys() const override;

	size_t GetStateSize() const override;
	bool SaveState(void *buffer) override;
	bool LoadState(const void *buffer) override;

	void GetGameInfo(std::string &title, std::string &id) const override;

	// Convert PSP key bitmask to GBA key bitmask
	static uint32_t PSPSKeysToGBA(uint32_t pspKeys);

	// Get the raw video buffer for rendering (RGBA8888, 240x160)
	const uint32_t *GetVideoBuffer() const { return videoBuffer_; }

	// [PPSSPP-FORK] MultiCore: configure save memory directory for SRAM/Flash
	void SetSaveDirectory(const std::string &dir);

	// [PPSSPP-FORK] MultiCore: GBA video rendering — lazy init, draw, shutdown
	void InitRendering(Draw::DrawContext *draw);
	void ShutdownRendering();

	// [PPSSPP-FORK] MultiCore: get resampled + converted audio (32768→44100 Hz, int16→int32)
	// Returns stereo pairs at 44100 Hz (735 per frame at 60fps).
	void GetMixedAudio(int32_t *buffer, size_t *stereoPairs);

	// [PPSSPP-FORK] MultiCore: clear all pending audio (for fast forward frame skipping)
	void ClearAudio();

	// [PPSSPP-FORK] MultiCore: save state to file (slot-based, .gbast extension)
	bool SaveStateToFile(int slot);
	bool LoadStateFromFile(int slot);
	static std::string GetSavePrefix(const std::string &title, const std::string &id);

private:
	void CloseSaveMemory();
	static constexpr int GBA_WIDTH = 240;
	static constexpr int GBA_HEIGHT = 160;

	mCore *core_ = nullptr;

	// Video buffer (RGBA8888)
	uint32_t videoBuffer_[GBA_WIDTH * GBA_HEIGHT]{};

	// Raw mGBA output buffer (XBGR8 format, mColor)
	uint32_t rawVideoBuffer_[GBA_WIDTH * GBA_HEIGHT]{};

	// Audio constants
	static constexpr size_t AUDIO_BUF_SIZE = 2048;
	static constexpr int GBA_NATIVE_RATE = 32768;  // mGBA core default
	static constexpr int TARGET_RATE = 44100;       // PPSSPP mixer rate

	// mGBA's own sinc resampler (32768 Hz -> 44100 Hz), void* for PIMPL
	void* resampler_;
	void* resampleDest_;

	// Temporary output buffer after resample
	int16_t audioBuffer_[AUDIO_BUF_SIZE * 2]{};
	size_t audioStereoPairs_ = 0;

	// DC blocking filter state (SkyEmu-inspired high-pass for SOUNDBIAS DC offset)
	float dcCapL_ = 0.0f;
	float dcCapR_ = 0.0f;

	// Audio rate from mGBA core (changes with SOUNDBIAS, but always derived from 32768 Hz base)
	unsigned coreSampleRate_ = 32768;

	// Track previous frame's mAudioBuffer size for rate detection
	size_t prevAvailable_ = 0;

	bool LoadROMInternal(const Path &path);
	std::string GetSavePrefix() const;

	std::string saveDir_;

	// Thin3D rendering resources (lazy-allocated at first Render() call)
	Draw::Texture *gbaTexture_ = nullptr;
	Draw::Pipeline *gbaPipeline_ = nullptr;
	Draw::SamplerState *gbaSampler_ = nullptr;
};

}  // namespace EmuCore

#endif  // PPSSPP_MULTICORE
