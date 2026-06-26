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

// [PPSSPP-FORK] CoreDragDropBase: abstract base for draggable/resizable controls
// Supports both single buttons (CoreDragDrop) and grouped controls (GBADPadGroup, GBAActionGroup)
class CoreDragDropBase : public MultiTouchButton {
public:
	CoreDragDropBase(const char *tag, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *lp)
		: MultiTouchButton(tag, bgImg, bgDownImg, img, scale, lp) {}
	virtual ~CoreDragDropBase() = default;
	virtual void SavePosition() = 0;
	virtual float GetScaleVal() const = 0;
	virtual void SetScaleVal(float s) = 0;
	virtual bool Contains(float x, float y) {
		return bounds_.Contains(x, y);
	}
};

// [PPSSPP-FORK] CoreDragDrop: draggable/resizable SINGLE button in per-core layout editor
// Mirrors PSP MultiTouchButton behavior for PSP's TouchControlLayoutScreen
class CoreDragDrop : public CoreDragDropBase {
public:
	CoreDragDrop(EmuCore::CoreTouchButton &btn, const Bounds &screenBounds, ImageID bgImg, ImageID img)
		: CoreDragDropBase("gba_dd", bgImg, bgImg, img, 1.0f,
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
	bool Contains(float x, float y) override {
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

// [PPSSPP-FORK] GBADPadGroup: grouped D-pad control matching PSP PSPDPadButtons
// Wraps 4 individual CoreTouchButton directions into one draggable widget.
// Jangan hapus, jangan ubah kode upstream.
class GBADPadGroup : public CoreDragDropBase {
public:
	GBADPadGroup(EmuCore::CoreTouchButton *btns[4], const Bounds &screenBounds)
		: CoreDragDropBase("gba_dpad", ImageID::invalid(), ImageID::invalid(), ImageID::invalid(), 1.0f,
			new UI::AnchorLayoutParams(
				((btns[0]->x + btns[1]->x + btns[2]->x + btns[3]->x) / 4.0f) * screenBounds.w,
				((btns[0]->y + btns[1]->y + btns[2]->y + btns[3]->y) / 4.0f) * screenBounds.h,
				UI::NONE, UI::NONE, UI::Centering::Both)),
		  screenBounds_(screenBounds) {
		for (int i = 0; i < 4; i++)
			btns_[i] = btns[i];
		// Identify direction indices via keyCode
		for (int i = 0; i < 4; i++) {
			switch (btns_[i]->keyCode) {
				case CTRL_LEFT:  left_ = i; break;
				case CTRL_RIGHT: right_ = i; break;
				case CTRL_UP:    up_ = i; break;
				case CTRL_DOWN:  down_ = i; break;
			}
		}
		// Compute spacing as average distance from center
		float cx = 0, cy = 0;
		for (int i = 0; i < 4; i++) { cx += btns_[i]->x; cy += btns_[i]->y; }
		cx /= 4.0f; cy /= 4.0f;
		float d = 0; int n = 0;
		float hl = fabs(btns_[left_]->x - cx);
		float hr = fabs(btns_[right_]->x - cx);
		float vu = fabs(btns_[up_]->y - cy);
		float vd = fabs(btns_[down_]->y - cy);
		if (hl > 0.001f) { d += hl; n++; }
		if (hr > 0.001f) { d += hr; n++; }
		if (vu > 0.001f) { d += vu; n++; }
		if (vd > 0.001f) { d += vd; n++; }
		spacing_ = (n > 0) ? d / n : 0.05f;
	}

	bool IsDownVisually() const override { return false; }

	void Draw(UIContext &dc) override {
		ImageID dirImg = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(dirImg);
		if (!image || image->w == 0) return;

		float desiredW = spacing_ * screenBounds_.w;
		scale_ = desiredW / (float)image->w;

		float cx = bounds_.centerX();
		float cy = bounds_.centerY();
		float sp = spacing_ * screenBounds_.w;
		uint32_t color = 0xFFFFFF;
		uint32_t colorBg = 0xFFFFFF;

		// xoff/yoff order: RIGHT, DOWN, LEFT, UP (matching PSP PSPDPadButtons)
		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		for (int i = 0; i < 4; i++) {
			float x = cx + xoff[i] * sp;
			float y = cy + yoff[i] * sp;
			float angle = i * (float)M_PI / 2.0f;

			dc.Draw()->DrawImageRotated(dirImg, x, y, scale_, angle + (float)M_PI, colorBg, false);

			float ax = cx + xoff[i] * (sp + 10.0f * scale_);
			float ay = cy + yoff[i] * (sp + 10.0f * scale_);
			dc.Draw()->DrawImageRotated(ImageID("I_ARROW"), ax, ay, scale_, angle + (float)M_PI, color);
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(
			g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR"));
		float iw = image ? (float)image->w : 40.0f;
		float s = (spacing_ * screenBounds_.w) / iw;
		w = 2.0f * (spacing_ * screenBounds_.w) + iw * s;
		h = w;
	}

	void SavePosition() override {
		float cx = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		float cy = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
		float sp = spacing_;
		btns_[left_]->x = cx - sp;
		btns_[left_]->y = cy;
		btns_[right_]->x = cx + sp;
		btns_[right_]->y = cy;
		btns_[up_]->x = cx;
		btns_[up_]->y = cy - sp;
		btns_[down_]->x = cx;
		btns_[down_]->y = cy + sp;
	}

	float GetScaleVal() const override { return spacing_; }
	void SetScaleVal(float s) override { if (s >= 0.01f && s <= 0.50f) spacing_ = s; }

	bool Contains(float x, float y) override {
		const float t = 0.25f;
		Bounds tb(bounds_.x - t * bounds_.w * 0.5f, bounds_.y - t * bounds_.h * 0.5f,
			bounds_.w * (1.0f + t), bounds_.h * (1.0f + t));
		return tb.Contains(x, y);
	}

private:
	EmuCore::CoreTouchButton *btns_[4];
	Bounds screenBounds_;
	int left_ = 0, right_ = 1, up_ = 2, down_ = 3;
	float spacing_ = 0.05f;
};

// [PPSSPP-FORK] GBAActionGroup: grouped GBA action buttons (A/B) matching PSP PSPActionButtons
// Wraps 2 CoreTouchButton (CTRL_CROSS=A, CTRL_CIRCLE=B) into one draggable widget.
// Jangan hapus, jangan ubah kode upstream.
class GBAActionGroup : public CoreDragDropBase {
public:
	GBAActionGroup(EmuCore::CoreTouchButton *btns[2], const Bounds &screenBounds)
		: CoreDragDropBase("gba_action", ImageID::invalid(), ImageID::invalid(), ImageID::invalid(), 1.0f,
			new UI::AnchorLayoutParams(
				((btns[0]->x + btns[1]->x) / 2.0f) * screenBounds.w,
				((btns[0]->y + btns[1]->y) / 2.0f) * screenBounds.h,
				UI::NONE, UI::NONE, UI::Centering::Both)),
		  screenBounds_(screenBounds) {
		btns_[0] = btns[0]; // A (CTRL_CROSS)
		btns_[1] = btns[1]; // B (CTRL_CIRCLE)
		// Compute spacing as distance between A and B centers
		float dx = fabs(btns_[1]->x - btns_[0]->x);
		float dy = fabs(btns_[1]->y - btns_[0]->y);
		spacing_ = (dx + dy > 0.001f) ? (dx + dy) * 0.5f : 0.05f;
	}

	bool IsDownVisually() const override { return false; }

	void Draw(UIContext &dc) override {
		ImageID roundBg = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(roundBg);
		if (!image || image->w == 0) return;

		float desiredW = spacing_ * screenBounds_.w;
		scale_ = desiredW / (float)image->w;

		float cx = bounds_.centerX();
		float cy = bounds_.centerY();
		float sp = spacing_ * screenBounds_.w;
		uint32_t color = 0xFFFFFF;
		uint32_t colorBg = 0xFFFFFF;

		// B (CTRL_CIRCLE) on right, A (CTRL_CROSS) below (matching PSP action button quadrant)
		// Right: cx + sp, cy
		dc.Draw()->DrawImageRotated(roundBg, cx + sp, cy, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(ImageID("I_CIRCLE"), cx + sp, cy, scale_, 0, color, false);

		// Below: cx, cy + sp
		dc.Draw()->DrawImageRotated(roundBg, cx, cy + sp, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(ImageID("I_CROSS"), cx, cy + sp, scale_, 0, color, false);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(
			g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND"));
		float iw = image ? (float)image->w : 40.0f;
		float s = (spacing_ * screenBounds_.w) / iw;
		w = 2.0f * (spacing_ * screenBounds_.w) + iw * s;
		h = 2.0f * (spacing_ * screenBounds_.w) + iw * s;
	}

	void SavePosition() override {
		float cx = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		float cy = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
		float sp = spacing_;
		// B on right, A below
		btns_[1]->x = cx + sp;  // B (CTRL_CIRCLE)
		btns_[1]->y = cy;
		btns_[0]->x = cx;       // A (CTRL_CROSS)
		btns_[0]->y = cy + sp;
	}

	float GetScaleVal() const override { return spacing_; }
	void SetScaleVal(float s) override { if (s >= 0.01f && s <= 0.50f) spacing_ = s; }

	bool Contains(float x, float y) override {
		const float t = 0.25f;
		Bounds tb(bounds_.x - t * bounds_.w * 0.5f, bounds_.y - t * bounds_.h * 0.5f,
			bounds_.w * (1.0f + t), bounds_.h * (1.0f + t));
		return tb.Contains(x, y);
	}

private:
	EmuCore::CoreTouchButton *btns_[2];
	Bounds screenBounds_;
	float spacing_ = 0.05f;
};

// [PPSSPP-FORK] CoreSnapGrid: draw grid lines on top of buttons (matching PSP SnapGrid z-order)
// Instead of a child View, called from CoreLayoutView::Draw() to avoid 10x10 measurement issue
// PSP SnapGrid is added last in CreateViews (on top of buttons) — we replicate that here
static void DrawCoreSnapGrid(UIContext &dc, const Bounds &bounds, u32 col) {
	if (g_Config.bTouchSnapToGrid && g_Config.iTouchSnapGridSize >= 2) {
		dc.Flush();
		dc.BeginNoTex();
		float xOff = bounds.x;
		float yOff = bounds.y;
		int spacing = g_Config.iTouchSnapGridSize;

		float x1 = 0.0f, x2 = bounds.w, y1 = 0.0f, y2 = bounds.h;

		// Center crosshair (thicker, matching PSP)
		dc.Draw()->Rect((x1+x2)*0.5f + xOff - g_display.pixel_in_dps_x, y1 + yOff, 3.0f * g_display.pixel_in_dps_x, y2 - y1, col);
		dc.Draw()->Rect(x1 + xOff, (y1+y2)*0.5f + yOff - g_display.pixel_in_dps_y, x2 - x1, 3.0f * g_display.pixel_in_dps_y, col);

		// Grid lines — offset so one line passes through center (matching PSP formula)
		int ix1 = (int)x1, ix2 = (int)x2, iy1 = (int)y1, iy2 = (int)y2;
		int centerOffX = (ix1 + ix2) / 2 % spacing;
		int centerOffY = (iy1 + iy2) / 2 % spacing;
		for (int x = ix1 + centerOffX; x < ix2; x += spacing)
			dc.Draw()->vLine((float)x + xOff, y1 + yOff, y2 + yOff, col);
		for (int y = iy1 + centerOffY; y < iy2; y += spacing)
			dc.Draw()->hLine(x1 + xOff, (float)y + yOff, x2 + xOff, col);

		dc.Flush();
		dc.Begin();
	}
}

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
	bool HasCreatedViews() const { return created_; }

	int mode_ = 0;

private:
	void ClearControls();

	CoreDragDropBase *picked_ = nullptr;
	float startObjX_ = -1.0f, startObjY_ = -1.0f;
	float startDragX_ = -1.0f, startDragY_ = -1.0f;
	float startScale_ = -1.0f;
	EmuCore::Type coreType_;
	std::vector<CoreDragDropBase *> controls_;
	bool created_ = false;

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
	// [PPSSPP-FORK] Draw grid on top of buttons (matching PSP SnapGrid z-order)
	DrawCoreSnapGrid(dc, bounds_, 0x3FFFFFFF);
}

void CoreLayoutView::ClearControls() {
	for (auto *c : controls_) RemoveSubview(c);
	controls_.clear();
	created_ = false;
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

	// First pass: identify buttons for grouped controls (D-pad, action buttons)
	// matching PSP's PSPDPadButtons and PSPActionButtons
	EmuCore::CoreTouchButton *dpadUp = nullptr, *dpadDown = nullptr;
	EmuCore::CoreTouchButton *dpadLeft = nullptr, *dpadRight = nullptr;
	EmuCore::CoreTouchButton *btnA = nullptr, *btnB = nullptr;

	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		if (!btn.visible) continue;
		switch (btn.keyCode) {
		case CTRL_UP:    dpadUp = &btn; break;
		case CTRL_DOWN:  dpadDown = &btn; break;
		case CTRL_LEFT:  dpadLeft = &btn; break;
		case CTRL_RIGHT: dpadRight = &btn; break;
		case CTRL_CROSS: btnA = &btn; break;
		case CTRL_CIRCLE: btnB = &btn; break;
		default: break;
		}
	}

	// Create D-pad group if all 4 directions visible (matching PSP PSPDPadButtons)
	bool hasDpadGroup = (dpadUp && dpadDown && dpadLeft && dpadRight);
	if (hasDpadGroup) {
		EmuCore::CoreTouchButton *dpadBtns[4] = {dpadLeft, dpadRight, dpadUp, dpadDown};
		auto *dd = new GBADPadGroup(dpadBtns, b);
		controls_.push_back(dd);
		Add(dd);
	}

	// Create Action group if both A and B visible (matching PSP PSPActionButtons)
	bool hasActionGroup = (btnA && btnB);
	if (hasActionGroup) {
		EmuCore::CoreTouchButton *actBtns[2] = {btnA, btnB};
		auto *ag = new GBAActionGroup(actBtns, b);
		controls_.push_back(ag);
		Add(ag);
	}

	// Second pass: remaining buttons as individual CoreDragDrop
	// [PPSSPP-FORK] Set created_ flag here so HasCreatedViews() works
	// even if all buttons are hidden (prevents infinite update() loop).
	created_ = true;
	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		if (!btn.visible) continue;
		// Skip buttons already handled by groups
		if (hasDpadGroup && (btn.keyCode == CTRL_UP || btn.keyCode == CTRL_DOWN ||
			btn.keyCode == CTRL_LEFT || btn.keyCode == CTRL_RIGHT)) continue;
		if (hasActionGroup && (btn.keyCode == CTRL_CROSS || btn.keyCode == CTRL_CIRCLE)) continue;

		ImageID icon;
		switch (btn.keyCode) {
		case CTRL_CROSS:     icon = ImageID("I_CROSS");   break;
		case CTRL_CIRCLE:    icon = ImageID("I_CIRCLE");  break;
		case CTRL_SELECT:    icon = ImageID("I_SELECT");  break;
		case CTRL_START:     icon = ImageID("I_START");   break;
		case CTRL_LTRIGGER:  icon = ImageID("I_L");      break;
		case CTRL_RTRIGGER:  icon = ImageID("I_R");      break;
		case CTRL_UP:        icon = ImageID("I_ARROW_UP");    break;
		case CTRL_DOWN:      icon = ImageID("I_ARROW_DOWN");  break;
		case CTRL_LEFT:      icon = ImageID("I_ARROW_LEFT");  break;
		case CTRL_RIGHT:     icon = ImageID("I_ARROW_RIGHT"); break;
		default:             icon = ImageID("I_CROSS");   break;
		}
		ImageID roundImg = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
		ImageID rectImg = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
		ImageID shoulderImg = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
		ImageID bg = (btn.keyCode == CTRL_LTRIGGER || btn.keyCode == CTRL_RTRIGGER) ? shoulderImg : roundImg;
		if (btn.keyCode == CTRL_SELECT || btn.keyCode == CTRL_START) bg = rectImg;
		auto *dd = new CoreDragDrop(btn, b, bg, icon);
		// [PPSSPP-FORK] PSP parity: flip R trigger horizontally (matching PSP TouchControlLayoutScreen)
		if (btn.keyCode == CTRL_RTRIGGER) dd->FlipImageH(true);
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
			case CTRL_UP:        label = "D-Pad Up";   icon = ImageID("I_ARROW_UP"); break;
			case CTRL_DOWN:      label = "D-Pad Down";  icon = ImageID("I_ARROW_DOWN"); break;
			case CTRL_LEFT:      label = "D-Pad Left";  icon = ImageID("I_ARROW_LEFT"); break;
			case CTRL_RIGHT:     label = "D-Pad Right"; icon = ImageID("I_ARROW_RIGHT"); break;
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
