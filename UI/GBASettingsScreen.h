// [PPSSPP-FORK] MultiCore: GBA settings screen (Controls, Display, Audio)
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"

class GBASettingsScreen : public UIDialogScreen {
public:
	GBASettingsScreen(const Path &gamePath) : gamePath_(gamePath) {}
	const char *tag() const override { return "GBASettings"; }
	void CreateViews() override;
	void onFinish(DialogResult result) override;

private:
	Path gamePath_;
};
