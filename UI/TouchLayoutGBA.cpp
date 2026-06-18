// [PPSSPP-FORK] MultiCore: GBA touch control layout
// Jangan hapus, jangan ubah kode upstream.

#include "UI/TouchLayoutGBA.h"

#ifdef PPSSPP_MULTICORE

#include "Core/HLE/sceCtrl.h"

namespace TouchLayoutGBA {

static const std::vector<TouchButton> landscapeLayout = {
	// D-Pad (left side)
	{CTRL_UP,    0.05f, 0.35f, 0.08f, 0.08f, "▲"},
	{CTRL_DOWN,  0.05f, 0.50f, 0.08f, 0.08f, "▼"},
	{CTRL_LEFT,  0.00f, 0.43f, 0.07f, 0.08f, "◀"},
	{CTRL_RIGHT, 0.10f, 0.43f, 0.07f, 0.08f, "▶"},

	// A & B buttons (right side) — GBA layout: A right, B left-down
	{CTRL_CROSS,  0.82f, 0.47f, 0.09f, 0.09f, "A"},
	{CTRL_CIRCLE, 0.73f, 0.38f, 0.09f, 0.09f, "B"},

	// L & R (top shoulders)
	{CTRL_LTRIGGER, 0.15f, 0.02f, 0.10f, 0.06f, "L"},
	{CTRL_RTRIGGER, 0.75f, 0.02f, 0.10f, 0.06f, "R"},

	// Start & Select (center)
	{CTRL_SELECT, 0.40f, 0.12f, 0.08f, 0.05f, "Select"},
	{CTRL_START,  0.52f, 0.12f, 0.08f, 0.05f, "Start"},
};

static const std::vector<TouchButton> portraitLayout = {
	// D-Pad
	{CTRL_UP,    0.05f, 0.50f, 0.10f, 0.08f, "▲"},
	{CTRL_DOWN,  0.05f, 0.68f, 0.10f, 0.08f, "▼"},
	{CTRL_LEFT,  0.00f, 0.59f, 0.07f, 0.08f, "◀"},
	{CTRL_RIGHT, 0.13f, 0.59f, 0.07f, 0.08f, "▶"},

	// A & B
	{CTRL_CROSS,  0.80f, 0.62f, 0.11f, 0.10f, "A"},
	{CTRL_CIRCLE, 0.68f, 0.52f, 0.11f, 0.10f, "B"},

	// L & R
	{CTRL_LTRIGGER, 0.10f, 0.02f, 0.12f, 0.06f, "L"},
	{CTRL_RTRIGGER, 0.78f, 0.02f, 0.12f, 0.06f, "R"},

	// Start & Select
	{CTRL_SELECT, 0.35f, 0.25f, 0.12f, 0.06f, "Select"},
	{CTRL_START,  0.53f, 0.25f, 0.12f, 0.06f, "Start"},
};

const std::vector<TouchButton> &GetLayout(bool portrait) {
	return portrait ? portraitLayout : landscapeLayout;
}

}  // namespace TouchLayoutGBA

#endif  // PPSSPP_MULTICORE
