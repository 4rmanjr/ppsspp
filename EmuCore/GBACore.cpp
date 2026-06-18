// [PPSSPP-FORK] MultiCore: GBA emulator core implementation
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"

#ifdef PPSSPP_MULTICORE

#include <cstring>
#include <fcntl.h>

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

namespace EmuCore {

// PSP CTRL bitmask values (from Core/HLE/sceCtrl.h)
static constexpr uint32_t PSP_CTRL_CROSS     = 0x4000;
static constexpr uint32_t PSP_CTRL_CIRCLE    = 0x2000;
static constexpr uint32_t PSP_CTRL_SQUARE    = 0x8000;
static constexpr uint32_t PSP_CTRL_TRIANGLE  = 0x1000;
static constexpr uint32_t PSP_CTRL_START     = 0x0008;
static constexpr uint32_t PSP_CTRL_SELECT    = 0x0001;
static constexpr uint32_t PSP_CTRL_UP        = 0x0010;
static constexpr uint32_t PSP_CTRL_DOWN      = 0x0040;
static constexpr uint32_t PSP_CTRL_LEFT      = 0x0080;
static constexpr uint32_t PSP_CTRL_RIGHT     = 0x0020;
static constexpr uint32_t PSP_CTRL_LTRIGGER  = 0x0100;
static constexpr uint32_t PSP_CTRL_RTRIGGER  = 0x0200;

// GBA key bit indices (from mgba/internal/gba/input.h)
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

uint32_t GBACore::PSPSKeysToGBA(uint32_t pspKeys) {
	uint32_t gbaKeys = 0;
	if (pspKeys & PSP_CTRL_CROSS)    gbaKeys |= GBA_BIT_A;
	if (pspKeys & PSP_CTRL_CIRCLE)   gbaKeys |= GBA_BIT_B;
	if (pspKeys & PSP_CTRL_TRIANGLE) gbaKeys |= GBA_BIT_A;   // alt: Triangle → A
	if (pspKeys & PSP_CTRL_SQUARE)   gbaKeys |= GBA_BIT_B;   // alt: Square → B
	if (pspKeys & PSP_CTRL_START)    gbaKeys |= GBA_BIT_START;
	if (pspKeys & PSP_CTRL_SELECT)   gbaKeys |= GBA_BIT_SELECT;
	if (pspKeys & PSP_CTRL_UP)       gbaKeys |= GBA_BIT_UP;
	if (pspKeys & PSP_CTRL_DOWN)     gbaKeys |= GBA_BIT_DOWN;
	if (pspKeys & PSP_CTRL_LEFT)     gbaKeys |= GBA_BIT_LEFT;
	if (pspKeys & PSP_CTRL_RIGHT)    gbaKeys |= GBA_BIT_RIGHT;
	if (pspKeys & PSP_CTRL_LTRIGGER) gbaKeys |= GBA_BIT_L;
	if (pspKeys & PSP_CTRL_RTRIGGER) gbaKeys |= GBA_BIT_R;
	return gbaKeys;
}

GBACore::GBACore() {
	core_ = GBACoreCreate();
	if (core_) {
		core_->init(core_);
		core_->setAudioBufferSize(core_, AUDIO_BUF_SIZE);
		core_->setAVStream(core_, nullptr);
	}
}

GBACore::~GBACore() {
	if (core_) {
		core_->deinit(core_);
		core_ = nullptr;
	}
}

bool GBACore::LoadROM(const Path &path) {
	return LoadROMInternal(path);
}

bool GBACore::LoadROMInternal(const Path &path) {
	if (!core_)
		return false;

	// Open ROM file via mGBA's VFile
	struct VFile *vf = VFileOpen(path.c_str(), O_RDONLY);
	if (!vf)
		return false;

	if (!core_->loadROM(core_, vf)) {
		vf->close(vf);
		return false;
	}

	core_->reset(core_);
	return true;
}

void GBACore::RunFrame() {
	if (!core_)
		return;

	core_->runFrame(core_);

	// Capture video
	const void *pixels = nullptr;
	size_t stride = 0;
	core_->getPixels(core_, &pixels, &stride);

	if (pixels) {
		// mGBA outputs mColor (uint32_t, format XBGR8 on desktop).
		// Convert to RGBA8888 for PPSSPP rendering.
		const mColor *src = static_cast<const mColor *>(pixels);
		size_t srcStride = stride / sizeof(mColor);

		for (int y = 0; y < GBA_HEIGHT && y < (int)srcStride; y++) {
			for (int x = 0; x < GBA_WIDTH; x++) {
				mColor c = src[y * srcStride + x];
				// XBGR8 → RGBA8888
				uint8_t r = c & 0xFF;
				uint8_t g = (c >> 8) & 0xFF;
				uint8_t b = (c >> 16) & 0xFF;
				videoBuffer_[y * GBA_WIDTH + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
			}
		}
	}

	// Capture audio from mGBA's internal audio buffer
	struct mAudioBuffer *audio = core_->getAudioBuffer(core_);
	if (audio) {
		size_t available = mAudioBufferAvailable(audio);
		if (available > 0) {
			// Limit to our buffer (in stereo frames)
			size_t toRead = (available < AUDIO_BUF_SIZE) ? available : AUDIO_BUF_SIZE;

			// mAudioBufferRead returns stereo frames read
			size_t read = mAudioBufferRead(audio, audioBuffer_, toRead);
			// Store as bytes for GetAudioSamples
			audioAvailable_ = read * 2 * sizeof(int16_t);
		}
	}
}

void GBACore::Reset() {
	if (core_) {
		core_->reset(core_);
	}
}

void GBACore::Render() {
	// Rendering is handled externally by EmuScreen which uploads videoBuffer_ as a texture.
}

int GBACore::GetAudioSampleRate() const {
	return 44100;
}

void GBACore::GetAudioSamples(int16_t *buffer, size_t *samples) {
	size_t toCopy = (audioAvailable_ < *samples) ? audioAvailable_ : *samples;
	memcpy(buffer, audioBuffer_, toCopy);
	*samples = toCopy;
	audioAvailable_ = 0;
}

void GBACore::SetKeys(uint32_t keys) {
	if (core_) {
		// Convert PSP key bitmask to GBA key bitmask transparently
		uint32_t gbaKeys = PSPSKeysToGBA(keys);
		core_->setKeys(core_, gbaKeys);
	}
}

uint32_t GBACore::GetKeys() const {
	if (core_) {
		// Return raw GBA keys (no reverse conversion needed)
		return core_->getKeys(core_);
	}
	return 0;
}

size_t GBACore::GetStateSize() const {
	if (core_) {
		return core_->stateSize(core_);
	}
	return 0;
}

bool GBACore::SaveState(void *buffer) {
	if (core_) {
		return core_->saveState(core_, buffer);
	}
	return false;
}

bool GBACore::LoadState(const void *buffer) {
	if (core_) {
		return core_->loadState(core_, buffer);
	}
	return false;
}

void GBACore::GetGameInfo(std::string &title, std::string &id) const {
	if (!core_) {
		title = "Unknown";
		id = "";
		return;
	}
	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	core_->getGameInfo(core_, &info);
	title = info.title;
	id = info.code;
}

}  // namespace EmuCore

#endif  // PPSSPP_MULTICORE
