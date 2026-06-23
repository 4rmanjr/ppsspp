// [PPSSPP-FORK] MultiCore: GBA settings screen implementation
// Jangan hapus, jangan ubah kode upstream.

#include "GBASettingsScreen.h"
#include "Core/Config.h"
#include "Common/UI/Context.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/View.h"
#include "Common/Data/Text/I18n.h"
#include "UI/ControlMappingScreen.h"

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

	// === Display ===
	list->Add(new ItemHeader(gs->T("Display")));

	list->Add(new PopupSliderChoice(&g_Config.iGBAAspectRatio, 0, 3, 0, gs->T("Aspect Ratio"), screenManager()));

	list->Add(new PopupSliderChoice(&g_Config.iGBATexFiltering, 0, 1, 1, gs->T("Texture Filtering"), screenManager()));

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
