// [PPSSPP-FORK] MultiCore: GBA emulator core implementation
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"

#ifdef PPSSPP_MULTICORE

#include <cstdio>
#include <cstring>
#include <fcntl.h>

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

namespace EmuCore {

GBACore::GBACore() {
	fprintf(stderr, "DEBUG: GBACoreCreate...\n");
	core_ = GBACoreCreate();
	fprintf(stderr, "DEBUG: core_=%p\n", (void*)core_);
	if (core_) {
		fprintf(stderr, "DEBUG: core_->init=%p\n", (void*)core_->init);
		fprintf(stderr, "DEBUG: core_->deinit=%p\n", (void*)core_->deinit);
		fprintf(stderr, "DEBUG: calling core_->init...\n");
		fflush(stderr);
		core_->init(core_);
		fprintf(stderr, "DEBUG: init OK\n");
		fprintf(stderr, "DEBUG: calling setAudioBufferSize...\n");
		fflush(stderr);
		core_->setAudioBufferSize(core_, AUDIO_BUF_SIZE);
		fprintf(stderr, "DEBUG: setAudioBufferSize OK\n");
		fflush(stderr);
		core_->setAVStream(core_, nullptr);
		fprintf(stderr, "DEBUG: ctor done\n");
		fflush(stderr);
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
		core_->setKeys(core_, keys);
	}
}

uint32_t GBACore::GetKeys() const {
	if (core_) {
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
