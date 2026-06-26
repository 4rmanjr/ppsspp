// [PPSSPP-FORK] MultiCore: Minimal test — verify GBACore loads and runs a ROM
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/GBACore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>

void handler(int sig) {
	fprintf(stderr, "Signal %d caught!\n", sig);
	exit(1);
}

int main(int argc, char *argv[]) {
	signal(SIGSEGV, handler);

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <gba_rom>\n", argv[0]);
		return 1;
	}

	const char *romPath = argv[1];
	printf("ROM path: %s\n", romPath);
	fflush(stdout);

	printf("Creating GBACore...\n");
	fflush(stdout);
	EmuCore::GBACore *core = new EmuCore::GBACore();
	printf("GBACore created: %p\n", (void*)core);
	fflush(stdout);

	printf("Loading ROM: %s\n", romPath);
	fflush(stdout);
	Path path(romPath);
	printf("Path created: %s\n", path.c_str());
	fflush(stdout);

	bool loaded = core->LoadROM(path);
	if (!loaded) {
		fprintf(stderr, "FAILED: Could not load ROM\n");
		delete core;
		return 1;
	}

	printf("ROM loaded OK!\n");
	fflush(stdout);

	printf("Testing input mapping...\n");
	fflush(stdout);
	// Test PSP→GBA key conversion
	uint32_t pspTestKeys = 0x4000 | 0x0008 | 0x0010; // Cross + Start + Up
	uint32_t gbaKeys = EmuCore::GBACore::PSPSKeysToGBA(pspTestKeys);
	printf("  PSP keys 0x%04X → GBA keys 0x%04X\n", pspTestKeys, gbaKeys);
	fflush(stdout);

	core->SetKeys(pspTestKeys);
	printf("  SetKeys OK\n");
	fflush(stdout);

	printf("Running 3 frames...\n");
	fflush(stdout);
	for (int i = 0; i < 3; i++) {
		core->RunFrame();
		printf("  Frame %d: OK\n", i + 1);
		fflush(stdout);
	}

	printf("Getting game info...\n");
	fflush(stdout);
	std::string title, id;
	core->GetGameInfo(title, id);
	printf("  Title: '%s'\n", title.c_str());
	printf("  Code: '%s'\n", id.c_str());
	fflush(stdout);

	const uint32_t *video = core->GetVideoBuffer();
	if (video) {
		printf("Video[0] = 0x%08X\n", video[0]);
	} else {
		printf("No video buffer\n");
	}

	delete core;
	printf("\n=== ALL TESTS PASSED ===\n");
	return 0;
}
