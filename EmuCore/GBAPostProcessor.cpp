// [PPSSPP-FORK] MultiCore: GBA post-processing pipeline implementation

#include "EmuCore/GBAPostProcessor.h"

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderTranslation.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/HW/Display.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GPUState.h"

GBAPostProcessor::GBAPostProcessor(Draw::DrawContext *draw)
	: draw_(draw) {
}

GBAPostProcessor::~GBAPostProcessor() {
	Shutdown();
}

void GBAPostProcessor::Init(int srcWidth, int srcHeight) {
	if (initialized_)
		return;

	srcWidth_ = srcWidth;
	srcHeight_ = srcHeight;

	// Create vertex buffer (4 verts for TRIANGLE_STRIP fullscreen quad)
	using namespace Draw;
	vdata_ = draw_->CreateBuffer(sizeof(QuadVertex) * 4, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);

	// Create cached samplers for post-shader passes
	samplerNearest_ = draw_->CreateSamplerState({ TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST, 0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE });
	samplerLinear_ = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR, 0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE });

	initialized_ = true;
}

void GBAPostProcessor::Shutdown() {
	DestroyShaders();
	if (vdata_) {
		vdata_->Release();
		vdata_ = nullptr;
	}
	if (samplerNearest_) {
		samplerNearest_->Release();
		samplerNearest_ = nullptr;
	}
	if (samplerLinear_) {
		samplerLinear_->Release();
		samplerLinear_ = nullptr;
	}
	initialized_ = false;
}

void GBAPostProcessor::DeviceLost() {
	Shutdown();
}

void GBAPostProcessor::DeviceRestored(Draw::DrawContext *draw) {
	draw_ = draw;
	Init(srcWidth_, srcHeight_);
}

bool GBAPostProcessor::UpdatePostShader(const std::vector<std::string> &shaderNames, int screenWidth, int screenHeight) {
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	// Cache: skip recompilation if shader names haven't changed
	if (shaderNames == currentShaderNames_) {
		// Update screen dimensions anyway (may have rotated)
		return usePostShader_;
	}

	// Destroy old shaders
	DestroyShaders();

	if (shaderNames.empty()) {
		usePostShader_ = false;
		currentShaderNames_.clear();
		return false;
	}

	// Load all shader info from the same system PSP uses
	ReloadAllPostShaderInfo(draw_);
	std::vector<const ShaderInfo *> chain = GetFullPostShadersChain(shaderNames);
	if (chain.empty()) {
		usePostShader_ = false;
		currentShaderNames_.clear();
		return false;
	}

	if (!CompileShaders(chain)) {
		usePostShader_ = false;
		currentShaderNames_.clear();
		return false;
	}

	usePostShader_ = true;
	currentShaderNames_ = shaderNames;
	return true;
}

void GBAPostProcessor::DestroyShaders() {
	usePostShader_ = false;

	for (auto *pipeline : shaderPipelines_) {
		if (pipeline)
			pipeline->Release();
	}
	shaderPipelines_.clear();
	shaderInfo_.clear();

	for (auto *fb : tempFramebuffers_) {
		if (fb)
			fb->Release();
	}
	tempFramebuffers_.clear();
}

bool GBAPostProcessor::CompileShaders(const std::vector<const ShaderInfo *> &chain) {
	using namespace Draw;

	ShaderLanguage lang = draw_->GetShaderLanguageDesc().shaderLanguage;

	for (size_t i = 0; i < chain.size(); i++) {
		const ShaderInfo *info = chain[i];
		if (!info)
			continue;

		// Read vertex and fragment shader sources
		std::string vsSrc;
		{
			size_t sz = 0;
			char *data = (char *)g_VFS.ReadFile(info->vertexShaderFile.c_str(), &sz);
			if (data) {
				vsSrc = std::string(data, sz);
				delete[] data;
			}
		}
		std::string fsSrc;
		{
			size_t sz = 0;
			char *data = (char *)g_VFS.ReadFile(info->fragmentShaderFile.c_str(), &sz);
			if (data) {
				fsSrc = std::string(data, sz);
				delete[] data;
			}
		}

		if (vsSrc.empty() || fsSrc.empty()) {
			ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: Failed to read shader files for %s", info->section.c_str());
			return false;
		}

		// Translate shaders if needed (post-shaders are GLSL 1.0)
		std::string vsTranslated = vsSrc;
		std::string fsTranslated = fsSrc;
		std::string vsError, fsError;

		if (lang != GLSL_1xx) {
			if (!TranslateShader(&vsTranslated, lang, draw_->GetShaderLanguageDesc(), nullptr,
					vsSrc, GLSL_1xx, ShaderStage::Vertex, &vsError)) {
				ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: VS translate error: %s", vsError.c_str());
				return false;
			}
			if (!TranslateShader(&fsTranslated, lang, draw_->GetShaderLanguageDesc(), nullptr,
					fsSrc, GLSL_1xx, ShaderStage::Fragment, &fsError)) {
				ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: FS translate error: %s", fsError.c_str());
				return false;
			}
		}

		// Create shader modules
		ShaderModule *vs = draw_->CreateShaderModule(ShaderStage::Vertex, lang,
			(const uint8_t *)vsTranslated.c_str(), vsTranslated.size(), "gba_post_vs");
		ShaderModule *fs = draw_->CreateShaderModule(ShaderStage::Fragment, lang,
			(const uint8_t *)fsTranslated.c_str(), fsTranslated.size(), "gba_post_fs");

		if (!vs || !fs) {
			if (vs) vs->Release();
			if (fs) fs->Release();
			ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: Shader module creation failed for %s", info->section.c_str());
			return false;
		}

		// Build input layout matching QuadVertex
		InputLayoutDesc inputDesc = {
			sizeof(QuadVertex),
			{
				{ SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
				{ SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
				{ SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
			},
		};

		// Post-shader uniform descriptor
		UniformBufferDesc postShaderDesc{ sizeof(PostShaderUniforms), {
			{ "gl_HalfPixel", 0, -1, UniformType::FLOAT4, offsetof(PostShaderUniforms, gl_HalfPixel) },
			{ "u_texelDelta", 1, 1, UniformType::FLOAT2, offsetof(PostShaderUniforms, texelDelta) },
			{ "u_pixelDelta", 2, 2, UniformType::FLOAT2, offsetof(PostShaderUniforms, pixelDelta) },
			{ "u_time", 3, 3, UniformType::FLOAT4, offsetof(PostShaderUniforms, time) },
			{ "u_timeDelta", 4, 4, UniformType::FLOAT4, offsetof(PostShaderUniforms, timeDelta) },
			{ "u_setting", 5, 5, UniformType::FLOAT4, offsetof(PostShaderUniforms, setting) },
			{ "u_video", 6, 6, UniformType::FLOAT1, offsetof(PostShaderUniforms, video) },
			{ "u_vr", 7, 7, UniformType::FLOAT1, offsetof(PostShaderUniforms, vr) },
		} };

		InputLayout *inputLayout = draw_->CreateInputLayout(inputDesc);
		DepthStencilState *depth = draw_->CreateDepthStencilState({ false, false, Comparison::LESS });
		BlendState *blend = draw_->CreateBlendState({ false, 0xF });
		RasterState *raster = draw_->CreateRasterState({});

		PipelineDesc pipelineDesc{
			Primitive::TRIANGLE_STRIP,
			{ vs, fs },
			inputLayout, depth, blend, raster, &postShaderDesc,
		};
		Pipeline *pipeline = draw_->CreateGraphicsPipeline(pipelineDesc, "gba_post");

		// Release intermediate refs
		if (inputLayout) inputLayout->Release();
		if (depth) depth->Release();
		if (blend) blend->Release();
		if (raster) raster->Release();
		vs->Release();
		fs->Release();

		if (!pipeline) {
			ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: Pipeline creation failed for %s", info->section.c_str());
			return false;
		}

		shaderPipelines_.push_back(pipeline);
		shaderInfo_.push_back(*info);

		// Allocate temp framebuffer for this pass (GBA always needs an output FB)
		int fbW = info->outputResolution ? screenWidth_ : srcWidth_;
		int fbH = info->outputResolution ? screenHeight_ : srcHeight_;
		// For chained shaders, use previous FB's size (unless outputResolution overrides)
		if (!tempFramebuffers_.empty() && !info->outputResolution) {
			Draw::Framebuffer *prev = tempFramebuffers_.back();
			draw_->GetFramebufferDimensions(prev, &fbW, &fbH);
		}

		Draw::Framebuffer *fb = draw_->CreateFramebuffer({ fbW, fbH, 1, 1, 0, false, "gba_post_temp" });
		if (!fb) {
			ERROR_LOG(Log::FrameBuf, "GBAPostProcessor: Failed to allocate temp FB for %s", info->section.c_str());
			return false;
		}
		tempFramebuffers_.push_back(fb);
	}

	return true;
}

Draw::Framebuffer *GBAPostProcessor::Process(Draw::Framebuffer *input) {
	if (!initialized_ || !usePostShader_ || shaderPipelines_.empty())
		return input;

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	// Upload fullscreen quad vertices (used by every pass)
	QuadVertex verts[4] = {};
	verts[0] = { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF };
	verts[1] = {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0xFFFFFFFF };
	verts[2] = { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF };
	verts[3] = {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF };

	draw_->UpdateBuffer(vdata_, (const uint8_t *)verts, 0, sizeof(verts), Draw::UPDATE_DISCARD);

	int lastWidth = srcWidth_;
	int lastHeight = srcHeight_;
	Draw::Framebuffer *currentInput = input;

	_assert_msg_(shaderPipelines_.size() <= tempFramebuffers_.size(), "GBAPostProcessor: tempFB count < pipeline count");

	for (size_t i = 0; i < shaderPipelines_.size(); i++) {
		Draw::Pipeline *pipeline = shaderPipelines_[i];
		const ShaderInfo &info = shaderInfo_[i];

		// Output FB always exists (allocated in CompileShaders for every pass)
		Draw::Framebuffer *outputFB = tempFramebuffers_[i];
		int outW, outH;
		draw_->GetFramebufferDimensions(outputFB, &outW, &outH);

		// Bind output FB as render target
		draw_->BindFramebufferAsRenderTarget(outputFB, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "GBAPost");

		// Bind input (previous pass output) as texture
		draw_->BindFramebufferAsTexture(currentInput, 0, Draw::Aspect::COLOR_BIT, 0);

		// Set viewport to output FB size
		Draw::Viewport viewport{ 0.0f, 0.0f, (float)outW, (float)outH, 0.0f, 1.0f };
		draw_->SetViewport(viewport);
		draw_->SetScissorRect(0, 0, outW, outH);

		// Calculate and upload uniforms
		PostShaderUniforms uniforms;
		{
			float u_delta = 1.0f / lastWidth;
			float v_delta = 1.0f / lastHeight;
			float u_pixel_delta = 1.0f / outW;
			float v_pixel_delta = 1.0f / outH;
			int flipCount = __DisplayGetFlipCount();
			int vCount = __DisplayGetVCount();
			float time[4] = { (float)time_now_d(), (float)(vCount % 60) / 60.0f, (float)vCount, (float)(flipCount % 60) };

			uniforms.texelDelta[0] = u_delta;
			uniforms.texelDelta[1] = v_delta;
			uniforms.pixelDelta[0] = u_pixel_delta;
			uniforms.pixelDelta[1] = v_pixel_delta;
			memcpy(uniforms.time, time, 4 * sizeof(float));
			uniforms.timeDelta[0] = time[0] - prevUniforms_.time[0];
			uniforms.timeDelta[1] = (time[2] - prevUniforms_.time[2]) / 60.0f;
			uniforms.timeDelta[2] = time[2] - prevUniforms_.time[2];
			uniforms.timeDelta[3] = (time[3] != prevUniforms_.time[3]) ? 1.0f : 0.0f;
			uniforms.video = 0.0f;
			uniforms.vr = 0.0f;
			uniforms.gl_HalfPixel[0] = u_pixel_delta * 0.5f;
			uniforms.gl_HalfPixel[1] = v_pixel_delta * 0.5f;

			// Shader settings from config
			for (int s = 0; s < 4; s++) {
				std::string key = info.section;
				char suffix[32];
				snprintf(suffix, sizeof(suffix), "SettingCurrentValue%d", s + 1);
				key += suffix;
				auto it = g_Config.mPostShaderSetting.find(key);
				uniforms.setting[s] = (it != g_Config.mPostShaderSetting.end()) ? it->second : info.settings[s].value;
			}

			// [PPSSPP-FORK] MultiCore: override gamma + LCD profile from global config
			if (info.section == "GBALCD") {
				uniforms.setting[2] = (float)g_Config.iGBALCDProfile;  // u_setting.z = profile
				uniforms.setting[3] = g_Config.fGBAGamma;              // u_setting.w = gamma
			} else if (info.section == "GBA_GAMMA") {
				uniforms.setting[0] = g_Config.fGBAGamma;              // u_setting.x = gamma
			}
		}

		// Bind pipeline and update uniforms
		draw_->BindPipeline(pipeline);
		draw_->UpdateDynamicUniformBuffer(&uniforms, sizeof(uniforms));

		// Bind sampler (reuse cached)
		Draw::SamplerState *sampler = info.isUpscalingFilter ? samplerNearest_ : samplerLinear_;
		draw_->BindSamplerStates(0, 1, &sampler);
		draw_->BindVertexBuffer(vdata_, 0);
		draw_->Draw(4, 0);

		// Rotate: current output becomes next input
		currentInput = outputFB;
		lastWidth = outW;
		lastHeight = outH;

		// Save uniforms for next frame
		memcpy(&prevUniforms_, &uniforms, sizeof(uniforms));
	}

	return currentInput;
}
