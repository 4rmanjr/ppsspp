# BUGS

## Tab Overlap di GameSettingsScreen

**Status**: Fixed (2025-06-08)

**Deskripsi**: Saat di tab Networking lalu pindah ke tab Audio/Graphics, konten tab Networking masih terlihat tumpang tindih dengan tab baru.

**Root Cause**: `UI/TabbedDialogScreen.cpp:CreateViews()` — urutan `EnsureAllCreated()` dan `SetInitialTab()` terbalik:
1. `EnsureAllCreated()` jalan duluan → semua tab dibuat, visibility set berdasarkan `currentTab_` (default = 0)
2. Tab 0 dapat `V_VISIBLE`, tab lainnya `V_GONE`
3. `SetInitialTab(3)` jalan belakangan → ubah `currentTab_` ke 3, tapi **tidak update visibility**
4. Tab 0 tetap `V_VISIBLE` → tumpang tindih saat tab mana pun diklik

**Fix**: Tukar urutan — `SetInitialTab()` dulu, baru `EnsureAllCreated()`.

```cpp
// Before (BUG):
CreateTabs();
tabHolder_->EnsureAllCreated();  // currentTab_ = 0 saat ini
if (currentTabSetting_)
    tabHolder_->SetInitialTab(*currentTabSetting_);  // currentTab_ = 3, tapi visibilitas tidak diupdate

// After (FIX):
CreateTabs();
if (currentTabSetting_)
    tabHolder_->SetInitialTab(*currentTabSetting_);  // currentTab_ = 3
tabHolder_->EnsureAllCreated();  // visibilitas set berdasarkan currentTab_ = 3 (benar)
```

---

## I_SYNC Image ID Tidak Valid

**Status**: Fixed (2025-06-08)

**Deskripsi**: `ImageID("I_SYNC")` di button "Sync Now" tidak ada di atlas → render garbage.

**Root Cause**: Image ID `I_SYNC` tidak terdaftar di atlas PPSSPP, menyebabkan UI render texture invalid.

**Fix**: Hapus parameter ImageID dari `Choice` (tidak pakai icon).

---

## TabHolder Crash `!createdCurrent`

**Status**: Fixed (2025-06-08)

**Deskripsi**: Assertion `!createdCurrent` di `TabHolder::SetCurrentTab()` ketika `currentTabSetting_` out of bounds.

**Root Cause**: Saved tab index dari sesi sebelumnya tidak valid (misal VR tab nomor 6, tapi VR tidak aktif).

**Fix**: Panggil `SetInitialTab()` dulu, lalu `EnsureAllCreated()`.
