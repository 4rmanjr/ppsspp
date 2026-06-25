// [PPSSPP-FORK] MultiCore: Generic per-core touch layout editor
// Interactive drag-and-drop editor for GBA (and future cores).
// Mirrors TouchControlLayoutScreen behavior.
// Jangan hapus, jangan ubah kode upstream.

#include "UI/CoreTouchLayoutScreen.h"
#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/UI.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Log.h"
#include "Common/System/System.h"
#include "UI/GamepadEmu.h"

static float g_layoutScale = 0.8f;

// [PPSSPP-FORK] CoreDragDrop: draggable/resizable button in per-core layout editor
// Mirrors PSP MultiTouchButton behavior for PSP's TouchControlLayoutScreen
class CoreDragDrop : public MultiTouchButton {
public:
	CoreDragDrop(EmuCore::CoreTouchButton &btn, const Bounds &screenBounds, ImageID bgImg, ImageID img)
		: MultiTouchButton("gba_dd", bgImg, bgImg, img, 1.0f,
			new UI::AnchorLayoutParams(btn.x * screenBounds.w, btn.y * screenBounds.h, UI::NONE, UI::NONE, UI::Centering::Both)),
		  btn_(btn), screenBounds_(screenBounds), bgImg_(bgImg) {}

	bool IsDownVisually() const override { return false; }

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(bgImg_);
		if (image && image->w > 0 && screenBounds_.w > 0.0f) {
			float desiredW = btn_.w * screenBounds_.w;
			float scale = desiredW / (float)image->w;
			w = image->w * scale;
			h = image->h * scale;
		} else {
			w = 0;
			h = 0;
		}
	}

	void Draw(UIContext &dc) override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(bgImg_);
		if (image && image->w > 0 && screenBounds_.w > 0.0f) {
			float desiredW = btn_.w * screenBounds_.w;
			scale_ = desiredW / (float)image->w;
		}
		MultiTouchButton::Draw(dc);
	}

	void SavePosition() {
		btn_.x = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		btn_.y = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
	}
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
	Bounds screenBounds_;  // value copy — safe even if parent view is destroyed
	ImageID bgImg_;
};

// [PPSSPP-FORK] CoreSnapGrid: Snap/grid lines drawn behind touch buttons
// Mirrors PSP SnapGrid behavior but adapted for per-core layout editor
class CoreSnapGrid : public UI::View {
public:
	CoreSnapGrid(u32 color) : UI::View(), col(color) {}
	void Draw(UIContext &dc) override {
		if (g_Config.bTouchSnapToGrid && g_Config.iTouchSnapGridSize >= 2) {
			dc.Flush();
			dc.BeginNoTex();
			float xOff = bounds_.x;
			float yOff = bounds_.y;
			int w = (int)bounds_.w;
			int h = (int)bounds_.h;
			int spacing = g_Config.iTouchSnapGridSize;

			// Center crosshair
			dc.Draw()->vLine(w * 0.5f + xOff, yOff, yOff + h, col);
			dc.Draw()->hLine(xOff, h * 0.5f + yOff, xOff + w, col);

			// Grid lines — offset so one line passes through center
			int halfW = w / 2;
			int halfH = h / 2;
			for (int x = halfW % spacing; x < w; x += spacing)
				dc.Draw()->vLine((float)x + xOff, yOff, yOff + h, col);
			for (int y = halfH % spacing; y < h; y += spacing)
				dc.Draw()->hLine(xOff, (float)y + yOff, xOff + w, col);

			dc.Flush();
			dc.Begin();
		}
	}
	std::string DescribeText() const override { return ""; }
private:
	u32 col;
};

// [PPSSPP-FORK] CoreLayoutView: layout preview container for per-core editor
// Manages drag-drop controls, grid overlay, portrait/landscape mode
class CoreLayoutView : public UI::AnchorLayout {
public:
	CoreLayoutView(EmuCore::Type core, bool portrait, UI::LayoutParams *lp)
		: UI::AnchorLayout(lp) { SetClip(true); coreType_ = core; portrait_ = portrait; }

	bool Touch(const TouchInput &input) override;
	void CreateViews();
	void Draw(UIContext &dc) override;
	void SetPortrait(bool portrait);
	bool HasCreatedViews() const { return !controls_.empty(); }

	int mode_ = 0;

private:
	void ClearControls();

	CoreDragDrop *picked_ = nullptr;
	float startObjX_ = -1.0f, startObjY_ = -1.0f;
	float startDragX_ = -1.0f, startDragY_ = -1.0f;
	float startScale_ = -1.0f;
	EmuCore::Type coreType_;
	std::vector<CoreDragDrop *> controls_;

public:
	bool portrait_ = false;
};

bool CoreLayoutView::Touch(const TouchInput &touch) {
	using namespace UI;

	if ((touch.flags & TouchInputFlags::MOVE) && picked_) {
		if (mode_ == 0) {
			// Clamp to preview area (matching PSP behavior)
			Bounds vr = this->GetBounds();
			vr.x = 0.0f;
			vr.y = 0.0f;
			float nx = startObjX_ + (touch.x - startDragX_);
			float ny = startObjY_ + (touch.y - startDragY_);
			if (g_Config.bTouchSnapToGrid && g_Config.iTouchSnapGridSize > 0) {
				float grid = (float)g_Config.iTouchSnapGridSize;
				float cx = vr.centerX();
				float cy = vr.centerY();
				// Snap relative to center (matching PSP snap anchoring)
				nx -= fmod(nx - cx, grid);
				ny -= fmod(ny - cy, grid);
			}
			nx = std::clamp(nx, 0.0f, vr.w);
			ny = std::clamp(ny, 0.0f, vr.h);
			picked_->ReplaceLayoutParams(new AnchorLayoutParams(nx, ny, NONE, NONE, Centering::Both));
		} else if (mode_ == 1) {
			// btn_.w is normalized width (fraction of screen, default ~0.10).
			// Resize range [0.03, 0.30] = 3%-30% of screen width.
			float diff = -(touch.y - startDragY_) * 0.0015f;
			float ns = startScale_ + diff;
			if (ns < 0.03f) ns = 0.03f;
			if (ns > 0.30f) ns = 0.30f;
			picked_->SetScaleVal(ns);
		}
	}
	if ((touch.flags & TouchInputFlags::DOWN) && !picked_) {
		for (auto *c : controls_) {
			if (c->Contains(touch.x, touch.y)) {
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

void CoreLayoutView::Draw(UIContext &dc) {
	using namespace UI;
	dc.FillRect(Drawable(0x80000000), bounds_);
	AnchorLayout::Draw(dc);
}

void CoreLayoutView::ClearControls() {
	for (auto *c : controls_) RemoveSubview(c);
	controls_.clear();
}

void CoreLayoutView::SetPortrait(bool portrait) {
	if (portrait_ == portrait) return;
	portrait_ = portrait;
	ClearControls();
	CreateViews();
}

void CoreLayoutView::CreateViews() {
	const Bounds &b = GetBounds();
	if (b.w == 0.0f || b.h == 0.0f) return;

	auto &cfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);

	// Grid lines drawn behind buttons (mirrors PSP SnapGrid z-order)
	Add(new CoreSnapGrid(0x3FFFFFFF));

	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		if (!btn.visible) continue;
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
		auto *dd = new CoreDragDrop(btn, b, bg, icon);
		controls_.push_back(dd);
		Add(dd);
	}
}

// [PPSSPP-FORK] CoreCheckBoxChoice — wraps a CheckBox inside a Choice
// Mirrors PSP TouchControlVisibilityScreen pattern for row-based visibility toggles
class CoreCheckBoxChoice : public UI::Choice {
public:
	CoreCheckBoxChoice(std::string_view text, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(text, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CoreCheckBoxChoice::HandleClick);
	}
	CoreCheckBoxChoice(ImageID imgID, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(imgID, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CoreCheckBoxChoice::HandleClick);
	}
private:
	void HandleClick(UI::EventParams &e) { checkbox_->Toggle(); }
	UI::CheckBox *checkbox_;
};

// [PPSSPP-FORK] CoreTouchVisibilityPopup: per-button visibility toggle popup
// Mirrors PSP's TouchControlVisibilityScreen with icons + Toggle All
class CoreTouchVisibilityPopup : public UI::PopupScreen {
public:
	CoreTouchVisibilityPopup(EmuCore::Type coreType, bool portrait)
		: UI::PopupScreen("GBA Touch Control Visibility"), coreType_(coreType), portrait_(portrait) {}

	const char *tag() const override { return "CoreTouchVisibilityPopup"; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto co = GetI18NCategory(I18NCat::CONTROLS);
		auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		auto &cfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);
		parent->Add(new ItemHeader(di->T("Show Buttons")));

		struct ToggleInfo {
			const char *label;
			ImageID icon;
			bool *show;
		};
		std::vector<ToggleInfo> toggles;

		for (int i = 0; i < cfg.count; i++) {
			auto &btn = cfg.buttons[i];
			const char *label = nullptr;
			ImageID icon;
			switch (btn.keyCode) {
			case CTRL_CROSS:     label = "A";     icon = ImageID("I_CROSS");   break;
			case CTRL_CIRCLE:    label = "B";     icon = ImageID("I_CIRCLE");  break;
			case CTRL_SELECT:    label = "Select"; icon = ImageID("I_SELECT"); break;
			case CTRL_START:     label = "Start";  icon = ImageID("I_START");  break;
			case CTRL_LTRIGGER:  label = "L";      icon = ImageID("I_L");      break;
			case CTRL_RTRIGGER:  label = "R";      icon = ImageID("I_R");      break;
			case CTRL_UP:        label = "D-Pad Up";   icon = ImageID("I_ARROW"); break;
			case CTRL_DOWN:      label = "D-Pad Down";  icon = ImageID("I_ARROW"); break;
			case CTRL_LEFT:      label = "D-Pad Left";  icon = ImageID("I_ARROW"); break;
			case CTRL_RIGHT:     label = "D-Pad Right"; icon = ImageID("I_ARROW"); break;
			}
			if (!label) continue;
			toggles.push_back({label, icon, &btn.visible});
		}

		const int cellSize = 380;
		GridLayoutSettings gridsettings(cellSize, 52, 3);
		gridsettings.fillCells = true;
		GridLayout *grid = parent->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

		for (auto &t : toggles) {
			LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			row->SetSpacing(0);

			CheckBox *cb = new CheckBox(t.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
			row->Add(cb);

			Choice *choice;
			if (t.icon.isValid()) {
				choice = new CoreCheckBoxChoice(t.icon, cb, new LinearLayoutParams(1.0f));
			} else {
				choice = new CoreCheckBoxChoice(mc->T(t.label), cb, new LinearLayoutParams(1.0f));
			}
			choice->SetCentered(true);
			row->Add(choice);
			grid->Add(row);
		}

		parent->Add(new Spacer(8.0f));

		// System buttons (Fast-forward, Pause) — from main TouchControlConfig (shared with PSP)
		parent->Add(new ItemHeader(di->T("System Buttons")));
		DeviceOrientation orient = portrait_ ? DeviceOrientation::Portrait : DeviceOrientation::Landscape;
		TouchControlConfig &touchCfg = g_Config.GetTouchControlsConfig(orient);

		// Fast-forward
		{
			auto *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			row->SetSpacing(0);
			CheckBox *cb = new CheckBox(&touchCfg.touchFastForwardKey.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
			row->Add(cb);
			Choice *choice = new CoreCheckBoxChoice(ImageID("I_FAST_FORWARD_LINE"), cb, new LinearLayoutParams(1.0f));
			choice->SetCentered(true);
			row->Add(choice);
			parent->Add(row);
		}
		// Pause
		{
			auto *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			row->SetSpacing(0);
			CheckBox *cb = new CheckBox(&touchCfg.touchPauseKey.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
			row->Add(cb);
			bool hasBackButton = System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON);
			if (!hasBackButton) {
				cb->SetEnabled(false);
			}
			Choice *choice = new CoreCheckBoxChoice(ImageID("I_HAMBURGER"), cb, new LinearLayoutParams(1.0f));
			if (!hasBackButton) {
				choice->SetEnabled(false);
			}
			choice->SetCentered(true);
			row->Add(choice);
			parent->Add(row);
		}

		parent->Add(new Spacer(8.0f));

		// Toggle All — matches PSP TouchControlVisibilityScreen context menu
		// nextToggleAll_ starts true: first click toggles ALL ON (show all), second click ALL OFF
		auto *toggleAll = parent->Add(new Choice(di->T("Toggle All")));
		toggleAll->OnClick.Add([this](UI::EventParams &e) {
			auto &tcfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);
			for (int i = 0; i < tcfg.count; i++) {
				tcfg.buttons[i].visible = nextToggleAll_;
			}
			// Also toggle system buttons (Fast-forward, Pause) — match PSP Toggle All
			DeviceOrientation orient = portrait_ ? DeviceOrientation::Portrait : DeviceOrientation::Landscape;
			TouchControlConfig &tcfgSys = g_Config.GetTouchControlsConfig(orient);
			tcfgSys.touchFastForwardKey.show = nextToggleAll_;
			if (System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON)) {
				tcfgSys.touchPauseKey.show = nextToggleAll_;
			}
			// (Pause disabled on back-button-less devices — InitPadLayout forces it visible)
			nextToggleAll_ = !nextToggleAll_;
		});

		parent->Add(new Choice(di->T("OK")))->OnClick.Add([this](UI::EventParams &) {
			TriggerFinish(DR_OK);
		});
	}

	void OnCompleted(DialogResult result) override {
		// Save on all exit paths (OK, Cancel, back button) — matches PSP TouchControlVisibilityScreen::onFinish
		EmuCore::SaveTouchConfig(coreType_);
	}

private:
	EmuCore::Type coreType_;
	bool portrait_;
	bool nextToggleAll_ = true;
};

void CoreTouchLayoutScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	EmuCore::LoadTouchConfig(coreType_);
	const char *coreName = EmuCore::GetConfigSection(coreType_);
	const Bounds &bounds = GetLayoutBounds(*screenManager()->getUIContext());
	const float leftW = 200.0f;
	g_layoutScale = 1.0f - (leftW + 10.0f) / std::max(bounds.w, 1.0f);

	const DeviceOrientation orientation = GetDeviceOrientation();
	bool portrait = (orientation == DeviceOrientation::Portrait);

	auto *rootLayout = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	rootLayout->SetSpacing(0.0f);
	root_ = rootLayout;

	// Left sidebar
	auto *leftScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(leftW, FILL_PARENT)));
	leftScroll->SetAlignOpposite(true);
	auto *leftCol = leftScroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(12.0f, 0.0f))));
	leftCol->Add(new ItemHeader(std::string(coreName) + " Touch Layout"));

	// Mode: Pindah / Atur Ulang Ukuran
	mode_ = leftCol->Add(new ChoiceStrip(ORIENT_VERTICAL));
	mode_->AddChoice(di->T("Move"), ImageID("I_MOVE"));
	mode_->AddChoice(di->T("Resize"), ImageID("I_RESIZE"));
	mode_->SetSelection(0, false);
	mode_->OnChoice.Add([this](EventParams &) {
		if (layoutView_) layoutView_->mode_ = mode_->GetSelection();
	});

	// Kustomisasi — buka popup visibility (respect current orientation)
	leftCol->Add(new Choice(co->T("Customize")))->OnClick.Add([this](EventParams &) {
		bool p = layoutView_ ? layoutView_->portrait_ : false;
		screenManager()->push(new CoreTouchVisibilityPopup(coreType_, p));
	});

	// Snap/Garis Pinggir — enable/disable grid
	leftCol->Add(new CheckBox(&g_Config.bTouchSnapToGrid, di->T("Snap")));

	// Grid/Kisi-kisi — enabled only if Snap is checked
	PopupSliderChoice *gridSize = new PopupSliderChoice(&g_Config.iTouchSnapGridSize, 2, 256, 64, di->T("Grid"), screenManager(), "");
	gridSize->SetEnabledPtr(&g_Config.bTouchSnapToGrid);
	leftCol->Add(gridSize);

	// Atur Ulang
	leftCol->Add(new Spacer(8.0f));
	leftCol->Add(new Choice(di->T("Reset")))->OnClick.Handle(this, &CoreTouchLayoutScreen::OnReset);

	// Kembali
	leftCol->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Add([this](EventParams &) {
		TriggerFinish(DR_CANCEL);
	});
	leftScroll->SetShadows(false);

	// Panel kanan — preview layout
	auto *rightCol = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(0.0f, 12.0f, 12.0f, 12.0f))));
	rightCol->Add(new TextView(co->T(DeviceOrientationToString(orientation))))->SetTextSize(TextSize::Small);
	rightCol->Add(new Spacer(new LinearLayoutParams(1.0f)));
	float previewH = bounds.h * g_layoutScale;
	if (previewH > bounds.h * 0.85f) previewH = bounds.h * 0.85f;
	if (previewH < 100.0f) previewH = 100.0f;
	layoutView_ = rightCol->Add(new CoreLayoutView(coreType_, portrait, new LinearLayoutParams(FILL_PARENT, previewH)));
}

void CoreTouchLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	EmuCore::LoadTouchConfig(coreType_);
	RecreateViews();
}

void CoreTouchLayoutScreen::onFinish(DialogResult) {
	EmuCore::SaveTouchConfig(coreType_);
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
