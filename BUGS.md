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

**Status**: ✅ Not a bug (2026-06-11)

**Lokasi**: `Core/SaveStateLANSync.cpp:450-479` (general handler), `Core/SaveStateLANSync.cpp:560-590` (POST handler)

**Deskripsi**: Dianalisa ulang. General handler baca sampai 200 iterasi × 16KB = 3.2MB buffer awal. POST upload handler punya `while (bytesRead < contentLength)` loop sendiri yang membaca sisa bytes dari socket stream. Upload besar bekerja dengan benar.

**Impact**: N/A — bukan bug fungsional. Optimisasi memory (hindari double-buffer 3.2MB) deferred sebagai low-priority.

**Fix**: Tidak diperlukan.

---

### #5: Conflict Resolution Stubbed

**Status**: ✅ Fixed (pre-2026-06-11, BUGS.md was stale)

**Lokasi**: `Core/SaveStateLANSync.cpp:1466-1578`

**Deskripsi**: `ResolveConflict()` mendownload .ppst + .jpg thumbnail dari peer, simpan ke disk lokal dengan atomic .tmp → rename. `ResolveAllConflicts()` iterasi semua pending conflicts, panggil `ResolveConflict()`, lalu clear queue. `KEEP_LOCAL` return early. Implementasi lengkap.

**Impact**: N/A — sudah bekerja.

**Fix**: Update BUGS.md.

---

### #6: Server Tidak Validasi Token

**Status**: ✅ Fixed (2026-06-11)

**Lokasi**: `Core/SaveStateLANSync.cpp:493-516`

**Deskripsi**: API endpoint tidak validasi `Authorization: Bearer`. Device mana pun bisa download/overwrite tanpa pairing.

**Impact**: Security hole.

**Fix**: Tambah `ExtractBearerToken()` helper dan pre-route auth check. Semua `/api/v1/saves/*` endpoint sekarang validasi token terhadap paired peer tokens. Return 401 untuk request tidak terautentikasi. Pairing endpoints tetap tanpa auth (PIN-based).

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

---

# SDL Desktop Deadlock — LAN Sync Tidak Bisa Distart

**Status**: ✅ Fixed (2025-06-09)

**Deskripsi**: LAN Save State Sync tidak bisa diaktifkan sama sekali di desktop SDL/Linux. Server TCP tidak pernah start, tombol Pair dan Sync tidak merespon. Deadlock pattern — 5 bug saling blokir.

---

### #11: LinuxLANSync Tidak Di-init di Startup SDL

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/NativeApp.cpp:566-567`

**Deskripsi**: Di Android, `NativeApp.cpp` panggil `AndroidLANSync::Instance().Init()` saat startup. Di SDL, tidak ada panggilan `GetLinuxLANSync().Init()` — `SaveStateLANSync` internal state (`mDNS`, `PlatformKeyStore`, dll) tidak pernah diinisialisasi.

**Impact**: Server `StartServer()` bisa dipanggil secara teknis, tapi `PlatformKeyStore` dan `mDNS` belum siap.

**Fix**: Tambah `GetLinuxLANSync().Init()` di dalam blok `#elif !PPSSPP_PLATFORM(WINDOWS)` yang sudah ada, SETELAH `g_Config.memStickDirectory` di-set. Tidak bikin blok `#elif` baru agar tidak memotong chain preprocessor.

**⚠️ Regression (2025-06-09)**: Awalnya ditaruh di blok `#elif !defined(MOBILE_DEVICE)` terpisah yang memotong chain preprocessor — menyebabkan `g_Config.memStickDirectory` tidak pernah di-set di Linux → error `/PSP: Permission denied`. Diperbaiki dengan memindahkan ke dalam blok `#elif !PPSSPP_PLATFORM(WINDOWS)` yang sudah ada.

---

### #12: Checkbox "Enable LAN Sync" Handler Hanya untuk Android

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/GameSettingsScreen.cpp:1149-1158`

**Deskripsi**: Handler `OnClick` dari checkbox "Enable LAN Sync" dibungkus `#if PPSSPP_PLATFORM(ANDROID)`. Di SDL, toggle checkbox cuma set `g_Config.lanSync.bEnabled = true` — **tidak pernah** manggil `StartServer()` atau `StartDiscovery()`.

**Impact**: Server tidak pernah start meskipun user mengaktifkan LAN sync.

**Fix**: Tambah `#elif (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC)) && !PPSSPP_PLATFORM(ANDROID)` yang panggil `GetLinuxLANSync().Enable()` / `.Disable()`.

**⚠️ Regression (2025-06-09)**: Awalnya pakai guard `!defined(MOBILE_DEVICE)` yang juga reachable di Windows Qt build — `GetLinuxLANSync()` tidak tersedia di Windows → linker error. Diperbaiki dengan guard spesifik platform.

---

### #13: SDLLANSyncUI Hanya Dibuat Jika bEnabled Sudah True

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/EmuScreen.cpp:1887-1888`

**Deskripsi**: `SDLLANSyncUI` (yang set `g_LANSyncUI` global via constructor) cuma dibuat jika `g_Config.lanSync.bEnabled == true` saat EmuScreen init. Karena default `false`, object tidak pernah dibuat. Render loop (line 1960) juga cek `g_Config.lanSync.bEnabled && g_LANSyncUI` — `g_LANSyncUI` selalu null.

```cpp
if (g_Config.lanSync.bEnabled) {
    new SDLLANSyncUI();  // g_LANSyncUI di-set di constructor
}
```

**Impact**: Semua ImGui dialog (settings, pairing, progress) tidak pernah dirender karena `g_LANSyncUI == nullptr`.

**Fix**: Hapus kondisi `if (g_Config.lanSync.bEnabled)` — `SDLLANSyncUI` harus selalu dibuat di desktop.

---

### #14: Pair Button di SDL Coba Buka g_LANSyncUI yang Null

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/GameSettingsScreen.cpp:1200-1203`

**Deskripsi**: Handler "Pair New Device" di SDL cuma ngecek `if (g_LANSyncUI)` lalu `g_LANSyncUI->OpenSettings()`. Karena Bug #13 bikin `g_LANSyncUI` null, tombol tidak ngapa-ngapain. Bahkan setelah Bug #13 di-fix, `OpenSettings()` cuma buka settings window — bukan pairing flow.

**Impact**: User tidak bisa pairing dari tombol Pair New Device.

**Fix**: Ganti jadi `g_LANSyncUI->OpenServerPairing()` + `g_LANSyncUI->OpenPairing()` — generate PIN dan buka dialog pairing.

---

### #15: Render Loop Juga Terblokir oleh Kondisi bEnabled

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/EmuScreen.cpp:1960`

**Deskripsi**: Render loop ImGui dialog cuma jalan jika `g_Config.lanSync.bEnabled && g_LANSyncUI`. Setelah Bug #13 di-fix, `g_LANSyncUI` tersedia. Tapi kondisi `bEnabled` masih menghalangi rendering dialog pairing & settings jika user baru enable dari settings screen.

**Impact**: Dialog pairing/progress tidak muncul hingga restart EmuScreen.

**Fix**: Ubah kondisi jadi `if (g_LANSyncUI)` saja — tanpa cek `bEnabled`. Biarkan masing-masing dialog handle visibility-nya sendiri via `*open` flag.

---

---

### #16: HandlePairStatus Tidak Return token/peerId

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `Core/SaveStateLANSync.cpp:1422-1449` (`HandlePairStatus`)

**Deskripsi**: Saat PC polling `GET /api/v1/pair-status?requestId=...` setelah user accept pairing di Android, response cuma `{"status":"approved"}` — **tanpa** `token` dan `peerId`. 

Akibatnya di `AutoPairWithPeer()`:
```cpp
std::string token = extractStr("token");  // empty!
std::string peerId = extractStr("peerId"); // empty!
```
Token dan peerId kosong → peer disimpan tanpa token → sync gagal karena `Authorization: Bearer` kosong.

**Root Cause**: `PendingPairRequest` tidak punya field `token`. `HandlePairRespond` generate token tapi tidak disimpan di request. `HandlePairStatus` hanya return status tanpa detail.

**Fix**:
1. Tambah field `std::string token` ke `PendingPairRequest`
2. `HandlePairRespond`: simpan `storedToken` ke `req.token`
3. `HandlePairStatus`: return `token` dan `peerId` saat status `approved`

---

### #17: HandlePairRespond Set peer.port = 0

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `Core/SaveStateLANSync.cpp:1385-1400` (`HandlePairRespond`)

**Deskripsi**: Saat server accept pairing via `HandlePairRespond`, `peer.port` di-set ke `0` karena `PendingPairRequest` tidak menyimpan port client. Akibatnya saat sync, `DoSync()` connect ke `peer.port = 0` → connection refused.

**Root Cause**: `HandleAutoPairRequest` tidak parse `port` dari request body client. `AutoPairWithPeer()` juga tidak include `port` di body.

**Fix**:
1. `AutoPairWithPeer()`: tambah `"port":<serverPort_>` di request body
2. `HandleAutoPairRequest()`: parse `port` dari body, simpan di `PendingPairRequest.port`
3. `HandlePairRespond()`: pakai `req.port` instead of hardcoded `0`

---

### #18: Toast "No online paired peers" Selalu Muncul

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/GameSettingsScreen.cpp:1217-1221`

**Deskripsi**: `System_Toast("No online paired peers")` dipanggil **setelah loop** tanpa conditional — selalu muncul meskipun sync berhasil di-trigger.

```cpp
for (auto &p : peers) {
    if (p.paired && p.online) {
        core.SyncWithPeer(...);  // sync berhasil
    }
}
System_Toast("No online paired peers");  // TAPI toast ini tetap muncul!
```

**Fix**: Tambah `bool found = false` dan set ke `true` saat sync di-trigger. Toast cuma muncul jika `!found`.

---

---

### #19: Self-Discovery — Device Pairing dengan Dirinya Sendiri

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `Core/SaveStateLANSync.cpp:160-200` (StartDiscovery callbacks)

**Deskripsi**: Setelah enable LAN Sync, device: 
1. Start server + announce mDNS (`svc.id = deviceId_`)
2. Start discovery browser → menemukan service mDNS milik **diri sendiri**
3. Dirinya masuk ke `discoveredPeers_`
4. UI menampilkan device sendiri sebagai "Discovered Peers"
5. User klik "Pair" → `AutoPairWithPeer(ownIP, ownPort)`
6. Connect ke server sendiri → `HandleAutoPairRequest` → pending request
7. Buka Pair lagi → muncul tombol "Accept"/"Reject" untuk request pairing dari diri sendiri
8. Accept → paired dengan diri sendiri, port = 0

**Gejala di logcat**:
```
Pair request from PPSSPP  (padahal ini device sendiri)
Pairing accepted!
No online paired peers  (karena port = 0, sync gagal connect)
```

**Root Cause**: 
- Callback mDNS & UDP discovery di `StartDiscovery()` tidak filter `peer.id == deviceId_`
- `AutoPairWithPeer()` tidak cek apakah target adalah diri sendiri
- `GetDiscoveredPeers()` tidak filter self entries

**Fix (4 lapis defense)**:
1. `mDNS callback`: `if (peer.id == deviceId_) return;`
2. `UDP callback`: `if (peer.id == deviceId_) return;`
3. `AutoPairWithPeer()`: cek `port == serverPort_` sebelum connect
4. `HandleAutoPairRequest()`: reject jika `peerId == deviceId_`
5. `GetDiscoveredPeers()`: filter out `p.id != deviceId_`

---

---

### #20: SDL ImGui `DrawPairingDialog` Tidak Support Pending Requests

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `SDL/SDLLANSync.cpp:199-270` (`DrawPairingDialog`)

**Deskripsi**: ImGui pairing dialog (`SDLLANSyncUI::DrawPairingDialog`) hanya support PIN-based flow lama. Tidak menampilkan pending requests dengan tombol Accept/Reject. Akibatnya di PC (SDL), jika ada Android yang kirim pair request, PC user tidak bisa accept via ImGui UI.

Juga, `DrawAutoDiscoverSection` Pair button panggil PIN-based flow (`awaitingPIN_ = true`), bukan `AutoPairWithPeer`. Mengakibatkan ketidakcocokan dengan flow baru.

**Fix**:
1. Tambah `DrawPendingRequestsSection()` — tampilkan pending requests dengan Accept/Reject
2. `DrawPairingDialog()` panggil `DrawPendingRequestsSection()`
3. `DrawAutoDiscoverSection()` Pair button panggil `AutoPairWithPeer()`, bukan PIN flow
4. Tambah deklarasi `DrawPendingRequestsSection()` di `SDLLANSync.h`

---

### #21: PIN Generation Tidak Tepat di Android

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/GameSettingsScreen.cpp:1187-1198`, `UI/LANPeerListScreen.cpp:118-128`

**Deskripsi**: PIN di-generate setiap kali user buka Pair New Device, tapi AutoPairWithPeer tidak pakai PIN. PIN cuma diperlukan untuk manual entry. Toast "Your PIN: XXXX - Port: Y" muncul tiap buka pairing screen — membingungkan.

**Fix**:
1. Hapus `GeneratePairingPin()` + Toast dari button handler Android
2. Pindah ke `LANPeerListScreen::ShowManualEntry()` — PIN digenerate hanya saat user pilih manual entry

---

### #25: DoSync Crash di Android — Background Thread Tanpa JNI Attachment

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `Core/SaveStateLANSync.cpp:845-860` (`SyncWithPeer` background thread)

**Deskripsi**: `DoSync()` dipanggil dari `std::thread` (background thread). Di Android, fungsi `File::GetFilesInDir()` menggunakan content URI via JNI (`Android_ListContentUri`). Fungsi tersebut memanggil `getEnv()` yang meng-*assert* bahwa thread sudah ter-attach ke Java VM.

Thread dari `std::thread` TIDAK ter-attach ke JVM → `getEnv()` panggil `HandleAssert` → SIGABRT.

**Stack trace**:
```
#08 SaveStateLANSync::DoSync(...)+444
#07 File::GetFilesInDir(...)
#06 Android_ListContentUri(...)
#05 getEnv()+252 → HandleAssert → CRASH
```

**Root Cause**: Setiap thread yang panggil JNI functions di Android harus di-attach ke JavaVM via `AttachCurrentThread`. `std::thread` tidak melakukan ini secara otomatis.

**Fix**: `SyncWithPeer()` — di lambda background thread, attach `gJvm->AttachCurrentThread()` sebelum `DoSync`, detach setelah selesai.

---

### #22: Toast "Pair request sent!" Kurang Tepat

**Status**: ✅ Fixed (2025-06-09)

**Lokasi**: `UI/LANPeerListScreen.cpp:120`

**Deskripsi**: Toast muncul SETELAH pairing selesai (polling approved), bukan saat request dikirim. Pesan misleading.

**Fix**: Ganti jadi `System_Toast("Pairing successful!")`.

---

## Summary

| # | Bug | Priority | Status |
|---|-----|----------|--------|
| 1 | Hardcoded `"current_game"` | Critical | ✅ Fixed |
| 2 | Token mismatch pairing | Critical | ✅ Fixed |
| 3 | HandleSaveList missing `gameId` | Critical | ✅ Fixed |
| 4 | 4KB request limit | High | ✅ Not a bug |
| 5 | Conflict resolution stub | High | ✅ Fixed (stale BUGS.md) |
| 6 | No server auth | Medium | ✅ Fixed |
| 7 | Detached thread lifecycle | Medium | ✅ Fixed |
| 8 | GetPeers() empty | Low | ❌ Open |
| 9 | TLS not wired | Low | ❌ Open |
| 10 | Windows keystore persistence | Low | ❌ Open |
| 11 | LinuxLANSync tidak di-init startup | High | ✅ Fixed |
| 12 | Checkbox handler Android-only | High | ✅ Fixed |
| 13 | SDLLANSyncUI tidak pernah dibuat | High | ✅ Fixed |
| 14 | Pair button null pointer | High | ✅ Fixed |
| 15 | Render loop terblokir bEnabled | High | ✅ Fixed |
| 16 | HandlePairStatus tidak return token/peerId | Critical | ✅ Fixed |
| 17 | HandlePairRespond set peer.port = 0 | Critical | ✅ Fixed |
| 18 | Toast "No online paired peers" selalu muncul | Medium | ✅ Fixed |
| 19 | Self-Discovery (pair dengan diri sendiri) | Critical | ✅ Fixed |
| 20 | ImGui pairing dialog tidak support pending requests | High | ✅ Fixed |
| 21 | PIN generation tidak tepat di Android | Low | ✅ Fixed |
| 22 | Toast "Pair request sent!" misleading | Low | ✅ Fixed |
| 23 | Device type hardcoded "Linux" di semua platform | High | ✅ Fixed |
| 24 | Android JNI discovered peers tidak masuk ke SaveStateLANSync | Critical | ✅ Fixed |
| 25 | DoSync crash di Android: background thread tanpa JNI attachment | Critical | ✅ Fixed |
