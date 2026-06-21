// [PPSSPP-FORK] MultiCore: GBA emulator core implementation
// Wraps mGBA library into EmuCore interface.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"

#ifdef PPSSPP_MULTICORE

#include <cstring>
#include <fcntl.h>

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/directories.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>

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

		// [PPSSPP-FORK] MultiCore: init sinc resampler (32768 -> 44100 Hz)
		resampleDest_ = reinterpret_cast<struct mAudioBuffer*>(new char[sizeof(struct mAudioBuffer)]);
		mAudioBufferInit(static_cast<struct mAudioBuffer*>(resampleDest_), AUDIO_BUF_SIZE, 2);
		resampler_ = reinterpret_cast<struct mAudioResampler*>(new char[sizeof(struct mAudioResampler)]);
		mAudioResamplerInit(static_cast<struct mAudioResampler*>(resampler_), mINTERPOLATOR_SINC);
		// [PPSSPP-FORK] MultiCore: upgrade sinc width from 8 (default) to 16 for better audio
		{
			auto *r = static_cast<struct mAudioResampler*>(resampler_);
			mInterpolatorSincDeinit(&r->sinc);
			mInterpolatorSincInit(&r->sinc, 8192, 16);
			r->lowWaterMark = r->sinc.width;
			r->highWaterMark = r->sinc.width;
		}
		mAudioResamplerSetDestination(static_cast<struct mAudioResampler*>(resampler_), static_cast<struct mAudioBuffer*>(resampleDest_), TARGET_RATE);

		INFO_LOG(Log::System, "[GBA] Core initialized with sinc resampler (width=16)");
	} else {
		ERROR_LOG(Log::System, "[GBA] Failed to create core");
	}
}

GBACore::~GBACore() {
	if (core_) {
		// [PPSSPP-FORK] MultiCore: flush and close save memory directories
		mDirectorySetDeinit(&core_->dirs);
		mAudioResamplerDeinit(static_cast<struct mAudioResampler*>(resampler_));
		delete[] static_cast<char*>(resampler_);
		resampler_ = nullptr;
		mAudioBufferDeinit(static_cast<struct mAudioBuffer*>(resampleDest_));
		delete[] static_cast<char*>(resampleDest_);
		resampleDest_ = nullptr;
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
	// Convert from mColor (BGR packed in uint32) to RGBA8888 (Thin3D format)
	// mGBA stores B in high bits, G middle, R low bits:
	//   M_RGB5_TO_BGR8 = (B5<<19) | (G5<<11) | (R5<<3)
	// Thin3D R8G8B8A8 expects little-endian: byte0=R, byte1=G, byte2=B, byte3=A
	for (int y = 0; y < GBA_HEIGHT; y++) {
		for (int x = 0; x < GBA_WIDTH; x++) {
			mColor c = rawVideoBuffer_[y * GBA_WIDTH + x];
			// Extract RGB from mGBA's BGR-packed uint32
			uint8_t r = c & 0xFF;
			uint8_t g = (c >> 8) & 0xFF;
			uint8_t b = (c >> 16) & 0xFF;
			// Pack as RGBA (not BGR!) — Thin3D expects byte0=R, byte1=G, byte2=B, byte3=A
			videoBuffer_[y * GBA_WIDTH + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
		}
	}

	// Debug: verify render is working
	static int vdebugCount = 0;
	if (++vdebugCount <= 5) {
		NOTICE_LOG(Log::System, "[GBA] Render frame %d: raw[0]=0x%08X converted[0]=0x%08X",
			vdebugCount, rawVideoBuffer_[0], videoBuffer_[0]);
	}

	// Resample audio from mGBA's internal buffer (32768 Hz) to PPSSPP rate (44100 Hz)
	// Uses mGBA's own sinc resampler for high quality anti-aliased conversion
	struct mAudioBuffer *src = core_->getAudioBuffer(core_);
	if (src) {
		size_t available = mAudioBufferAvailable(src);
		prevAvailable_ = available;

		if (available > 0) {
			// Query actual GBA sample rate from core
			if (core_->audioSampleRate) {
				coreSampleRate_ = core_->audioSampleRate(core_);
			}

			// Set resampler source: core's audio buffer at GBA rate, consume=true
			mAudioResamplerSetSource(static_cast<struct mAudioResampler*>(resampler_), src, (double)coreSampleRate_, true);

			// Process resampler: reads from core buffer, writes sinc-interpolated output to dest
			mAudioResamplerProcess(static_cast<struct mAudioResampler*>(resampler_));

			// Drain excess source audio, but PRESERVE lowWaterMark samples for resampler state
			// Resampler leaves timestamp at ~lowWaterMark (8), expects those source frames next frame
			// If we drain everything, timestamp goes out of sync -> audio artifacts
			size_t remaining = mAudioBufferAvailable(src);
			static constexpr size_t LOW_WATER = 8;
			if (remaining > LOW_WATER + 4) {  // only if more than lowWaterMark + margin
				size_t toDrain = remaining - LOW_WATER;
				int16_t drainBuf[64];
				while (toDrain > 0) {
					size_t chunk = toDrain > 64 ? 64 : toDrain;
					mAudioBufferRead(src, drainBuf, chunk);
					toDrain -= chunk;
				}
			}

			// Read resampled output (44100 Hz) into our buffer
			size_t outAvail = mAudioBufferAvailable(static_cast<struct mAudioBuffer*>(resampleDest_));
			if (outAvail > AUDIO_BUF_SIZE) outAvail = AUDIO_BUF_SIZE;

			size_t read = mAudioBufferRead(static_cast<struct mAudioBuffer*>(resampleDest_), audioBuffer_, outAvail);
			audioStereoPairs_ = read;

			// Debug: print resampler stats + warn on underrun
			static int audioDbg = 0;
			if (++audioDbg <= 3 || audioDbg % 300 == 0) {
				NOTICE_LOG(Log::System, "[GBA] Audio frame %d: coreAvail=%zu coreRate=%u outPairs=%zu/%d first=[%d,%d] last=[%d,%d]",
					audioDbg, available, coreSampleRate_, read, TARGET_PAIRS,
					audioBuffer_[0], audioBuffer_[1],
					read > 0 ? audioBuffer_[(read-1)*2] : 0,
					read > 0 ? audioBuffer_[(read-1)*2+1] : 0);
				if (read < TARGET_PAIRS) {
					WARN_LOG(Log::System, "[GBA] Audio underrun: got %zu pairs, expected %d", read, TARGET_PAIRS);
				}
			}
		}
	}
}

void GBACore::Reset() {
	if (core_) {
		INFO_LOG(Log::System, "[GBA] Core reset");
		core_->reset(core_);
	}
}

// ─── Vertex format matching Thin3D TEXTURE_COLOR_2D shader preset ───
struct GBAVertex {
	float x, y, z;
	float u, v;
	uint32_t rgba;
};

void GBACore::InitRendering(Draw::DrawContext *draw) {
	if (!draw || gbaPipeline_)
		return;

	NOTICE_LOG(Log::System, "[GBA] InitRendering START");
	using namespace Draw;

	// Sampler — nearest neighbor for pixel-perfect GBA graphics
	SamplerStateDesc nearestDesc{};
	nearestDesc.magFilter = TextureFilter::NEAREST;
	nearestDesc.minFilter = TextureFilter::NEAREST;
	gbaSampler_ = draw->CreateSamplerState(nearestDesc);

	// Shaders from presets (same ones used by UIContext)
	ShaderModule *vs = draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D);
	ShaderModule *fs = draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D);
	if (!vs || !fs) {
		ERROR_LOG(Log::G3D, "[GBA] Failed to get shader presets");
		return;
	}

	// Input layout
	InputLayoutDesc inputDesc = {
		sizeof(GBAVertex),
		{
			{ SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};
	InputLayout *inputLayout = draw->CreateInputLayout(inputDesc);

	BlendState *blend = draw->CreateBlendState({ true, 0xF, BlendFactor::ONE, BlendFactor::ONE_MINUS_SRC_ALPHA });
	DepthStencilState *depth = draw->CreateDepthStencilState({ false, false, Comparison::LESS });
	RasterState *raster = draw->CreateRasterState({});

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_LIST,
		{ vs, fs },
		inputLayout, depth, blend, raster, &vsTexColBufDesc,
	};
	gbaPipeline_ = draw->CreateGraphicsPipeline(pipelineDesc, "gba_video");

	// Release intermediate refs — pipeline holds its own
	if (inputLayout) inputLayout->Release();
	if (blend) blend->Release();
	if (depth) depth->Release();
	if (raster) raster->Release();

	// GBA framebuffer texture (240x160, RGBA8888)
	TextureDesc texDesc{};
	texDesc.type = TextureType::LINEAR2D;
	texDesc.format = DataFormat::R8G8B8A8_UNORM;
	texDesc.width = GBA_WIDTH;
	texDesc.height = GBA_HEIGHT;
	texDesc.depth = 1;
	texDesc.mipLevels = 1;
	texDesc.tag = "GBA_fb";
	static std::vector<uint8_t> dummyData(GBA_WIDTH * GBA_HEIGHT * 4, 0);
	texDesc.initData.push_back(dummyData.data());
	gbaTexture_ = draw->CreateTexture(texDesc);

	NOTICE_LOG(Log::System, "[GBA] InitRendering COMPLETE — pipeline=%p texture=%p sampler=%p",
		gbaPipeline_, gbaTexture_, gbaSampler_);
}

void GBACore::ShutdownRendering() {
	if (gbaPipeline_) { gbaPipeline_->Release(); gbaPipeline_ = nullptr; }
	if (gbaTexture_)  { gbaTexture_->Release();  gbaTexture_ = nullptr; }
	if (gbaSampler_)  { gbaSampler_->Release();  gbaSampler_ = nullptr; }
	NOTICE_LOG(Log::System, "[GBA] ShutdownRendering — all GPU resources released");
}

void GBACore::Render(Draw::DrawContext *draw) {
	if (!draw || !core_)
		return;

	// Lazy init rendering pipeline on first render call
	if (!gbaTexture_ || !gbaPipeline_) {
		InitRendering(draw);
		if (!gbaTexture_ || !gbaPipeline_)
			return;
	}

	// Upload GBA framebuffer to texture
	const uint8_t *data = reinterpret_cast<const uint8_t *>(videoBuffer_);
	draw->UpdateTextureLevels(gbaTexture_, &data, nullptr, 1);

	// Bind pipeline + sampler + texture
	draw->BindPipeline(gbaPipeline_);
	Draw::SamplerState *samplers[1] = { gbaSampler_ };
	draw->BindSamplerStates(0, 1, samplers);
	draw->BindTexture(0, gbaTexture_);

	// Calculate viewport with correct GBA aspect (240:160 = 3:2)
	int screenW = g_display.pixel_xres;
	int screenH = g_display.pixel_yres;
	float gbaAspect = (float)GBA_WIDTH / (float)GBA_HEIGHT;
	float screenAspect = (float)screenW / (float)screenH;

	float drawW, drawH, drawX, drawY;
	if (screenAspect > gbaAspect) {
		// Screen wider than GBA → letterbox left/right
		drawH = (float)screenH;
		drawW = drawH * gbaAspect;
		drawX = (screenW - drawW) / 2.0f;
		drawY = 0.0f;
	} else {
		// Screen taller than GBA → letterbox top/bottom
		drawW = (float)screenW;
		drawH = drawW / gbaAspect;
		drawX = 0.0f;
		drawY = (screenH - drawH) / 2.0f;
	}

	using namespace Draw;

	// Build a textured fullscreen quad (2 triangles = 6 verts, non-indexed)
	const float left = drawX;
	const float right = drawX + drawW;
	const float top = drawY;
	const float bottom = drawY + drawH;

	GBAVertex verts[6] = {
		{ left,  top,    0.0f, 0.0f, 0.0f, 0xFFFFFFFF },
		{ right, top,    0.0f, 1.0f, 0.0f, 0xFFFFFFFF },
		{ right, bottom, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF },
		{ left,  top,    0.0f, 0.0f, 0.0f, 0xFFFFFFFF },
		{ right, bottom, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF },
		{ left,  bottom, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF },
	};

	// Update uniform buffer (matching VsTexColUB layout)
	VsTexColUB ub{};
	Lin::Matrix4x4 ortho = ComputeOrthoMatrix((float)screenW, (float)screenH, draw->GetDeviceCaps().coordConvention);
	memcpy(ub.WorldViewProj, ortho.getReadPtr(), sizeof(Lin::Matrix4x4));
	ub.tint = 1.0f;
	ub.saturation = 1.0f;
	draw->UpdateDynamicUniformBuffer(&ub, sizeof(ub));

	draw->DrawUP(verts, 6);
}

void GBACore::DeviceLost() {
	ShutdownRendering();
}

void GBACore::DeviceRestored(Draw::DrawContext *draw) {
	// Rendering resources will be lazily recreated on next Render() call.
	NOTICE_LOG(Log::System, "[GBA] DeviceRestored — lazy reinit on next render");
}

int GBACore::GetAudioSampleRate() const {
	return 44100;
}

void GBACore::GetAudioSamples(int16_t *buffer, size_t *samples) {
	// Audio is now converted via GetMixedAudio — this stub maintains vtable compat
	*samples = 0;
}

void GBACore::ClearAudio() {
	// Clear all pending audio including resampler dest to prevent stale sample
	// carryover after fast-forward frame skip.
	struct mAudioBuffer *src = core_->getAudioBuffer(core_);
	if (src) {
		mAudioBufferClear(src);
	}
	mAudioBufferClear(static_cast<struct mAudioBuffer*>(resampleDest_));
	audioStereoPairs_ = 0;
}

const int16_t *GBACore::GetRawAudio(size_t *stereoPairs) {
	// Get the raw resampled int16 audio buffer with DC filter applied.
	// Caller should use this for direct SDL output (int16 format).
	if (audioStereoPairs_ == 0) {
		*stereoPairs = 0;
		return nullptr;
	}

	size_t pairs = audioStereoPairs_ > AUDIO_BUF_SIZE ? AUDIO_BUF_SIZE : audioStereoPairs_;
	for (size_t i = 0; i < pairs; i++) {
		float left = (float)audioBuffer_[i * 2];
		float right = (float)audioBuffer_[i * 2 + 1];

		// DC blocking filter (SkyEmu-inspired)
		float outL = left - dcCapL_;
		float outR = right - dcCapR_;
		dcCapL_ = (left - outL) * 0.996f;
		dcCapR_ = (right - outR) * 0.996f;

		if (outL > 32767.0f) outL = 32767.0f;
		if (outL < -32768.0f) outL = -32768.0f;
		if (outR > 32767.0f) outR = 32767.0f;
		if (outR < -32768.0f) outR = -32768.0f;

		audioBuffer_[i * 2]     = (int16_t)outL;
		audioBuffer_[i * 2 + 1] = (int16_t)outR;
	}

	*stereoPairs = pairs;
	audioStereoPairs_ = 0;
	return audioBuffer_;
}

void GBACore::GetMixedAudio(int32_t *buffer, size_t *stereoPairs) {
	if (!buffer || !stereoPairs) return;

	*stereoPairs = TARGET_PAIRS;

	size_t pairs = audioStereoPairs_;
	if (pairs > AUDIO_BUF_SIZE) pairs = AUDIO_BUF_SIZE;

	if (pairs == 0) {
		memset(buffer, 0, TARGET_PAIRS * 2 * sizeof(int32_t));
		return;
	}

	// Audio already sinc-resampled to 44100 Hz by mGBA resampler
	// Just apply DC blocking filter + convert int16 -> int32
	size_t toCopy = (pairs < (size_t)TARGET_PAIRS) ? pairs : (size_t)TARGET_PAIRS;
	size_t i;
	for (i = 0; i < toCopy; i++) {
		float left = (float)audioBuffer_[i * 2];
		float right = (float)audioBuffer_[i * 2 + 1];

		// DC blocking filter — EMA tracker for SOUNDBIAS residual DC
		// Tracks actual DC offset instead of self-decaying capacitor
		dcCapL_ += (left - dcCapL_) * 0.004f;
		dcCapR_ += (right - dcCapR_) * 0.004f;

		// Safety clamp (reset if capacitor drifted beyond ±2.0)
		if (!(dcCapL_ < 2.0f && dcCapL_ > -2.0f)) dcCapL_ = 0.0f;
		if (!(dcCapR_ < 2.0f && dcCapR_ > -2.0f)) dcCapR_ = 0.0f;

		float outL = left - dcCapL_;
		float outR = right - dcCapR_;

		if (outL > 32767.0f) outL = 32767.0f;
		if (outL < -32768.0f) outL = -32768.0f;
		if (outR > 32767.0f) outR = 32767.0f;
		if (outR < -32768.0f) outR = -32768.0f;

		buffer[i * 2]     = (int32_t)((int16_t)outL);
		buffer[i * 2 + 1] = (int32_t)((int16_t)outR);
	}

	// Pad remaining slots with zero (no DC step — avoids harsh/tinny artifacts)
	if (toCopy < (size_t)TARGET_PAIRS) {
		for (i = toCopy; i < (size_t)TARGET_PAIRS; i++) {
			buffer[i * 2] = 0;
			buffer[i * 2 + 1] = 0;
		}
	}

	// Debug: log resampler output stats
	static int audioMixFrame = 0;
	if (++audioMixFrame <= 3 || audioMixFrame % 300 == 0) {
		NOTICE_LOG(Log::System, "[GBA] Audio mix frame %d: pairs=%zu firstOut=[%d,%d] lastOut=[%d,%d]",
			audioMixFrame, pairs,
			buffer[0] >> 16, buffer[1] >> 16,
			buffer[(TARGET_PAIRS - 1) * 2] >> 16, buffer[(TARGET_PAIRS - 1) * 2 + 1] >> 16);
	}

	// Mark as consumed
	audioStereoPairs_ = 0;
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

std::string GBACore::GetSavePrefix() const {
	std::string title, id;
	GetGameInfo(const_cast<std::string &>(title), const_cast<std::string &>(id));
	return GetSavePrefix(title, id);
}

std::string GBACore::GetSavePrefix(const std::string &title_in, const std::string &id) {
	std::string title = title_in;
	// Sanitize: keep only alphanumeric + underscore, max 32 chars
	std::string prefix;
	for (char c : title) {
		if (isalnum((unsigned char)c) || c == '_' || c == '-') {
			prefix += c;
		} else if (!prefix.empty() && prefix.back() != '_') {
			prefix += '_';
		}
		if (prefix.size() >= 32)
			break;
	}
	if (prefix.empty())
		prefix = "GBA_ROM";

	// Use game code if available for dedup
	if (!id.empty()) {
		std::string cleanId;
		for (char c : id) {
			if (isalnum((unsigned char)c))
				cleanId += c;
		}
		if (!cleanId.empty())
			prefix = cleanId + "_" + prefix;
	}
	return prefix;
}

bool GBACore::SaveStateToFile(int slot) {
	if (!core_) return false;

	std::string prefix = GetSavePrefix();
	Path dir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::string filename = StringFromFormat("GBA_%s_%d.ppst", prefix.c_str(), slot);
	Path path = dir / filename;

	File::CreateFullPath(path);

	size_t size = GetStateSize();
	if (size == 0) {
		WARN_LOG(Log::SaveState, "[GBA] Save state failed: size is 0");
		return false;
	}

	std::vector<u8> buffer(size);
	if (!SaveState(buffer.data())) {
		WARN_LOG(Log::SaveState, "[GBA] SaveState() returned false");
		return false;
	}

	bool ok = File::WriteDataToFile(false, buffer.data(), size, path);
	if (ok) {
		INFO_LOG(Log::SaveState, "[GBA] State saved: %s (slot %d, %zu bytes)", path.c_str(), slot + 1, size);
	} else {
		WARN_LOG(Log::SaveState, "[GBA] Failed to write file: %s", path.c_str());
	}
	return ok;
}

bool GBACore::LoadStateFromFile(int slot) {
	if (!core_) return false;

	std::string prefix = GetSavePrefix();
	Path dir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::string filename = StringFromFormat("GBA_%s_%d.ppst", prefix.c_str(), slot);
	Path path = dir / filename;

	std::string data;
	if (!File::ReadBinaryFileToString(path, &data)) {
		WARN_LOG(Log::SaveState, "[GBA] No save state file: %s", path.c_str());
		return false;
	}

	bool ok = LoadState(data.data());
	if (ok) {
		INFO_LOG(Log::SaveState, "[GBA] State loaded: %s (slot %d, %zu bytes)", path.c_str(), slot + 1, data.size());
	} else {
		WARN_LOG(Log::SaveState, "[GBA] LoadState() returned false");
	}
	return ok;
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
