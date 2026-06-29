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
#include "UI/SimpleDialogScreen.h"

static float g_layoutScale = 0.8f;

// [PPSSPP-FORK] CoreDragDropBase: abstract base for draggable/resizable controls
// D-pad uses CoreDPadGroup (grouped). Other buttons use individual CoreDragDrop.
class CoreDragDropBase : public MultiTouchButton {
public:
	CoreDragDropBase(const char *tag, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *lp)
		: MultiTouchButton(tag, bgImg, bgDownImg, img, scale, lp) {}
	virtual ~CoreDragDropBase() = default;
	virtual void SavePosition() = 0;
	virtual float GetScaleVal() const = 0;
	virtual void SetScaleVal(float s) = 0;
	virtual float GetSpacingVal() const { return 0.0f; }
	virtual void SetSpacingVal(float s) { }
	virtual bool Contains(float x, float y) = 0;
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

	void SavePosition() override {
		btn_.x = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		btn_.y = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
	}
	float GetScaleVal() const override { return btn_.w; }
	void SetScaleVal(float s) override { btn_.w = btn_.h = s; }
	bool Contains(float x, float y) override {
		// Minimum touch target size (matching mobile accessibility standards, e.g., 80 pixels/dps)
		// to make picking smaller buttons easier in the preview editor container.
		float minTarget = 80.0f;
		float targetW = std::max(bounds_.w * 1.25f, minTarget);
		float targetH = std::max(bounds_.h * 1.25f, minTarget);
		float cx = bounds_.centerX();
		float cy = bounds_.centerY();
		Bounds tb(cx - targetW * 0.5f, cy - targetH * 0.5f, targetW, targetH);
		return tb.Contains(x, y);
	}
private:
	EmuCore::CoreTouchButton &btn_;
	Bounds screenBounds_;  // value copy — safe even if parent view is destroyed
	ImageID bgImg_;
};

// [PPSSPP-FORK] MultiCore: CoreDPadGroup — grouped D-pad for GBA preview editor
// Mirrors PSP's PSPDPadButtons: renders 4 directional arrows in a cross pattern.
// Single draggable/resizable unit that manages 4 CoreTouchButton configs.
class CoreDPadGroup : public CoreDragDropBase {
public:
	CoreDPadGroup(EmuCore::CoreTouchButton &up, EmuCore::CoreTouchButton &down,
				  EmuCore::CoreTouchButton &left, EmuCore::CoreTouchButton &right,
				  float spacing, EmuCore::CoreTouchConfig &cfg, const Bounds &screenBounds)
		: CoreDragDropBase("gba_dpad", ImageID::invalid(), ImageID::invalid(), ImageID::invalid(), 1.0f,
			new UI::AnchorLayoutParams(
				((up.x + down.x + left.x + right.x) * 0.25f) * screenBounds.w,
				((up.y + down.y + left.y + right.y) * 0.25f) * screenBounds.h,
				UI::NONE, UI::NONE, UI::Centering::Both)),
		  up_(up), down_(down), left_(left), right_(right), cfg_(cfg), screenBounds_(screenBounds) {
		// Ensure all 4 buttons share the same size
		float avgW = (up_.w + down_.w + left_.w + right_.w) * 0.25f;
		up_.w = down_.w = left_.w = right_.w = avgW;
		up_.h = down_.h = left_.h = right_.h = avgW;

		// [PPSSPP-FORK] PSP parity: calculate D-pad spacing from actual button positions
		// on load so existing layouts are respected. After editing, spacing_ is controlled
		// independently via horizontal drag (matching PSP's PSPDPadButtons behavior).
		float cx = (up.x + down.x + left.x + right.x) * 0.25f;
		float cy = (up.y + down.y + left.y + right.y) * 0.25f;
		float dv = fabs(up.y - cy) + fabs(down.y - cy);
		float dh = fabs(left.x - cx) + fabs(right.x - cx);
		spacing_ = (dv > 0.0f || dh > 0.0f) ? (dv + dh) * 0.25f : spacing;
		if (spacing_ < 0.01f) spacing_ = 0.01f;
	}

	bool IsDownVisually() const override { return false; }

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_DIR"));
		if (image && image->w > 0) {
			// [PPSSPP-FORK] Bounding box accounts for both scale and spacing (arms extend beyond button size)
			float armSpan = std::max(up_.w * screenBounds_.w, spacing_ * screenBounds_.w);
			w = armSpan * 2.0f;
			h = armSpan * 2.0f;
		} else {
			w = 0;
			h = 0;
		}
	}

	void Draw(UIContext &dc) override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_DIR"));
		if (!image || image->w == 0) return;

		float desiredW = up_.w * screenBounds_.w;
		scale_ = desiredW / (float)image->w;

		// [PPSSPP-FORK] PSP parity: respect user opacity + button color
		float opacity = GamepadGetOpacity();
		uint32_t colorBg = colorAlpha(g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");

		// [PPSSPP-FORK] Render D-pad arrows relative to current dragging center
		// (matching PSP behavior to support smooth dragging/snapping in preview).
		static const float angles[4] = {0.0f, M_PI / 2.0f, M_PI, 3.0f * M_PI / 2.0f};
		static const float xoff[4] = {1.0f, 0.0f, -1.0f, 0.0f};
		static const float yoff[4] = {0.0f, 1.0f, 0.0f, -1.0f};

		float cpx = bounds_.centerX();
		float cpy = bounds_.centerY();
		// [PPSSPP-FORK] PSP parity: use independent spacing instead of hardcoded half-width
		float dist_pixels = spacing_ * screenBounds_.w;

		for (int i = 0; i < 4; i++) {
			float px = cpx + xoff[i] * dist_pixels;
			float py = cpy + yoff[i] * dist_pixels;

			// Overlay arrow — slightly further from center along same direction
			float dx = px - cpx;
			float dy = py - cpy;
			float len = sqrtf(dx * dx + dy * dy);
			float arrowOff = 4.0f * scale_;
			float ix = px, iy = py;
			if (len > 0.0f) {
				ix = px + (dx / len) * arrowOff;
				iy = py + (dy / len) * arrowOff;
			}

			dc.Draw()->DrawImageRotated(dirImage, px, py, scale_, angles[i] + M_PI, colorBg, false);
			dc.Draw()->DrawImageRotated(ImageID("I_ARROW"), ix, iy, scale_, angles[i] + M_PI, color, false);
		}
	}

	void SavePosition() override {
		float cx = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		float cy = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
		// [PPSSPP-FORK] PSP parity: use independent spacing instead of hardcoded half-width
		float dist = spacing_;
		up_.x = cx;
		up_.y = cy - dist;
		down_.x = cx;
		down_.y = cy + dist;
		left_.x = cx - dist;
		left_.y = cy;
		right_.x = cx + dist;
		right_.y = cy;
		// [PPSSPP-FORK] Persist spacing back to config
		cfg_.dpadSpacing = spacing_;
	}

	float GetScaleVal() const override { return up_.w; }

	void SetScaleVal(float s) override {
		up_.w = down_.w = left_.w = right_.w = s;
		up_.h = down_.h = left_.h = right_.h = s;
	}

	// [PPSSPP-FORK] PSP parity: independent D-pad spacing control
	float GetSpacingVal() const override { return spacing_; }
	void SetSpacingVal(float s) override { spacing_ = s; }

	bool Contains(float x, float y) override {
		const float t = 0.25f;
		// Expanded hitbox (2x the regular size to cover full D-pad area)
		float expandW = bounds_.w * 1.5f * 0.5f * t;
		float expandH = bounds_.h * 1.5f * 0.5f * t;
		Bounds tb(bounds_.x - expandW, bounds_.y - expandH,
			bounds_.w * 1.5f + expandW * 2.0f, bounds_.h * 1.5f + expandH * 2.0f);
		return tb.Contains(x, y);
	}

private:
	EmuCore::CoreTouchButton &up_, &down_, &left_, &right_;
	EmuCore::CoreTouchConfig &cfg_;  // [PPSSPP-FORK] Config reference for persisting dpadSpacing
	float spacing_ = 0.085f;  // [PPSSPP-FORK] Normalized arm distance from center (fraction of screen width)
	Bounds screenBounds_;
};

// [PPSSPP-FORK] MultiCore: D-pad is CoreDPadGroup (grouped, matching PSP)
// Other buttons (A, B, L, R, Start, Select) remain individual CoreDragDrop.

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
	CoreDragDropBase *GetPickedControl(float x, float y);

	CoreDragDropBase *picked_ = nullptr;
	float startObjX_ = -1.0f, startObjY_ = -1.0f;
	float startDragX_ = -1.0f, startDragY_ = -1.0f;
	float startScale_ = -1.0f;
	float startSpacing_ = -1.0f;  // [PPSSPP-FORK] PSP parity: D-pad spacing at drag start
	EmuCore::Type coreType_;
	std::vector<CoreDragDropBase *> controls_;
	bool created_ = false;

public:
	bool portrait_ = false;
};

// [PPSSPP-FORK] MultiCore: clamp point to bounds (matching PSP ClampTo)
static Point2D ClampPointTo(const Point2D &p, const Bounds &b) {
	return Point2D(std::clamp(p.x, b.x, b.x + b.w), std::clamp(p.y, b.y, b.y + b.h));
}

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
				// Snap to the nearest grid line relative to center (preventing fmod asymmetry/jumps)
				nx = cx + roundf((nx - cx) / grid) * grid;
				ny = cy + roundf((ny - cy) / grid) * grid;
			}
			Point2D clamped = ClampPointTo(Point2D(nx, ny), vr);
			nx = clamped.x;
			ny = clamped.y;
			picked_->ReplaceLayoutParams(new AnchorLayoutParams(nx, ny, NONE, NONE, Centering::Both));
		} else if (mode_ == 1) {
			// btn_.w is normalized width (fraction of screen, default ~0.10).
			// Resize range [0.03, 0.30] = 3%-30% of screen width.
			float diffY = -(touch.y - startDragY_) * 0.0015f;
			float ns = startScale_ + diffY;
			if (ns < 0.03f) ns = 0.03f;
			if (ns > 0.30f) ns = 0.30f;
			picked_->SetScaleVal(ns);

			// [PPSSPP-FORK] PSP parity: horizontal drag controls D-pad spacing independently
			// Vertical = scale, Horizontal = spacing (matching PSP TouchControlLayoutScreen)
			float diffX = (touch.x - startDragX_) * 0.0015f;
			float nsp = startSpacing_ + diffX;
			if (nsp < 0.015f) nsp = 0.015f;
			if (nsp > 0.30f) nsp = 0.30f;
			picked_->SetSpacingVal(nsp);
		}
	}
	if ((touch.flags & TouchInputFlags::DOWN) && !picked_) {
		picked_ = GetPickedControl(touch.x, touch.y);
		if (picked_) {
			startDragX_ = touch.x;
			startDragY_ = touch.y;
			const auto *params = picked_->GetLayoutParams()->As<AnchorLayoutParams>();
			startObjX_ = params->left;
			startObjY_ = params->top;
			startScale_ = picked_->GetScaleVal();
			startSpacing_ = picked_->GetSpacingVal();  // [PPSSPP-FORK] PSP parity: save spacing at drag start
		}
	}
	if ((touch.flags & TouchInputFlags::UP) && picked_) {
		picked_->SavePosition();
		picked_ = nullptr;
	}
	return true;
}

// [PPSSPP-FORK] PSP parity: distance-based picking — pick closest control to touch point
// Matching PSP ControlLayoutView::getPickedControl behavior
CoreDragDropBase *CoreLayoutView::GetPickedControl(float x, float y) {
	CoreDragDropBase *best = nullptr;
	float bestDist = 0.0f;
	for (auto *c : controls_) {
		if (c->Contains(x, y)) {
			const Bounds &b = c->GetBounds();
			float dist = (b.centerX() - x) * (b.centerX() - x) + (b.centerY() - y) * (b.centerY() - y);
			if (!best || dist < bestDist) {
				bestDist = dist;
				best = c;
			}
		}
	}
	return best;
}

void CoreLayoutView::Draw(UIContext &dc) {
	using namespace UI;
	// [PPSSPP-FORK] PSP parity: apply user opacity setting to preview buttons
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;
	GamepadUpdateOpacity(std::max(0.5f, opacity));
	dc.FillRect(Drawable(0x80000000), bounds_);
	dc.Flush();
	AnchorLayout::Draw(dc);
	// [PPSSPP-FORK] MultiCore: Draw grid on top of buttons (matching PSP SnapGrid z-order)
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

	// [PPSSPP-FORK] MultiCore: D-pad uses CoreDPadGroup (grouped, matching PSP)
	// Other buttons (A, B, L, R, Start, Select) remain individual CoreDragDrop.
	created_ = true;

	// Find D-pad buttons for grouping
	EmuCore::CoreTouchButton *dpUp = nullptr, *dpDown = nullptr;
	EmuCore::CoreTouchButton *dpLeft = nullptr, *dpRight = nullptr;
	bool dpadVisible = false;
	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		switch (btn.keyCode) {
		case CTRL_UP:    dpUp = &btn;    if (btn.visible) dpadVisible = true; break;
		case CTRL_DOWN:  dpDown = &btn;  if (btn.visible) dpadVisible = true; break;
		case CTRL_LEFT:  dpLeft = &btn;  if (btn.visible) dpadVisible = true; break;
		case CTRL_RIGHT: dpRight = &btn; if (btn.visible) dpadVisible = true; break;
		}
	}

	// Create grouped D-pad if all 4 buttons exist and at least one is visible
	if (dpUp && dpDown && dpLeft && dpRight && dpadVisible) {
		// [PPSSPP-FORK] Pass dpadSpacing + config ref for independent spacing persistence
		auto *dpad = new CoreDPadGroup(*dpUp, *dpDown, *dpLeft, *dpRight, cfg.dpadSpacing, cfg, b);
		controls_.push_back(dpad);
		Add(dpad);
	}

	// Create individual buttons for non-D-pad (skip D-pad keyCodes)
	for (int i = 0; i < cfg.count; i++) {
		auto &btn = cfg.buttons[i];
		if (!btn.visible) continue;
		// Skip D-pad buttons (handled by CoreDPadGroup above)
		if (btn.keyCode == CTRL_UP || btn.keyCode == CTRL_DOWN ||
			btn.keyCode == CTRL_LEFT || btn.keyCode == CTRL_RIGHT)
			continue;

		ImageID icon;
		ImageID bg;
		bool doFlip = false;
		{
			const auto *def = EmuCore::GetButtonDef(coreType_, btn.keyCode);
			if (def) {
				icon = ImageID(def->imageID);
				// [PPSSPP-FORK] PSP parity: resolve static string for outline to avoid dangling std::string_view
				const char *bgName = def->bgID;
				if (g_Config.iTouchButtonStyle) {
					if (strcmp(bgName, "I_ROUND") == 0) bgName = "I_ROUND_LINE";
					else if (strcmp(bgName, "I_RECT") == 0) bgName = "I_RECT_LINE";
					else if (strcmp(bgName, "I_SHOULDER") == 0) bgName = "I_SHOULDER_LINE";
					else if (strcmp(bgName, "I_STICK_BG") == 0) bgName = "I_STICK_BG_LINE";
				}
				bg = ImageID(bgName);
				doFlip = def->flipH;
			} else {
				icon = ImageID("I_CROSS");
				bg = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
			}
		}
		auto *dd = new CoreDragDrop(btn, b, bg, icon);
		if (doFlip) dd->FlipImageH(true);
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

// [PPSSPP-FORK] CoreTouchVisibilityScreen: per-button visibility setup (full dialog)
// Mirrors PSP TouchControlVisibilityScreen: full screen, icons, i18n, context menu
class CoreTouchVisibilityScreen : public UISimpleBaseDialogScreen {
public:
	CoreTouchVisibilityScreen(const Path &gamePath, EmuCore::Type coreType, bool portrait)
		: UISimpleBaseDialogScreen(gamePath, SimpleDialogFlags::ContentsCanScroll | SimpleDialogFlags::CustomContextMenu),
		  coreType_(coreType), portrait_(portrait) {}

	const char *tag() const override { return "CoreTouchVisibilityScreen"; }

	std::string_view GetTitle() const override {
		auto co = GetI18NCategory(I18NCat::CONTROLS);
		return co->T("Touch Control Visibility");
	}

	void CreateContextMenu(UI::ViewGroup *parent) override {
		using namespace UI;
		auto di = GetI18NCategory(I18NCat::DIALOG);

		// Toggle All — same pattern as PSP TouchControlVisibilityScreen
		Choice *toggleAll = parent->Add(new Choice(di->T("Toggle All")));
		toggleAll->OnClick.Add([this](UI::EventParams &e) {
			auto &tcfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);
			for (int i = 0; i < tcfg.count; i++) {
				tcfg.buttons[i].visible = nextToggleAll_;
			}
			// Also toggle system buttons (Fast-forward, Pause)
			DeviceOrientation orient = portrait_ ? DeviceOrientation::Portrait : DeviceOrientation::Landscape;
			TouchControlConfig &tcfgSys = g_Config.GetTouchControlsConfig(orient);
			tcfgSys.touchFastForwardKey.show = nextToggleAll_;
			if (System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON)) {
				tcfgSys.touchPauseKey.show = nextToggleAll_;
			}
			nextToggleAll_ = !nextToggleAll_;
		});
	}

	void CreateDialogViews(UI::ViewGroup *parent) override {
		using namespace UI;
		auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		auto &cfg = EmuCore::GetTouchConfigMutable(coreType_, portrait_);
		parent->Add(new ItemHeader(di->T("Show Buttons")));

		struct ToggleInfo {
			std::string labelKey;
			ImageID icon;
			bool *show;
		};
		std::vector<ToggleInfo> toggles;

		for (int i = 0; i < cfg.count; i++) {
			auto &btn = cfg.buttons[i];
			ImageID icon;
			const char *labelKey = nullptr;
			{
				const auto *def = EmuCore::GetButtonDef(coreType_, btn.keyCode);
				if (def) {
					icon = ImageID(def->imageID);
					labelKey = def->labelKey;
				}
			}
			if (!labelKey) continue;
			toggles.push_back({std::string(labelKey), icon, &btn.visible});
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
				choice = new CoreCheckBoxChoice(mc->T(t.labelKey), cb, new LinearLayoutParams(1.0f));
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
	}

	void onFinish(DialogResult result) override {
		// Save on all exit paths — matches PSP TouchControlVisibilityScreen::onFinish
		EmuCore::SaveTouchConfig(coreType_);
		INFO_LOG(Log::System, "[TOUCH] CoreTouchVisibilityScreen closed for %s", EmuCore::GetConfigSection(coreType_));
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

	// Kustomisasi — buka full dialog visibility (matching PSP TouchControlVisibilityScreen)
	leftCol->Add(new Choice(co->T("Customize")))->OnClick.Add([this](EventParams &) {
		bool p = layoutView_ ? layoutView_->portrait_ : false;
		screenManager()->push(new CoreTouchVisibilityScreen(gamePath_, coreType_, p));
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
