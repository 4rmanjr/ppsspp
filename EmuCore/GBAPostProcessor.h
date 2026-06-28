// [PPSSPP-FORK] MultiCore: GBA post-processing pipeline
// Wraps PPSSPP's existing post-shader system for GBA output.

#pragma once

#include <vector>
#include <string>
#include "Common/GPU/thin3d.h"

struct ShaderInfo;

class GBAPostProcessor {
public:
	GBAPostProcessor(Draw::DrawContext *draw);
	~GBAPostProcessor();

	void Init(int srcWidth, int srcHeight);
	void Shutdown();
	void DeviceLost();
	void DeviceRestored(Draw::DrawContext *draw);

	bool IsActive() const { return usePostShader_; }

	// Rebuild shader chain from config. Returns true if any shaders are active.
	bool UpdatePostShader(const std::vector<std::string> &shaderNames, int screenWidth, int screenHeight);

	// Run post-processing chain on input FB. Returns output FB (may be input if chain empty).
	Draw::Framebuffer *Process(Draw::Framebuffer *input);

private:
	// Thin3D vertex layout for fullscreen quads
	struct QuadVertex {
		float x, y, z;
		float u, v;
		uint32_t rgba;
	};

	// Uniform buffer layout matching post-shader expectations
	struct PostShaderUniforms {
		float gl_HalfPixel[4]{};
		float texelDelta[2]{};
		float pixelDelta[2]{};
		float time[4]{};
		float timeDelta[4]{};
		float setting[4]{};
		float video = 0.0f;
		float vr = 0.0f;
	};

	void DestroyShaders();
	bool CompileShaders(const std::vector<const ShaderInfo *> &chain);
	void RunShaderPass(const ShaderInfo &info, Draw::Pipeline *pipeline,
		Draw::Framebuffer *inputFB, Draw::Framebuffer *outputFB, int lastWidth, int lastHeight,
		int outWidth, int outHeight);

	Draw::DrawContext *draw_ = nullptr;
	bool initialized_ = false;
	bool usePostShader_ = false;

	int srcWidth_ = 240;
	int srcHeight_ = 160;
	int screenWidth_ = 0;
	int screenHeight_ = 0;

	// Vertex buffer for fullscreen quad (4 verts)
	Draw::Buffer *vdata_ = nullptr;

	// Cached samplers (created once, reused every frame)
	Draw::SamplerState *samplerNearest_ = nullptr;
	Draw::SamplerState *samplerLinear_ = nullptr;

	// Shader chain
	std::vector<Draw::Pipeline *> shaderPipelines_;
	std::vector<ShaderInfo> shaderInfo_;
	std::vector<Draw::Framebuffer *> tempFramebuffers_;

	// Cached shader names to avoid recompilation every frame
	std::vector<std::string> currentShaderNames_;

	// Previous frame uniforms for time delta
	PostShaderUniforms prevUniforms_{};
};
