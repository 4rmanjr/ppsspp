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
#include "GPU/Common/PostShader.h"   // [PPSSPP-FORK] MultiCore: post-shader list
#include "EmuCore/Config.h"

using namespace UI;

void GBASettingsScreen::CreateViews() {
	auto gs = GetI18NCategory(I18NCat::GRAPHICS);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto au = GetI18NCategory(I18NCat::AUDIO);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ps = GetI18NCategory(I18NCat::POSTSHADERS);

	root_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	list->SetSpacing(0);

	// === Controls ===
	list->Add(new ItemHeader(co->T("Controls")));
	list->Add(new Choice(co->T("Control Mapping")))->OnClick.Add([this](EventParams &) {
		screenManager()->push(new ControlMappingScreen(gamePath_));
	});
	// [PPSSPP-FORK] MultiCore: Controls section — add touch toggle matching PSP GameSettingsScreen
	list->Add(new CheckBox(&g_Config.bShowTouchControls, co->T("On-screen touch controls")));

	Choice *touchLayoutChoice = list->Add(new Choice(co->T("Customize On-Screen Controls")));
	touchLayoutChoice->OnClick.Add([this](EventParams &) {
		screenManager()->push(new CoreTouchLayoutScreen(gamePath_, EmuCore::Type::GBA));
	});
	touchLayoutChoice->SetEnabledPtr(&g_Config.bShowTouchControls);

	// === Display ===
	list->Add(new ItemHeader(gs->T("Display")));

	static const char *aspectOptions[] = {"3:2", "16:9", "1:1", "Stretch"};
	list->Add(new PopupMultiChoice(&g_Config.iGBAAspectRatio, gs->T("Aspect Ratio"), aspectOptions, 0, 4, I18NCat::GRAPHICS, screenManager()));

	static const char *filterOptions[] = {"Nearest", "Linear"};
	list->Add(new PopupMultiChoice(&g_Config.iGBATexFiltering, gs->T("Texture Filtering"), filterOptions, 0, 2, I18NCat::GRAPHICS, screenManager()));

	static const char *scaleOptions[] = {"Off", "Auto", "2x", "3x", "4x"};
	list->Add(new PopupMultiChoice(&g_Config.iGBAIntegerScale, gs->T("Integer Scaling"), scaleOptions, 0, 5, I18NCat::GRAPHICS, screenManager()));

	// [PPSSPP-FORK] MultiCore: GBA post-processing shader selector
	// Build list of available post-shaders + "Use PSP Setting" entry
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	const auto &allShaders = GetAllPostShaderInfo();
	std::vector<std::string> shaderDisplay;
	std::vector<std::string> shaderValues;
	shaderDisplay.push_back(std::string(ps->T("UsePSP", "Use PSP Setting")));  // Display: descriptive
	shaderValues.push_back("");                                      // Value: empty = use PSP
	for (const auto &shader : allShaders) {
		if (shader.visible && !shader.isStereo) {
			shaderDisplay.push_back(std::string(ps->T(shader.section.c_str(), shader.name)));
			shaderValues.push_back(shader.section);
		}
	}
	list->Add(new PopupMultiChoiceDynamic(&g_Config.sGBAPostShader, gs->T("Post-Processing Shader"), shaderDisplay, I18NCat::POSTSHADERS, screenManager(), &shaderValues));

	// [PPSSPP-FORK] MultiCore: Quick toggle to enable GBA LCD Simulation
	Choice *lcdToggle;
	if (g_Config.sGBAPostShader == "GBALCD") {
		lcdToggle = list->Add(new Choice(gs->T("Disable GBA LCD Simulation")));
	} else {
		lcdToggle = list->Add(new Choice(gs->T("Enable GBA LCD Simulation")));
	}
	lcdToggle->OnClick.Add([this](EventParams &) {
		if (g_Config.sGBAPostShader == "GBALCD") {
			g_Config.sGBAPostShader.clear();
		} else {
			g_Config.sGBAPostShader = "GBALCD";
		}
		RecreateViews();
	});

	// [PPSSPP-FORK] MultiCore: GBA brightness slider (0.5-1.0, beyond 1.0 requires HDR pipeline)
	list->Add(new PopupSliderChoiceFloat(&g_Config.fGBABrightness, 0.5f, 1.0f, 1.0f, gs->T("Brightness"), 0.05f, screenManager(), ""));

	// [PPSSPP-FORK] MultiCore: GBA gamma correction slider
	list->Add(new PopupSliderChoiceFloat(&g_Config.fGBAGamma, 1.0f, 2.5f, 1.5f, gs->T("Gamma Correction"), 0.1f, screenManager(), ""));

	// [PPSSPP-FORK] MultiCore: GBA LCD profile selector (relevant when GBALCD shader is active)
	static const char *lcdProfileOptions[] = {"AGB-001 (Original)", "AGS-001 (SP Frontlit)", "AGS-101 (SP Backlit)"};
	list->Add(new PopupMultiChoice(&g_Config.iGBALCDProfile, gs->T("LCD Profile"), lcdProfileOptions, 0, 3, I18NCat::GRAPHICS, screenManager()));

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
