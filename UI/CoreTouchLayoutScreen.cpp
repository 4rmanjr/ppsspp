// [PPSSPP-FORK] MultiCore: Generic per-core touch layout editor
// Interactive drag-and-drop editor for GBA (and future cores).
// Mirrors TouchControlLayoutScreen behavior.
// Jangan hapus, jangan ubah kode upstream.

#include "UI/CoreTouchLayoutScreen.h"
#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/UI.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Log.h"
#include "UI/GamepadEmu.h"

static float g_layoutScale = 0.8f;

class GBADragDrop : public MultiTouchButton {
public:
	GBADragDrop(EmuCore::CoreTouchButton &btn, const Bounds &screenBounds, ImageID bgImg, ImageID img)
		: MultiTouchButton("gba_dd", bgImg, bgImg, img, 1.0f,
			new UI::AnchorLayoutParams(btn.x * screenBounds.w, btn.y * screenBounds.h, UI::NONE, UI::NONE, UI::Centering::Both)),
		  btn_(btn), screenBounds_(screenBounds) {}

	bool IsDownVisually() const override { return false; }
	void Draw(UIContext &dc) override {
		scale_ = g_layoutScale;
		MultiTouchButton::Draw(dc);
	}
	void SavePosition() {
		btn_.x = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		btn_.y = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
	}
	void SetVisibleFlag(bool visible) { btn_.visible = visible; }
	bool GetVisibleFlag() const { return btn_.visible; }
	float GetScaleVal() const { return btn_.w; }
	void SetScaleVal(float s) { btn_.w = btn_.h = s; }
	virtual bool Contains(float x, float y) {
		const float t = 0.25f;
		Bounds tb(bounds_.x - t * bounds_.w * 0.5f, bounds_.y - t * bounds_.h * 0.5f,
			bounds_.w * (1.0f + t), bounds_.h * (1.0f + t));
		return tb.Contains(x, y);
	}
private:
	EmuCore::CoreTouchButton &btn_;
	const Bounds &screenBounds_;
};

class GBALayoutView : public UI::AnchorLayout {
public:
	GBALayoutView(EmuCore::Type core, bool portrait, UI::LayoutParams *lp)
		: UI::AnchorLayout(lp) { SetClip(true); coreType_ = core; portrait_ = portrait; }

	bool Touch(const TouchInput &input) override;
	void CreateViews();
	void Draw(UIContext &dc) override;
	void SetPortrait(bool portrait);
	bool HasCreatedViews() const { return !controls_.empty(); }

	int mode_ = 0;

private:
	void ClearControls();
	void ToggleButtonVisibility(GBADragDrop *dd);
	GBADragDrop *picked_ = nullptr;
	float startObjX_ = -1.0f, startObjY_ = -1.0f;
	float startDragX_ = -1.0f, startDragY_ = -1.0f;
	float startScale_ = -1.0f;
	EmuCore::Type coreType_;
	bool portrait_;
	std::vector<GBADragDrop *> controls_;
};

bool GBALayoutView::Touch(const TouchInput &touch) {
	using namespace UI;

	if ((touch.flags & TouchInputFlags::MOVE) && picked_) {
		if (mode_ == 0) {
			float nx = startObjX_ + (touch.x - startDragX_);
			float ny = startObjY_ + (touch.y - startDragY_);
			if (g_Config.bTouchSnapToGrid && g_Config.iTouchSnapGridSize > 0) {
				float grid = (float)g_Config.iTouchSnapGridSize;
				nx -= fmod(nx, grid);
				ny -= fmod(ny, grid);
			}
			picked_->ReplaceLayoutParams(new AnchorLayoutParams(nx, ny, NONE, NONE, Centering::Both));
		} else if (mode_ == 1) {
			float diff = -(touch.y - startDragY_) * 0.02f;
			float ns = startScale_ + diff;
			if (ns < 0.3f) ns = 0.3f;
			if (ns > 1.5f) ns = 1.5f;
			picked_->SetScaleVal(ns);
		}
	}
	if ((touch.flags & TouchInputFlags::DOWN) && !picked_) {
		for (auto *c : controls_) {
			if (c->Contains(touch.x, touch.y)) {
				if (mode_ == 2) {
					ToggleButtonVisibility(c);
					return true;
				}
				picked_ = c;
				startDragX_ = touch.x;
				startDragY_ = touch.y;
				const auto *params = picked_->GetLayoutParams()->As<AnchorLayoutParams>();
				startObjX_ = params->left;
				startObjY_ = params->top;
				startScale_ = picked_->GetScaleVal();
				break;
			}
		}
	}
	if ((touch.flags & TouchInputFlags::UP) && picked_) {
		picked_->SavePosition();
		picked_ = nullptr;
	}
	return true;
}

void GBALayoutView::ToggleButtonVisibility(GBADragDrop *dd) {
	dd->SetVisibleFlag(!dd->GetVisibleFlag());
	ClearControls();
	CreateViews();
}

void GBALayoutView::Draw(UIContext &dc) {
	using namespace UI;
	dc.FillRect(Drawable(0x80000000), bounds_);
	dc.Flush();
	// Draw snap grid or customize hints
	if (mode_ == 2) {
		// In customize mode, show a hint that tapping toggles visibility
		char msg[64];
		snprintf(msg, sizeof(msg), "Tap to toggle visibility");
		dc.DrawText(msg, bounds_.centerX(), bounds_.y + 20, 0xFFFFFFFF, ALIGN_CENTER);
		dc.Flush();
	}
	AnchorLayout::Draw(dc);
}

void GBALayoutView::ClearControls() {
	for (auto *c : controls_) RemoveSubview(c);
	controls_.clear();
}

void GBALayoutView::SetPortrait(bool portrait) {
	if (portrait_ == portrait) return;
	portrait_ = portrait;
	ClearControls();
	CreateViews();
}

void GBALayoutView::CreateViews() {
	const Bounds &b = GetBounds();
	if (b.w == 0.0f || b.h == 0.0f) return;

	auto &cfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);

	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		if (!btn.visible && mode_ != 2) continue;
		ImageID icon;
		switch (btn.keyCode) {
		case CTRL_CROSS:     icon = ImageID("I_CROSS");   break;
		case CTRL_CIRCLE:    icon = ImageID("I_CIRCLE");  break;
		case CTRL_SELECT:    icon = ImageID("I_SELECT");  break;
		case CTRL_START:     icon = ImageID("I_START");   break;
		case CTRL_LTRIGGER:  icon = ImageID("I_L");      break;
		case CTRL_RTRIGGER:  icon = ImageID("I_R");      break;
		case CTRL_UP:
		case CTRL_DOWN:
		case CTRL_LEFT:
		case CTRL_RIGHT:     icon = ImageID("I_ARROW");   break;
		default:             icon = ImageID("I_CROSS");   break;
		}
		ImageID roundImg = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
		ImageID rectImg = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
		ImageID shoulderImg = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
		ImageID bg = (btn.keyCode == CTRL_LTRIGGER || btn.keyCode == CTRL_RTRIGGER) ? shoulderImg : roundImg;
		if (btn.keyCode == CTRL_SELECT || btn.keyCode == CTRL_START) bg = rectImg;
		auto *dd = new GBADragDrop(btn, b, bg, icon);
		controls_.push_back(dd);
		Add(dd);
	}
}

// Custom popup for per-button visibility toggle
class GBATouchVisibilityPopup : public UI::PopupScreen {
public:
	GBATouchVisibilityPopup(EmuCore::Type coreType, bool portrait)
		: UI::PopupScreen("GBA Touch Control Visibility"), coreType_(coreType), portrait_(portrait) {}

	const char *tag() const override { return "GBATouchVisibilityPopup"; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto co = GetI18NCategory(I18NCat::CONTROLS);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		auto &cfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);
		parent->Add(new ItemHeader(di->T("Show Buttons")));

		for (int i = 0; i < cfg.count; i++) {
			auto &btn = cfg.buttons[i];
			const char *label = nullptr;
			switch (btn.keyCode) {
			case CTRL_CROSS:     label = "A"; break;
			case CTRL_CIRCLE:    label = "B"; break;
			case CTRL_SELECT:    label = "Select"; break;
			case CTRL_START:     label = "Start"; break;
			case CTRL_LTRIGGER:  label = "L"; break;
			case CTRL_RTRIGGER:  label = "R"; break;
			case CTRL_UP:        label = "D-Pad Up"; break;
			case CTRL_DOWN:      label = "D-Pad Down"; break;
			case CTRL_LEFT:      label = "D-Pad Left"; break;
			case CTRL_RIGHT:     label = "D-Pad Right"; break;
			}
			if (!label) continue;
			parent->Add(new CheckBox(&btn.visible, co->T(label)));
		}
		parent->Add(new Spacer(12.0f));
		parent->Add(new Choice(di->T("OK")))->OnClick.Add([this](EventParams &) {
			EmuCore::SaveTouchConfig(coreType_);
			TriggerFinish(DR_OK);
		});
	}

private:
	EmuCore::Type coreType_;
	bool portrait_;
};

void CoreTouchLayoutScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	EmuCore::LoadTouchConfig(coreType_);
	const char *coreName = EmuCore::GetConfigSection(coreType_);
	borderState_ = g_Config.iTouchButtonStyle != 0;
	const Bounds &bounds = GetLayoutBounds(*screenManager()->getUIContext());
	const float leftW = 200.0f;
	g_layoutScale = 1.0f - (leftW + 10.0f) / std::max(bounds.w, 1.0f);

	auto *rootLayout = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	rootLayout->SetSpacing(0.0f);
	root_ = rootLayout;

	// Left sidebar
	auto *leftScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(leftW, FILL_PARENT)));
	leftScroll->SetAlignOpposite(true);
	auto *leftCol = leftScroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(12.0f, 0.0f))));
	leftCol->Add(new ItemHeader(std::string(coreName) + " Touch Layout"));

	// Mode strip: Move / Resize / Customize
	mode_ = leftCol->Add(new ChoiceStrip(ORIENT_VERTICAL));
	mode_->AddChoice(di->T("Move"), ImageID("I_MOVE"));
	mode_->AddChoice(di->T("Resize"), ImageID("I_RESIZE"));
	mode_->AddChoice(co->T("Customize"), ImageID("I_EDIT"));
	mode_->SetSelection(0, false);
	mode_->OnChoice.Add([this](EventParams &) {
		if (layoutView_) layoutView_->mode_ = mode_->GetSelection();
	});

	// Border/Garis pinggir (toggles filled↔outline style)
	{
		bool initialBorder = g_Config.iTouchButtonStyle != 0;
		CheckBox *border = new CheckBox(&borderState_, di->T("Border"));
		border->OnClick.Add([this](EventParams &) {
			g_Config.iTouchButtonStyle = borderState_ ? 1 : 0;
			RecreateViews();
		});
		leftCol->Add(border);
	}

	// Snap to Grid + Grid size
	CheckBox *snap = new CheckBox(&g_Config.bTouchSnapToGrid, di->T("Snap"));
	leftCol->Add(snap);
	PopupSliderChoice *gridSize = new PopupSliderChoice(&g_Config.iTouchSnapGridSize, 2, 256, 64, di->T("Grid"), screenManager(), "");
	gridSize->SetEnabledPtr(&g_Config.bTouchSnapToGrid);
	leftCol->Add(gridSize);

	// Reset
	leftCol->Add(new Spacer(8.0f));
	leftCol->Add(new Choice(di->T("Reset")))->OnClick.Handle(this, &CoreTouchLayoutScreen::OnReset);

	// Back
	leftCol->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Add([this](EventParams &) {
		EmuCore::SaveTouchConfig(coreType_);
		TriggerFinish(DR_CANCEL);
	});
	leftScroll->SetShadows(false);

	// Right column — interactive layout
	auto *rightCol = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(0.0f, 12.0f, 12.0f, 12.0f))));
	rightCol->Add(new TextView(di->T("Landscape")))->SetTextSize(TextSize::Small);
	rightCol->Add(new Spacer(new LinearLayoutParams(1.0f)));
	float previewH = bounds.h * g_layoutScale;
	layoutView_ = rightCol->Add(new GBALayoutView(coreType_, false, new LinearLayoutParams(FILL_PARENT, previewH)));
}

void CoreTouchLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	EmuCore::LoadTouchConfig(coreType_);
	RecreateViews();
}

void CoreTouchLayoutScreen::onFinish(DialogResult) {
	INFO_LOG(Log::System, "[TOUCH] CoreTouchLayoutScreen closed for %s", EmuCore::GetConfigSection(coreType_));
}

void CoreTouchLayoutScreen::resized() { RecreateViews(); }

void CoreTouchLayoutScreen::update() {
	UIBaseDialogScreen::update();
	if (!layoutView_) return;
	if (!layoutView_->HasCreatedViews())
		layoutView_->CreateViews();
}

void CoreTouchLayoutScreen::OnReset(UI::EventParams &) {
	EmuCore::InitDefaultTouchConfigs();
	EmuCore::SaveTouchConfig(coreType_);
	RecreateViews();
}
