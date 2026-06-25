// [PPSSPP-FORK] MultiCore: Generic per-core touch layout editor
// Lightweight editor for per-core touch button positions.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/TabHolder.h"
#include "BaseScreens.h"
#include "EmuCore/EmuCore.h"

class GBALayoutView;

class CoreTouchLayoutScreen : public UIBaseDialogScreen {
public:
	CoreTouchLayoutScreen(const Path &gamePath, EmuCore::Type coreType)
		: UIBaseDialogScreen(gamePath), coreType_(coreType) {}

	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void onFinish(DialogResult reason) override;
	void resized() override;
	void update() override;

	const char *tag() const override { return "CoreTouchLayout"; }

protected:
	ViewLayoutMode LayoutMode() const override { return ViewLayoutMode::ApplyInsets; }
	void OnReset(UI::EventParams &e);

private:
	EmuCore::Type coreType_;
	UI::ChoiceStrip *mode_ = nullptr;
	GBALayoutView *layoutView_ = nullptr;
	bool borderState_ = false;  // shadow for iTouchButtonStyle (bool → int)
};
