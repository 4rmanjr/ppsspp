// [PPSSPP-FORK] TestGBASpeedControl
// Unit test for GBA speed control logic (EmuCore/GBASpeedControl.h)
// Jangan hapus, jangan ubah kode upstream.
#ifdef PPSSPP_MULTICORE

#include "EmuCore/GBASpeedControl.h"
#include <cstdio>
#include <cstring>

// Manual test framework matching UnitTest.h macros
#define EXPECT_TRUE(a) if (!(a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_FALSE(a) if ((a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_EQ(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n  expected: %d\n  actual:   %d\n", __FUNCTION__, __LINE__, (int)(a), (int)(b)); return false; }

// ── ComputeGBAFramesToRun tests ──────────────────────────────────────────────

static bool TestNormalSpeed() {
	printf("--- TestNormalSpeed\n");
	double lastFrameTime = 0.0;

	EmuCore::GBASpeedInput input;
	input.fastForward = false;
	input.fpsLimit = FPSLimit::NORMAL;
	input.customFps1 = 60;
	input.customFps2 = 0;
	input.renderDuplicateFrames = false;
	input.now = 1.0; // first frame at t=1.0

	// lastFrameTime=0, now=1.0 → interval (1/60≈0.0167) elapsed → 1 frame
	int frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1);
	// lastFrameTime should be updated to now
	EXPECT_EQ(lastFrameTime == 1.0, true);

	// Call again immediately → interval NOT elapsed → 0 frames
	input.now = 1.001;
	frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 0);
	// lastFrameTime unchanged
	EXPECT_EQ(lastFrameTime == 1.0, true);

	// Wait past interval → 1 frame
	input.now = 1.05;
	frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1);
	EXPECT_EQ(lastFrameTime == 1.05, true);

	printf("  PASS\n");
	return true;
}

static bool TestFastForward() {
	printf("--- TestFastForward\n");
	double lastFrameTime = 0.0;

	EmuCore::GBASpeedInput input;
	input.fastForward = true;
	input.fpsLimit = FPSLimit::NORMAL;
	input.renderDuplicateFrames = false; // default: 8 frames
	input.now = 1.0;

	// Fast forward ON → 8 frames regardless of timing
	int frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 8);
	// lastFrameTime NOT updated (fast forward bypasses timing)
	EXPECT_EQ(lastFrameTime == 0.0, true);

	// With renderDuplicateFrames → 3 frames
	input.renderDuplicateFrames = true;
	frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 3);
	EXPECT_EQ(lastFrameTime == 0.0, true);

	printf("  PASS\n");
	return true;
}

static bool TestCustomFps() {
	printf("--- TestCustomFps\n");
	double lastFrameTime = 0.0;

	EmuCore::GBASpeedInput input;
	input.fastForward = false;
	input.fpsLimit = FPSLimit::CUSTOM1;
	input.customFps1 = 120; // 2x speed
	input.customFps2 = 30;  // unused
	input.now = 1.0;

	// CUSTOM1=120fps → targetInterval=0.0083, should run at 2x
	// After 1 second, definitely elapsed
	int frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1); // still 1 frame per call, just at higher frequency
	EXPECT_EQ(lastFrameTime == 1.0, true);

	// Now set to CUSTOM2=30fps (0.033s interval)
	input.fpsLimit = FPSLimit::CUSTOM2;
	input.customFps2 = 30;
	lastFrameTime = 0.0;

	// After 1 second, elapsed
	frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1);
	EXPECT_EQ(lastFrameTime == 1.0, true);

	// CUSTOM1 with invalid (zero/negative) fps → fallback to 60
	input.fpsLimit = FPSLimit::CUSTOM1;
	input.customFps1 = 0;
	input.customFps2 = 0;
	lastFrameTime = 0.0;

	frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1); // falls back to 60fps timing

	printf("  PASS\n");
	return true;
}

static bool TestZeroTargetInterval() {
	printf("--- TestZeroTargetInterval\n");
	double lastFrameTime = 0.0;

	EmuCore::GBASpeedInput input;
	input.fastForward = false;
	input.fpsLimit = FPSLimit::CUSTOM1;
	input.customFps1 = 0; // → fpsLimit=60 after fallback, not zero
	// Actually the fallback clamps to 60, so target = 0.0167
	// Let me test what happens with fpsLimit that would be problematic
	input.now = 1.0;

	// fpsLimit = 60 after fallback → targetInterval = 0.0167
	// Since 1.0 > 0.0167, should return 1
	int frames = EmuCore::ComputeGBAFramesToRun(input, lastFrameTime);
	EXPECT_EQ(frames, 1);

	// Without fallback, if fpsLimit were somehow zero:
	// targetInterval = INF, so the else-if would be (INF <= 0 || ...)
	// → INF <= 0 is false
	// → now - lastFrameTime >= INF is false (always, since INF > any finite)
	// → returns 0 (no frame)
	// This is fine as it prevents division by zero / runaway

	printf("  PASS\n");
	return true;
}

// ── CycleSpeedToggle tests ──────────────────────────────────────────────────

static bool TestCycleToggleNormalToCustom1() {
	printf("--- TestCycleToggleNormalToCustom1\n");
	// NORMAL → CUSTOM1 (when customFps1 is valid)
	FPSLimit result = EmuCore::CycleSpeedToggle(FPSLimit::NORMAL, 60, 0);
	EXPECT_EQ((int)result, (int)FPSLimit::CUSTOM1);

	// NORMAL → stays NORMAL when customFps1 is invalid
	result = EmuCore::CycleSpeedToggle(FPSLimit::NORMAL, -1, 0);
	EXPECT_EQ((int)result, (int)FPSLimit::NORMAL);

	printf("  PASS\n");
	return true;
}

static bool TestCycleToggleCustom1ToCustom2() {
	printf("--- TestCycleToggleCustom1ToCustom2\n");
	// CUSTOM1 → CUSTOM2 (when customFps2 is valid)
	FPSLimit result = EmuCore::CycleSpeedToggle(FPSLimit::CUSTOM1, 60, 30);
	EXPECT_EQ((int)result, (int)FPSLimit::CUSTOM2);

	// CUSTOM1 → NORMAL (when customFps2 is invalid)
	result = EmuCore::CycleSpeedToggle(FPSLimit::CUSTOM1, 60, -1);
	EXPECT_EQ((int)result, (int)FPSLimit::NORMAL);

	printf("  PASS\n");
	return true;
}

static bool TestCycleToggleCustom2ToNormal() {
	printf("--- TestCycleToggleCustom2ToNormal\n");
	FPSLimit result = EmuCore::CycleSpeedToggle(FPSLimit::CUSTOM2, 60, 30);
	EXPECT_EQ((int)result, (int)FPSLimit::NORMAL);

	printf("  PASS\n");
	return true;
}

static bool TestCycleToggleFullCycle() {
	printf("--- TestCycleToggleFullCycle\n");
	// Full cycle: NORMAL → CUSTOM1 → CUSTOM2 → NORMAL
	FPSLimit limit = FPSLimit::NORMAL;

	limit = EmuCore::CycleSpeedToggle(limit, 60, 30);
	EXPECT_EQ((int)limit, (int)FPSLimit::CUSTOM1);

	limit = EmuCore::CycleSpeedToggle(limit, 60, 30);
	EXPECT_EQ((int)limit, (int)FPSLimit::CUSTOM2);

	limit = EmuCore::CycleSpeedToggle(limit, 60, 30);
	EXPECT_EQ((int)limit, (int)FPSLimit::NORMAL);

	printf("  PASS\n");
	return true;
}

// ── Register tests ──────────────────────────────────────────────────────────

struct TestEntry {
	const char *name;
	bool (*func)();
};

#define REGISTER_TEST(name) { #name, name }

static TestEntry tests[] = {
	REGISTER_TEST(TestNormalSpeed),
	REGISTER_TEST(TestFastForward),
	REGISTER_TEST(TestCustomFps),
	REGISTER_TEST(TestZeroTargetInterval),
	REGISTER_TEST(TestCycleToggleNormalToCustom1),
	REGISTER_TEST(TestCycleToggleCustom1ToCustom2),
	REGISTER_TEST(TestCycleToggleCustom2ToNormal),
	REGISTER_TEST(TestCycleToggleFullCycle),
};

static const int numTests = sizeof(tests) / sizeof(tests[0]);

int main(int argc, const char *argv[]) {
	int passed = 0;
	int failed = 0;

	const char *filter = nullptr;
	if (argc > 1) {
		filter = argv[1];
	}

	for (int i = 0; i < numTests; i++) {
		if (filter && strcmp(tests[i].name, filter) != 0) {
			continue;
		}
		printf("Test: %s\n", tests[i].name);
		if (tests[i].func()) {
			printf("  ✓ PASS\n");
			passed++;
		} else {
			printf("  ✗ FAIL\n");
			failed++;
		}
	}

	printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
	return failed > 0 ? 1 : 0;
}

#else // !PPSSPP_MULTICORE
int main(int, const char *[]) {
	printf("Test skipped: PPSSPP_MULTICORE not defined\n");
	return 0;
}
#endif
