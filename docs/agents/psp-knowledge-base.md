# PSP Knowledge Base — GBA Implementation Mismatches

> **Tujuan:** Catat setiap mismatch yang ditemukan antara implementasi GBA dan PSP.
> Agent lain WAJIB baca file ini sebelum mengerjakan fitur GBA apapun.
> Update file ini setiap kali menemukan mismatch baru.

## Cara Penggunaan

- Format: `# File <path> -> <class/fungsi>`
- Setiap entry: Bug → Akibat → Fix → Pelajaran
- Tanggal + komit hash untuk traceability
- Jangan hapus entry lama (kecuali sudah irrelevant)

---

# ======================================================
# UI / Core Touch Layout
# ======================================================

## nextToggleAll_ initial value — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** `nextToggleAll_` mulai `false` (GBA), harus `true` (PSP)
- **Akibat:** Toggle All pertama malah SEMBUNYIKAN semua button (kebalikan dari ekspektasi user)
- **Fix:** `false` → `true` (komit `c989dbf886`)
- **Pelajaran:** PSP value WAJIB dicek sebelum inisialisasi field baru. Jangan asumsi `false` itu "default safe".

## Toggle All tidak cover system buttons — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Toggle All hanya toggle game buttons (A/B/Select/Start/L/R/D-Pad), Fast-forward dan Pause tidak kena
- **Akibat:** User toggle all OFF → Fast-forward & Pause tetap ON. Inkonsisten dengan PSP
- **Fix:** Extend handler untuk juga toggle `touchFastForwardKey.show` dan `touchPauseKey.show` dari `TouchControlConfig` (komit `c6faf8e`)
- **Pelajaran:** Jangan asumsi scope Toggle All sama dengan PSP. Selalu verifikasi ulang dengan PSP reference.

## Pause `SetMinimumAlpha` conditional vs unconditional — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → GBA Pause button creation
- **Bug:** `SetMinimumAlpha(0.1f)` hanya dipanggil pada device tanpa hardware back button. PSP panggil **unconditional**.
- **Akibat:** Pause button bisa fully transparent pada device dengan back button (user tidak bisa menemukannya)
- **Fix:** Hapus `if (!System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON))` — panggil unconditional (komit `c6faf8e`)
- **Pelajaran:** Baca PSP code secara utuh, jangan tebak kondisi berdasarkan komentar saja.

## System buttons (Fast-forward, Pause) tidak dibuat di GBA — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → GBA `CreateViews()`
- **Bug:** Hanya `AddGBATouchButtons()` dipanggil yang membuat game buttons (A/B/L/R/Select/Start/D-Pad). Tidak ada pembuatan Pause dan Fast-forward button.
- **Akibat:** User GBA tidak punya akses ke Pause (kecuali hardware button) dan Fast-forward sama sekali
- **Fix:** Tambah pembuatan BoolButton untuk Pause (`&pauseTrigger_`) dan Fast-forward (`&PSP_CoreParameter().fastForward`) dari shared `TouchControlConfig` (komit `ed32432`)
- **Pelajaran:** PSP `CreatePadLayout()` bukan cuma game buttons. Ada system buttons (Pause, Fast-forward) yang juga harus ada di GBA. Mapping fitur PSP→GBA harus lengkap.

## GBA touch button size tiny dots (preview editor) — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBADragDrop::Draw()`
- **Bug:** Formula `scale_ = btn_.w * g_layoutScale` (0.09 * 0.8 = 0.072) — treat `btn_.w` sebagai image scale factor, padahal normalized width
- **Akibat:** Button render ~7px (dot kecil) bukan ~87px
- **Fix:** `scale_ = (btn_.w * screenBounds_.w) / image->w` — normalized width → pixel → atlas tile scale (komit `af19ea7c03`)
- **Pelajaran:** Pahami semantic field yang digunakan. `btn_.w` adalah `normalized width` (0-1), bukan scale factor.

## GBA gameplay touch button size hardcoded — ✅ Fixed

- **File:** `UI/EmuScreen.cpp` → `AddGBATouchButtons()`
- **Bug:** `bgScale = 0.8f` (hardcoded, mengabaikan `btn.w`)
- **Akibat:** Semua button ukuran sama, tidak bisa di-customize via layout editor
- **Fix:** `bgScale = (btn.w * bounds.w) / (float)atlasImg->w` — match PSP behavior (komit `a94d8f5ed5`)
- **Pelajaran:** Hardcoded value harus dihindari. Setiap button size harus dari config.

## Resize range tidak match dengan semantic baru — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBALayoutView::Touch()`
- **Bug:** Resize range `[0.3, 1.5]` dirancang untuk semantic "image scale factor" (old broken formula). Setelah fix ke "normalized width", range ini terlalu besar.
- **Akibat:** Resize slider tidak proporsional. 1.5 normalized width = 150% screen → button overflow.
- **Fix:** Range `[0.03, 0.30]` = 3%-30% screen width. Movement factor `0.02f` → `0.0015f` untuk drag 133px span default→max (komit `af19ea7c03`)
- **Pelajaran:** Setiap perubahan semantic field WAJIB diikuti update range dan sensitivity.

## GBA portrait screen position bukan DisplayOffsetY — ✅ Fixed

- **File:** `EmuCore/GBACore.cpp` → `GBACore::GetRenderRect()`
- **Bug:** `y = (viewH - h) / 2.0f` (centered). PSP portrait pakai `CalculateDisplayOutputRect` yang baca `fDisplayOffsetY = 0.25f` (top-aligned).
- **Akibat:** GBA portrait centered di layar, PSP portrait top-aligned. User yang custom offset tidak kena GBA.
- **Fix:** `y = viewH * offsetY - h * 0.5f` dengan `offsetY = g_Config.displayLayoutPortrait.fDisplayOffsetY` (0.25 default) (komit `56924fa019`)
- **Pelajaran:** Layout formula berbeda per-core perlu diperiksa. PSP portrait logic ada di `GPU/Common/PresentationCommon.cpp`, GBA di `EmuCore/GBACore.cpp` — keduanya harus konsisten.

## GBATouchVisibilityPopup tidak ada button icons — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Hanya text label checkbox, tidak ada icon button (seperti PSP yang pakai I_CROSS, I_CIRCLE, dll)
- **Akibat:** Kurang user-friendly, sulit bedain A vs B secara visual
- **Fix:** Tambah `GBACheckBoxChoice` class + atlas images untuk setiap button row (komit `1eaa2c77be`)
- **Pelajaran:** Visual parity dengan PSP penting — icons membantu identifikasi cepat.

## GBATouchVisibilityPopup save hanya di OK OnClick — ✅ Fixed

- **File:** `UI/CoreTouchLayoutScreen.cpp` → `GBATouchVisibilityPopup`
- **Bug:** Save config hanya di handler OK click. PSP save di `onFinish()` yang mencakup semua exit path (OK, Cancel, system back button).
- **Akibat:** Android back button → data loss (perubahan visibility tidak tersimpan)
- **Fix:** Pindah save ke `OnCompleted()` override (komit `1eaa2c77be`)
- **Pelajaran:** Save WAJIB di `onFinish`/`OnCompleted` untuk cover semua exit path, bukan di handler button OK.

---

# ======================================================
# Shared Config / TouchControlConfig
# ======================================================

## TouchControlConfig ownership tidak jelas

- **File:** `UI/GamepadEmu.cpp`, `UI/EmuScreen.cpp`
- **Masalah:** PSP dan GBA pakai `g_Config.GetTouchControlsConfig(orientation)` yang SAMA. Tapi inisialisasi (`InitPadLayout`) hanya untuk PSP. GBA pakai value yang sudah di-init oleh PSP path.
- **Akibat:** Tidak ada masalah saat ini karena `InitPadLayout` dipanggil unconditional sebelum branching core. Tapi rawan kalau ada perubahan.
- **Pelajaran:** Shared config harus explicit siapa yang initialize dan kapan. Jangan mengandalkan side effect dari init core lain.

## Default Pause position berbeda antara PSP InitPadLayout dan GBA

- **File:** `UI/GamepadEmu.cpp` → `InitPadLayout()`
- **PSP:** `Pause_button_center_X = halfW`, `Pause_button_center_Y = 28.0f`
- **GBA:** Tidak ada `InitGBAPadLayout()` — GBA pakai value yang sudah di-init oleh PSP
- **Akibat:** Pause button position di GBA sama dengan PSP (top center). Mungkin kurang cocok untuk GBA layout.
- **Pelajaran:** Kalau butuh default posisi berbeda per-core, harus buat `Init<Core>PadLayout()` terpisah atau override di `EmuCore/Config.cpp`.
