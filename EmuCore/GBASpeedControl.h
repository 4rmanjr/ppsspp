// [PPSSPP-FORK] GBASpeedControl
// Testable speed control helpers for GBA core (ComputeGBAFramesToRun, CycleSpeedToggle)
// Jangan hapus, jangan ubah kode upstream.
#pragma once

#include "Core/CoreParameter.h"

namespace EmuCore {

// [PPSSPP-FORK] GBASpeedControl: input parameters for frame scheduling
struct GBASpeedInput {
	bool fastForward = false;
	FPSLimit fpsLimit = FPSLimit::NORMAL;
	int customFps1 = 60;   // g_Config.iFpsLimit1
	int customFps2 = 60;   // g_Config.iFpsLimit2
	bool renderDuplicateFrames = false; // g_Config.bRenderDuplicateFrames
	double now = 0.0;      // time_now_d()
};

// [PPSSPP-FORK] GBASpeedControl: compute framesToRun for one GBA update loop.
// Returns 0 (no frame), 1 (normal), or 3/8 (fast-forward).
// Updates lastFrameTime when a frame is scheduled.
inline int ComputeGBAFramesToRun(const GBASpeedInput &input, double &lastFrameTime) {
	int fpsLimit = 60;
	if (input.fpsLimit == FPSLimit::CUSTOM1) {
		fpsLimit = input.customFps1 > 0 ? input.customFps1 : 60;
	} else if (input.fpsLimit == FPSLimit::CUSTOM2) {
		fpsLimit = input.customFps2 > 0 ? input.customFps2 : 60;
	}

	// Fast forward: run multiple frames per update
	if (input.fastForward) {
		return input.renderDuplicateFrames ? 3 : 8;
	}

	// Normal / custom speed: schedule based on target interval
	double targetInterval = 1.0 / fpsLimit;
	if (targetInterval <= 0.0 || (input.now - lastFrameTime) >= targetInterval * 0.95) {
		lastFrameTime = input.now;
		return 1;
	}

	return 0;
}

// [PPSSPP-FORK] GBASpeedControl: cycle VIRTKEY_SPEED_TOGGLE logic.
// Mirrors the PSP cycle: NORMAL → CUSTOM1 → CUSTOM2 → NORMAL.
inline FPSLimit CycleSpeedToggle(FPSLimit current, int customFps1, int customFps2) {
	if (current == FPSLimit::NORMAL && customFps1 >= 0) {
		return FPSLimit::CUSTOM1;
	} else if (current == FPSLimit::CUSTOM1 && customFps2 >= 0) {
		return FPSLimit::CUSTOM2;
	} else if (current == FPSLimit::CUSTOM1 || current == FPSLimit::CUSTOM2) {
		return FPSLimit::NORMAL;
	}
	return current; // No valid custom speeds configured
}

} // namespace EmuCore
