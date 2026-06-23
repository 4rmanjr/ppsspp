// [PPSSPP-FORK] MultiCore: GBA settings screen implementation
// Jangan hapus, jangan ubah kode upstream.

#include "GBASettingsScreen.h"
#include "Core/Config.h"
#include "Common/UI/Context.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/View.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Data/Text/I18n.h"
#include "UI/ControlMappingScreen.h"
#include "UI/CoreTouchLayoutScreen.h"
#include "EmuCore/Config.h"

using namespace UI;

void GBASettingsScreen::CreateViews() {
	auto gs = GetI18NCategory(I18NCat::GRAPHICS);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto au = GetI18NCategory(I18NCat::AUDIO);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	root_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	list->SetSpacing(0);

	// === Controls ===
	list->Add(new ItemHeader(co->T("Controls")));
	list->Add(new Choice(co->T("Control Mapping")))->OnClick.Add([this](EventParams &) {
		screenManager()->push(new ControlMappingScreen(gamePath_));
	});
	list->Add(new Choice(co->T("Customize On-Screen Controls")))->OnClick.Add([this](EventParams &) {
		screenManager()->push(new CoreTouchLayoutScreen(gamePath_, EmuCore::Type::GBA));
	});

	// === Display ===
	list->Add(new ItemHeader(gs->T("Display")));

	static const char *aspectOptions[] = {"3:2", "16:9", "1:1", "Stretch"};
	list->Add(new PopupMultiChoice(&g_Config.iGBAAspectRatio, gs->T("Aspect Ratio"), aspectOptions, 0, 4, I18NCat::GRAPHICS, screenManager()));

	static const char *filterOptions[] = {"Nearest", "Linear"};
	list->Add(new PopupMultiChoice(&g_Config.iGBATexFiltering, gs->T("Texture Filtering"), filterOptions, 0, 2, I18NCat::GRAPHICS, screenManager()));

	list->Add(new CheckBox(&g_Config.bGBAIntegerScaling, gs->T("Integer Scaling")));

	// === Audio ===
	list->Add(new ItemHeader(au->T("Audio")));
	list->Add(new PopupSliderChoiceFloat(&g_Config.fGBAVolume, 0.0f, 1.0f, 1.0f, au->T("Volume"), 0.05f, screenManager(), ""));

	// === OK ===
	list->Add(new Choice(di->T("OK")))->OnClick.Add([this](EventParams &) {
		TriggerFinish(DR_OK);
	});
}

void GBASettingsScreen::onFinish(DialogResult result) {
	if (result == DR_OK) {
		g_Config.Save("GBASettingsScreen::onFinish");
	}
}
