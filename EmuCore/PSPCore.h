// [PPSSPP-FORK] MultiCore: PSP core wrapper
// Minimal wrapper that delegates to existing PPSSPP PSP emulation system.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "EmuCore/EmuCore.h"

namespace EmuCore {

class PSPCore : public Core {
public:
	PSPCore() = default;

	Type GetType() const override { return Type::PSP; }

	bool LoadROM(const Path &path) override;
	void RunFrame() override;
	void Reset() override;
	void Render(Draw::DrawContext *draw) override;

	int GetAudioSampleRate() const override;
	void GetAudioSamples(int16_t *buffer, size_t *samples) override;

	void SetKeys(uint32_t keys) override;
	uint32_t GetKeys() const override;

	size_t GetStateSize() const override;
	bool SaveState(void *buffer) override;
	bool LoadState(const void *buffer) override;

	void GetGameInfo(std::string &title, std::string &id) const override;
};

}  // namespace EmuCore
