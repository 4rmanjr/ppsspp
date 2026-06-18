// [PPSSPP-FORK] MultiCore: GBA touch control layout
// Simplified touch overlay for GBA (A, B buttons instead of △○×□).
// Reuses PSP CTRL_ constants for button mapping.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#ifdef PPSSPP_MULTICORE

#include <vector>

namespace TouchLayoutGBA {

struct TouchButton {
	int pspButton;       // PSP CTRL_ constant (reused for GBA)
	float x, y;          // Position (0-1 normalized)
	float w, h;          // Size (normalized)
	const char *label;   // Display text ("A", "B", etc.)
};

// Get the GBA-specific touch button layout for the current screen orientation.
const std::vector<TouchButton> &GetLayout(bool portrait);

}  // namespace TouchLayoutGBA

#endif  // PPSSPP_MULTICORE
