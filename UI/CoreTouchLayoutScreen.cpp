// [PPSSPP-FORK] MultiCore: Generic per-core touch layout editor
// Allows adjusting touch button positions for GBA (and future cores).
// Jangan hapus, jangan ubah kode upstream.

#include "UI/CoreTouchLayoutScreen.h"
#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/UI.h"
#include "Common/Log.h"

void CoreTouchLayoutScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	// Ensure config is loaded for this core
	EmuCore::LoadTouchConfig(coreType_);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	// Title
	const char *coreName = EmuCore::GetConfigSection(coreType_);
	root_->Add(new TextView(std::string("Customize ") + coreName + " Touch Layout", ALIGN_CENTER, false))
		->SetTextSize(TextSize::Big);
	root_->Add(new Spacer(new LinearLayoutParams(10.0f)));

	// Button list — each button shows keyCode + position
	LinearLayout *listContainer = new LinearLayout(Orientation::ORIENT_VERTICAL, new AnchorLayoutParams(10.0f, 80.0f, NONE, NONE, 10.0f, 150.0f));
	root_->Add(listContainer);

	const auto &cfg = EmuCore::GetTouchConfig(coreType_, false);  // landscape
	char buf[128];
	for (int i = 0; i < cfg.count; i++) {
		snprintf(buf, sizeof(buf), "%s: (%.2f, %.2f) %.2fx%.2f",
			cfg.buttons[i].label,
			cfg.buttons[i].x, cfg.buttons[i].y,
			cfg.buttons[i].w, cfg.buttons[i].h);
		listContainer->Add(new TextView(buf, ALIGN_LEFT, false));
	}

	// Note
	root_->Add(new TextView("Edit positions in ppsspp.ini section [" + std::string(EmuCore::GetTouchConfigSection(coreType_, false)) + "]", ALIGN_CENTER, false))
		->SetTextSize(TextSize::Small);

	// Reset button
	root_->Add(new Button(co->T("Reset to Defaults")))->OnClick.Handle(this, &CoreTouchLayoutScreen::OnReset);

	// Back button
	root_->Add(new Button(di->T("Back")))->OnClick.Add([this](EventParams &) {
		TriggerFinish(DR_CANCEL);
	});
}

void CoreTouchLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Re-read config in case it was modified
	EmuCore::LoadTouchConfig(coreType_);
}

void CoreTouchLayoutScreen::onFinish(DialogResult reason) {
	INFO_LOG(Log::System, "[TOUCH] CoreTouchLayoutScreen closed for %s", EmuCore::GetConfigSection(coreType_));
}

void CoreTouchLayoutScreen::resized() {
	RecreateViews();
}

void CoreTouchLayoutScreen::OnReset(UI::EventParams &e) {
	EmuCore::InitDefaultTouchConfigs();
	EmuCore::SaveTouchConfig(coreType_);
	RecreateViews();
}
