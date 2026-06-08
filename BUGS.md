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

---

# LAN Sync Bugs

## Legend
- `✅ Fixed` — Sudah diperbaiki
- `❌ Open` — Belum diperbaiki
- `⏳ Deferred` — Ditunda, tidak kritis untuk MVP

---

## Critical (End-to-End Flow)

### #1: Hardcoded `"current_game"` di DoSync

**Status**: ✅ Fixed (2025-06-08)

**Lokasi**: `Core/SaveStateLANSync.cpp:679,690`

**Deskripsi**: File path untuk download save state menggunakan string literal `"current_game"` bukan game prefix sebenarnya (`ULUS10083_1.02`). Akibatnya semua save di-download ke file `current_game_N.ppst`, saling timpa.

**Fix**: Parse `gameId` dari `HandleSaveList` response JSON, gunakan untuk file path.

---

### #2: Token Mismatch saat Pairing

**Status**: ✅ Fixed (2025-06-08)

**Lokasi**: `Core/SaveStateLANSync.cpp:902-903`

**Deskripsi**: `HandlePairRequest()` generate token A, lalu panggil `AcceptPairing()` yang generate token B. Client terima token A, server simpan token B.

**Fix**: Inline `AcceptPairing()` di `HandlePairRequest()`, simpan token yang sama yang direturn.

---

### #3: HandleSaveList Response Tidak Sertakan `gameId`

**Status**: ✅ Fixed (2025-06-08)

**Lokasi**: `Core/SaveStateLANSync.cpp:937-942`

**Deskripsi**: Response `GET /api/v1/saves/list` hanya berisi `slot`, `size`, `hash`, `hlc`. Tidak ada field `gameId`.

**Fix**: Tambah field `"gameId":"<prefix>"` di setiap entry JSON.

---

### #4: Server Hanya Baca 4KB Request

**Status**: ❌ Open

**Lokasi**: `Core/SaveStateLANSync.cpp:293-296`

**Deskripsi**: `recv(clientFd, buf, sizeof(buf) - 1, 0)` dengan `buf[4096]`. Upload body > 3.5KB terpotong.

**Impact**: Semua upload save state > 3.5KB rusak.

**Fix needed**: Loop read sampai Content-Length terpenuhi.

---

### #5: Conflict Resolution Stubbed

**Status**: ❌ Open

**Lokasi**: `Core/SaveStateLANSync.cpp:758-773`

**Deskripsi**: `ResolveConflict()` punya `break` kosong untuk `KEEP_LOCAL`/`KEEP_REMOTE`. `ResolveAllConflicts()` clear queue tanpa apply resolusi.

**Impact**: Konflik terdeteksi tapi tidak di-resolve.

**Fix needed**: Implementasi download/upload di `ResolveConflict()`.

---

### #6: Server Tidak Validasi Token

**Status**: ❌ Open

**Lokasi**: `Core/SaveStateLANSync.cpp:318-413`

**Deskripsi**: API endpoint tidak validasi `Authorization: Bearer`. Device mana pun bisa download/overwrite tanpa pairing.

**Impact**: Security hole.

**Fix needed**: Validasi token di setiap handler.

---

### #7: Detached Threads Tanpa Join

**Status**: ✅ Fixed (2025-06-08)

**Lokasi**: `Core/SaveStateLANSync.cpp:292,415`

**Deskripsi**: `std::thread(...).detach()` untuk setiap koneksi client. Shutdown saat thread running = crash.

**Fix**: Ganti `.detach()` dengan `AddBackgroundThread()` — thread di-join di `JoinAllThreads()` saat shutdown.

---

## Platform-Specific

### #8: UDPDiscovery::GetPeers() Return Empty

**Status**: ❌ Open

**Lokasi**: `Common/Net/UDPDiscovery.cpp:294-296`

**Deskripsi**: GetPeers() return `{}`. Peer discovery via callbacks berfungsi tapi query peer list tidak.

**Impact**: Perlu verifikasi apakah fungsi ini dipanggil.

---

### #9: TLS Tidak Di-wire ke Socket

**Status**: ❌ Open (Phase 5)

**Lokasi**: `Core/SaveStateLANSync.cpp:215-224`

**Deskripsi**: `TLSServerContext` dibuat, `AcceptTLS()` tidak pernah dipanggil. Semua koneksi plain TCP.

**Impact**: Data tidak dienkripsi.

---

### #10: PlatformKeyStore Windows No Persistence

**Status**: ❌ Open

**Lokasi**: `Common/Net/PlatformKeyStore_Windows.cpp`

**Deskripsi**: DPAPI in-memory only. `Shutdown()` tidak write ke disk. Secrets hilang setelah restart.

**Impact**: Windows only.

---

## Summary

| # | Bug | Priority | Status |
|---|-----|----------|--------|
| 1 | Hardcoded `"current_game"` | Critical | ✅ Fixed |
| 2 | Token mismatch pairing | Critical | ✅ Fixed |
| 3 | HandleSaveList missing `gameId` | Critical | ✅ Fixed |
| 4 | 4KB request limit | High | ❌ Open |
| 5 | Conflict resolution stub | High | ❌ Open |
| 6 | No server auth | Medium | ❌ Open |
| 7 | Detached thread lifecycle | Medium | ✅ Fixed |
| 8 | GetPeers() empty | Low | ❌ Open |
| 9 | TLS not wired | Low | ❌ Open |
| 10 | Windows keystore persistence | Low | ❌ Open |
