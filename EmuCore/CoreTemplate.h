// [PPSSPP-FORK] <CoreName>: <brief description>
// <Longer description of what this core does>
//
// Template for adding new emulator cores.
// Copy this file and rename to <CoreName>Core.h, then fill in the blanks.
//
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"

namespace EmuCore {

class <CoreName>Core : public Core {
public:
	<CoreName>Core();
	~<CoreName>Core() override;

	// ── Type ────────────────────────────────────────────────────────
	Type GetType() const override { return Type::<CORENAME>; }

	// ── Lifecycle ────────────────────────────────────────────────────
	bool LoadROM(const Path &path) override;
	void RunFrame() override;
	void Reset() override;

	// ── Rendering ────────────────────────────────────────────────────
	void Render(Draw::DrawContext *draw) override;
	void DeviceLost() override;
	void DeviceRestored(Draw::DrawContext *draw) override;

	// ── Audio ────────────────────────────────────────────────────────
	int GetAudioSampleRate() const override;
	void GetAudioSamples(int16_t *buffer, size_t *samples) override;

	// ── Input ────────────────────────────────────────────────────────
	void SetKeys(uint32_t keys) override;
	uint32_t GetKeys() const override;

	// ── Savestate ────────────────────────────────────────────────────
	size_t GetStateSize() const override;
	bool SaveState(void *buffer) override;
	bool LoadState(const void *buffer) override;

	// ── Game Info ────────────────────────────────────────────────────
	void GetGameInfo(std::string &title, std::string &id) const override;

	// ── <CoreName>-specific methods ──────────────────────────────────
	// void GetMixedAudio(int32_t *buffer, size_t *stereoPairs);
	// bool SaveStateToFile(int slot);
	// bool LoadStateFromFile(int slot);
	// void ClearAudio();

private:
	// ── Video state ──────────────────────────────────────────────────
	// Draw::Pipeline *pipeline_ = nullptr;
	// Draw::Texture *texture_ = nullptr;
	// Draw::SamplerState *sampler_ = nullptr;

	// ── Audio state ──────────────────────────────────────────────────
	// int16_t audioBuffer_[AUDIO_BUF_SIZE * 2]{};
	// size_t audioStereoPairs_ = 0;

	// ── Input state ──────────────────────────────────────────────────
	// uint32_t keys_ = 0;

	// ── Core library handle ──────────────────────────────────────────
	// void *coreHandle_ = nullptr;
};

}  // namespace EmuCore
