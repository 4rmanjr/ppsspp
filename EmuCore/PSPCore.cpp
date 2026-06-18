// [PPSSPP-FORK] MultiCore: PSP core wrapper
// Delegates all calls to existing PPSSPP emulator system.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/PSPCore.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/SaveState.h"

namespace EmuCore {

bool PSPCore::LoadROM(const Path &path) {
	// PSP ROM loading is handled by the existing PSP_LoadStartupFile/EmuScreen boot process.
	return true;
}

void PSPCore::RunFrame() {
	// PSP frame advancement handled by existing PSP_UpdateLoop() in EmuScreen.
}

void PSPCore::Reset() {
	bool unused = false;
	PSP_Shutdown(unused);
}

void PSPCore::Render() {
	// PSP rendering is done by GPU backends.
}

int PSPCore::GetAudioSampleRate() const {
	return 44100;
}

void PSPCore::GetAudioSamples(int16_t *buffer, size_t *samples) {
	// Audio is handled by existing PSP audio system.
	*samples = 0;
}

void PSPCore::SetKeys(uint32_t keys) {
	// PSP input is managed by the existing input system — no-op here.
}

uint32_t PSPCore::GetKeys() const {
	return __CtrlPeekButtons();
}

size_t PSPCore::GetStateSize() const {
	return 0;
}

bool PSPCore::SaveState(void *buffer) {
	return false;
}

bool PSPCore::LoadState(const void *buffer) {
	return false;
}

void PSPCore::GetGameInfo(std::string &title, std::string &id) const {
	title = "";
	id = "";
}

}  // namespace EmuCore
