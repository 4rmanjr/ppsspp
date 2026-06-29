// [PPSSPP-FORK] MultiCore: GBA emulator core via libmgba
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#pragma once


#include "EmuCore/EmuCore.h"
#include "EmuCore/GBAPostProcessor.h"
#include "Common/GPU/thin3d.h"

// Forward declare mGBA types (included only in .cpp)
struct mCore;

// Audio buffer size used by GBACore internals and external callers
static constexpr size_t GBA_AUDIO_BUF_SIZE = 4096;

namespace EmuCore {

class GBACore : public Core {
public:
	// GBA button bit indices (from mgba/internal/gba/input.h)
	static constexpr uint32_t GBA_BIT_A      = 1 << 0;
	static constexpr uint32_t GBA_BIT_B      = 1 << 1;
	static constexpr uint32_t GBA_BIT_SELECT = 1 << 2;
	static constexpr uint32_t GBA_BIT_START  = 1 << 3;
	static constexpr uint32_t GBA_BIT_RIGHT  = 1 << 4;
	static constexpr uint32_t GBA_BIT_LEFT   = 1 << 5;
	static constexpr uint32_t GBA_BIT_UP     = 1 << 6;
	static constexpr uint32_t GBA_BIT_DOWN   = 1 << 7;
	static constexpr uint32_t GBA_BIT_R      = 1 << 8;
	static constexpr uint32_t GBA_BIT_L      = 1 << 9;

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
	// [PPSSPP-FORK] MultiCore: set GBA keys with PSP buttons + GBA VIRTKEY bits combined
	void SetKeys(uint32_t pspKeys, uint32_t gbaVirtKeys);
	uint32_t GetKeys() const override;

	size_t GetStateSize() const override;
	bool SaveState(void *buffer) override;
	bool LoadState(const void *buffer) override;

	void GetGameInfo(std::string &title, std::string &id) const override;

	// Convert PSP key bitmask to GBA key bitmask
	static uint32_t PSPSKeysToGBA(uint32_t pspKeys);

	// [PPSSPP-FORK] MultiCore: get raw video buffer (R,G,B,0 byte order, 240x160)
	const uint32_t *GetVideoBuffer() const { return rawVideoBuffer_; }

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

	// [PPSSPP-FORK] MultiCore: get raw resampled int16 audio (44100 Hz) with DC filter
	// Returns pointer to internal buffer and stereo pair count.
	// Buffer is valid until next RunFrame() call.
	const int16_t *GetRawAudio(size_t *stereoPairs);

	// [PPSSPP-FORK] MultiCore: save state to file (slot-based, .gbast extension)
	bool SaveStateToFile(int slot);
	bool LoadStateFromFile(int slot);
	static std::string GetSavePrefix(const std::string &title, const std::string &id);
	std::string GetSavePrefix() const;

	// [PPSSPP-FORK] MultiCore: calculate render rect from aspect ratio config
	void GetRenderRect(float &x, float &y, float &w, float &h, float viewW, float viewH) const;

private:
	void CloseSaveMemory();
	static constexpr int GBA_WIDTH = 240;
	static constexpr int GBA_HEIGHT = 160;

	mCore *core_ = nullptr;

	// [PPSSPP-FORK] MultiCore: raw mGBA output buffer (R,G,B,0 byte order, uint32_t per pixel)
	uint32_t rawVideoBuffer_[GBA_WIDTH * GBA_HEIGHT]{};

	// Audio constants
	static constexpr size_t AUDIO_BUF_SIZE = GBA_AUDIO_BUF_SIZE;
	static constexpr int GBA_NATIVE_RATE = 32768;  // mGBA core default
	static constexpr int TARGET_RATE = 44100;       // PPSSPP mixer rate
	static constexpr int TARGET_PAIRS = TARGET_RATE / 60;  // 735 at 44100Hz 60fps

	// mGBA's own sinc resampler (32768 Hz -> 44100 Hz), void* for PIMPL
	void* resampler_;
	void* resampleDest_;

	// Temporary output buffer after resample
	int16_t audioBuffer_[AUDIO_BUF_SIZE * 2]{};
	size_t audioStereoPairs_ = 0;

	// DC blocking filter state (SkyEmu-inspired high-pass for SOUNDBIAS DC offset)
	float dcCapL_ = 0.0f;     // Used by GetMixedAudio (EMA tracker)
	float dcCapR_ = 0.0f;
	float dcCapRawL_ = 0.0f;  // Used by GetRawAudio (self-decay capacitor)
	float dcCapRawR_ = 0.0f;

	// [PPSSPP-FORK] GBA Audio Improvement: Low-pass filter states (EMA) to roll off harsh 8-bit quantization noise
	float lowPassL_ = 0.0f;
	float lowPassR_ = 0.0f;
	float lowPassRawL_ = 0.0f;
	float lowPassRawR_ = 0.0f;

	// Audio rate from mGBA core (changes with SOUNDBIAS, but always derived from 32768 Hz base)
	unsigned coreSampleRate_ = 32768;

	// Track previous frame's mAudioBuffer size for rate detection
	size_t prevAvailable_ = 0;

	bool LoadROMInternal(const Path &path);

	std::string saveDir_;

	// Thin3D rendering resources (lazy-allocated at first Render() call)
	Draw::Texture *gbaTexture_ = nullptr;
	Draw::Pipeline *gbaPipeline_ = nullptr;
	Draw::SamplerState *gbaSampler_ = nullptr;

	// [PPSSPP-FORK] MultiCore: offscreen framebuffer for post-processing pipeline
	Draw::Framebuffer *gbaOffscreenFB_ = nullptr;
	GBAPostProcessor *gbaPostProcessor_ = nullptr;
};

}  // namespace EmuCore

