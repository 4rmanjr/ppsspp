// [PPSSPP-FORK] MultiCore: GBA emulator core implementation
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"

#ifdef PPSSPP_MULTICORE

#include <cstring>
#include <fcntl.h>

#include "Common/Log.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/directories.h>
#include <mgba/core/config.h>
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
		// [PPSSPP-FORK] MultiCore: init mGBA config subsystem (required before reset/loadROM)
		mCoreConfigInit(&core_->config, "gba");

		core_->init(core_);
		core_->setAudioBufferSize(core_, AUDIO_BUF_SIZE);
		core_->setAVStream(core_, nullptr);
		// [PPSSPP-FORK] MultiCore: set video buffer so mGBA renders into our buffer
		core_->setVideoBuffer(core_, (mColor *)rawVideoBuffer_, GBA_WIDTH);
		// [PPSSPP-FORK] MultiCore: init directory set for save memory
		mDirectorySetInit(&core_->dirs);
		INFO_LOG(Log::System, "[GBA] Core initialized");
	} else {
		ERROR_LOG(Log::System, "[GBA] Failed to create core");
	}
}

GBACore::~GBACore() {
	if (core_) {
		// [PPSSPP-FORK] MultiCore: flush and close save memory directories
		mDirectorySetDeinit(&core_->dirs);
		core_->deinit(core_);
		INFO_LOG(Log::System, "[GBA] Core deinitialized, save flushed");
		core_ = nullptr;
	}
}

bool GBACore::LoadROM(const Path &path) {
	return LoadROMInternal(path);
}

bool GBACore::LoadROMInternal(const Path &path) {
	if (!core_)
		return false;

	INFO_LOG(Log::System, "[GBA] Loading ROM: %s", path.c_str());

	// Open ROM file via mGBA's VFile
	struct VFile *vf = VFileOpen(path.c_str(), O_RDONLY);
	if (!vf) {
		ERROR_LOG(Log::System, "[GBA] Failed to open ROM file: %s", path.c_str());
		return false;
	}

	if (!core_->loadROM(core_, vf)) {
		ERROR_LOG(Log::System, "[GBA] mGBA rejected ROM");
		vf->close(vf);
		return false;
	}

	core_->reset(core_);

	// Log game info
	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	core_->getGameInfo(core_, &info);
	INFO_LOG(Log::System, "[GBA] ROM loaded — title: '%s', code: '%s'",
		info.title[0] ? info.title : "(unknown)",
		info.code[0] ? info.code : "(none)");

	// [PPSSPP-FORK] MultiCore: auto-load save memory (.sav) if directory configured
	if (!saveDir_.empty()) {
		INFO_LOG(Log::System, "[GBA] Save directory: %s", saveDir_.c_str());

		struct VDir *saveDir = VDirOpen(saveDir_.c_str());
		if (saveDir) {
			core_->dirs.save = saveDir;
			strncpy(core_->dirs.baseName, info.title[0] ? info.title : "GBA_ROM", sizeof(core_->dirs.baseName) - 1);
			core_->dirs.baseName[sizeof(core_->dirs.baseName) - 1] = '\0';

			// Auto-load existing .sav file (mGBA also creates one if it doesn't exist)
			bool hasSave = mCoreAutoloadSave(core_);
			INFO_LOG(Log::System, "[GBA] Save memory autoload: %s", hasSave ? "found & loaded" : "no existing save");
		} else {
			ERROR_LOG(Log::System, "[GBA] Failed to open save directory: %s", saveDir_.c_str());
		}
	}

	return true;
}

void GBACore::SetSaveDirectory(const std::string &dir) {
	saveDir_ = dir;
	INFO_LOG(Log::System, "[GBA] Save directory set: %s", dir.c_str());
}

void GBACore::CloseSaveMemory() {
	if (core_ && core_->dirs.save) {
		core_->dirs.save->close(core_->dirs.save);
		core_->dirs.save = nullptr;
	}
}

void GBACore::RunFrame() {
	if (!core_)
		return;

	core_->runFrame(core_);

	// Periodic frame counter log (every 3600 frames = ~1 minute)
	static int frameCount = 0;
	if (++frameCount % 3600 == 0) {
		INFO_LOG(Log::System, "[GBA] Frame %d", frameCount);
	}

	// Capture video — mGBA rendered into rawVideoBuffer_ via setVideoBuffer
	// Convert from mColor (XBGR8) to RGBA8888
	for (int y = 0; y < GBA_HEIGHT; y++) {
		for (int x = 0; x < GBA_WIDTH; x++) {
			mColor c = rawVideoBuffer_[y * GBA_WIDTH + x];
			// XBGR8 → RGBA8888
			uint8_t r = c & 0xFF;
			uint8_t g = (c >> 8) & 0xFF;
			uint8_t b = (c >> 16) & 0xFF;
			videoBuffer_[y * GBA_WIDTH + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
		}
	}

	// Debug: verify render is working
	static int vdebugCount = 0;
	if (++vdebugCount <= 5) {
		NOTICE_LOG(Log::System, "[GBA] Render frame %d: raw[0]=0x%08X converted[0]=0x%08X",
			vdebugCount, rawVideoBuffer_[0], videoBuffer_[0]);
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
		INFO_LOG(Log::System, "[GBA] Core reset");
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
