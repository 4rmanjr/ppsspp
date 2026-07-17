# BUGS.md — Riwayat Bug & Issue yang Sudah FIXED (PPSSPP-LANSYNC)

> Dokumen ini memuat seluruh riwayat bug, issue, dan temuan code review
> yang sudah diperbaiki atau diverifikasi. Untuk status proyek dan issue
> yang masih **OPEN**, lihat [STATUS.md](STATUS.md).

---

## 📋 Daftar Isu — Semua FIXED

### Issues (#6–#14)

| # | Priority | Issue | Fix | Status |
|---|----------|-------|-----|--------|
| 6 | ~~KRITIS~~ | TLS antar-device gagal (sync tidak jalan) | Dual SSL_CTX + TOFU fingerprint | **FIXED** |
| 7 | ~~TINGGI~~ | Pairing tidak di-enforce di endpoint data | `IsPeerTrusted()` di 3 handler `/states` | **FIXED** |
| 8 | ~~SEDANG~~ | Resolusi konflik murni mtime (LWW) + celah tie | SHA256 checksum tie-break | **FIXED** |
| 9 | ~~RENDAH~~ | Tidak ada batas ukuran PUT | Content-Length check + 413 | **FIXED** |
| 10 | ~~RENDAH~~ | Parse gameId/slot asumsi 1 underscore | `ParseSaveFilename()` helper | **FIXED** |
| 11 | ~~SEDANG~~ | Smoke test Test 3 bisa FAIL palsu pada re-run | Mitigasi port-random + `fuser -k` | **VERIFIED** |
| 12 | ~~SEDANG~~ | TSAN belum dijalankan | Build TSAN + 0 race LANSync | **VERIFIED** |
| 13 | ~~SEDANG~~ | Android LOCAL guard (self-discovery) | 3 lapis self-detection | **FIXED** |
| 14 | ~~SEDANG~~ | mDNS error tidak tampil di UI | `DiscoveryEvent::ERROR` + `discoveryError_` | **FIXED** |

### Code Review — Technical Debt (TD1–TD7)

| # | Severity | Isu | Status |
|---|----------|-----|--------|
| TD1 | Rendah | Duplikasi `GetDeviceId()` / `GetLocalPeerId()` | **FIXED** |
| TD2 | Medium | Bug: `SyncWithAllPeers()` hanya sync 1 peer | **FIXED** |
| TD3 | Rendah | 4 parser JSON manual berbeda | **FIXED** |
| TD4 | Rendah (latent) | UB: `isdigit(data[pos])` tanpa cast | **FIXED** |
| TD6 | Low | Hint permission cuma teks kecil | **VERIFIED** |
| TD7 | Low | Hint belum dilokalisasi | **VERIFIED** |

### Code Review — Bug, Race, Memory, Security (SR1–SR9)

| # | Severity | Isu | Status |
|---|----------|-----|--------|
| SR1 | ~~Critical~~ | Directory traversal — **FALSE POSITIVE** | **FALSE POSITIVE** |
| SR2 | ~~Critical~~ | JSON injection | **FIXED** |
| SR3 | High | Data race `confirmPin_` | **FIXED** |
| SR4 | High | `currentSSL_` race | **FIXED** |
| SR5 | Medium | Non-blocking flag leak | **FIXED** |
| SR6 | Medium | OpenSSL return value unchecked | **FIXED** |
| SR7 | Medium | Non-portable `uint64_t` cast | **FIXED** |
| SR8 | Low | Busy-wait di `AutoSyncLoop()` | **FIXED** |
| SR9 | Low | `probeThread_` handle tidak direset | **FIXED** |

### Code Review — Production Hardening (CR1–CR4)

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| CR1 | HIGH | `FindPeer(peer.peerId)` mismatch — TOFU silently skipped | **FIXED** |
| CR2 | MEDIUM | `GetPeerCertPEM()` UB: `std::string(nullptr, 0)` | **FIXED** |
| CR3 | MEDIUM | `SavePeer()` 2048-byte buffer overflow | **FIXED** |
| CR4 | LOW/CRITICAL | PEM in JSON without string escaping | **FIXED** |

---

## 🔍 Detail Temuan

### Issue #6 — TLS antar-device gagal

**Root cause:** `TLSContext` single `ctx_` overwritten by `InitServer()` after `InitClient()`. When `LANSyncClient::Connect()` calls `GetSSLContext()` (which returned the server context after `InitServer()`), the client uses a `TLS_server_method()` SSL → handshake fails.

**Fix (6 files modified):**
- **Dual SSL_CTX**: `ctx_` split into `ctxServer_`/`ctxClient_`. `InitServer()` uses `TLS_server_method()`, `InitClient()` uses `TLS_client_method()`. Client calls `GetClientContext()`, server calls `GetServerContext()`.
- **SSL_VERIFY_NONE on client**: Fingerprint verified manually after handshake (TOFU model), not via OpenSSL CA chain.
- **Static helpers**: `GetPeerFingerprint()`, `GetPeerCertPEM()`, `GetX509Fingerprint()`, `GetFingerprintFromPEM()` for fingerprint extraction and verification.
- **LANSyncClient::Connect()**: After successful TLS handshake, extracts peer cert fingerprint and certPEM. If `expectedFingerprint_` is set (from stored TrustedPeer), verifies it matches the peer's cert and rejects on mismatch.
- **PairingManager**: `GetLocalPeerId()` uses cert fingerprint prefix (stable cross-session ID). `HandlePairBegin()` receives client certPEM in body, returns server certPEM in response. `HandlePairVerify()` stores client certPEM in TrustedPeer. `PairWithPeer()` sends client certPEM, extracts server certPEM from response, stores in TrustedPeer on success.
- **PlatformKeyStore**: `IsTrusted()` now matches fingerprint against stored `certPEM` fields of all trusted peers (was broken — looked for `${fingerprint}.json` files that never existed).
- **SaveStateLANSync::DoSyncWithPeer()**: After TLS handshake, verifies peer's cert fingerprint against all stored TrustedPeer certs. If any trusted peers exist and this fingerprint isn't among them, connection is rejected.
- **RAND_bytes for nonce**: Replaced weak timestamp-based nonce with OpenSSL CSPRNG.

### Issues #11, #12, #14 — Discovery Bugfixes

**Issue #11 — avahi-daemon error tidak tampil di UI:** `LANSyncDiscovery` sekarang menerima callback error dari `MDNSBrowser` dan mengirimkannya ke `LANSyncScreen` via `DiscoveryEvent::ERROR`.

**Issue #14 — mDNS discovery error tidak tampil di UI:** `MDNSBrowser::OnError` callback + `DiscoveryEvent::ERROR` → `LANSyncScreen` menampilkan `discoveryError_` di `serverStatus_`.

### Issue #7 — Pairing tidak di-enforce di endpoint data

**Fix (Session 2026-07-13):**
- `TLSContext`: `InitClient()` loads stored cert into `ctxClient_` via `SSL_CTX_use_certificate_chain_file` + `SSL_CTX_use_PrivateKey_file`.
- `LANSyncServer`: `HandleConnection()` sets `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`, stores `currentSSL_` for handler access.
- `SaveStateLANSync`: `IsPeerTrusted()` checks `GetCurrentSSL()` fingerprint against stored trusted peers. Applied to all 3 `/states` handlers (list, get, put) → returns HTTP 403 with JSON error.
- **Commit**: `4a9203ba0c`.

### Issue #8 — LWW conflict tie gap

**Fix (Session 2026-07-13):**
- `ResolveConflict()`: Added `else` branch when mtime equal. Compares SHA256 checksum. Diverged content → backup to `.conflict`, replaces with remote. Identical content → skip (log only).
- **Commit**: `1a91fe810e`.

### Issue #9 — PUT size limit

**Fix (Session 2026-07-13):**
- `HandleConnection()`: Reads `Content-Length` header via `atoll`. If > 100MB, sends `413 Payload Too Large` before `Dispatch()`.
- **Commit**: `40fe1cd2de`.

### Issue #10 — Parse gameId/slot

**Fix (Session 2026-07-13):**
- `rfind('_')` confirmed correct for `<gameId>_<slot>.ppst` format (slot is `std::to_string(int)`, no underscores).
- Added `ParseSaveFilename()` helper: `strtol` with proper error checking, gameId empty guard, slot range 0-999.
- Refactored 3 duplicated call sites → single function.
- **Commit**: `67421d73bb`.

### Issue #13 — Self-Detection via Peer ID

**Problem:** Android NsdManager tidak punya `AVAHI_LOOKUP_RESULT_LOCAL` seperti Avahi. Self-discovery hanya dimitigasi oleh deviceName comparison (false negative jika nama sama) + loopback/APIPA filter (tidak tangkap LAN IP asli).

**Fix — 3 lapis self-detection yang saling melengkapi:**

| Layer | Mekanisme | Platform |
|---|---|---|
| 1. Avahi LOCAL flag | `AVAHI_LOOKUP_RESULT_LOCAL` (existing) | Linux ✅ |
| 2. Peer ID comparison | TXT `"id"` sekarang berisi cert fingerprint (bukan deviceName). `OnPeerFound`/`OnPeerLost` skip jika `peer.peerId == ourPeerId_`. | **ALL** |
| 3. DeviceName filter | `peer.deviceName == deviceName_` (existing, jadi fallback) | ALL ✅ |
| 4. Loopback/APIPA | `127.0.0.1`, `::1`, `169.254.x.x` (existing) | ALL ✅ |

**Perubahan kunci:**
- **TXT `id` diisi cert fingerprint**: `MDNS_Linux.cpp` (Avahi) dan `MDNS_Android.cpp` (JNI forward) + `LANSyncMDNSHelper.java` (NsdManager setAttribute) — bukan deviceName lagi.
- **Java forward peerId**: `nativeOnPeerFound` signature tambah `String peerId` → diisi dari `resolvedInfo.getAttributes().get("id")` yang sudah di-parse tapi dibuang.
- **Self-check di C++**: `LANSyncDiscovery` simpan `ourPeerId_` dari `tlsCtx_->GetCertFingerprint()`. `OnPeerFound` dan `OnPeerLost` skip jika match.
- **Fallback untuk old device**: Jika TXT `id` masih berisi deviceName (device belum update), `peer.peerId == ourPeerId_` tidak match → layer 3 (deviceName) dan 4 (loopback) tetap jalan.

---

## 🧹 Code Review Findings — Technical Debt

### TD1 — Duplikasi Device ID

`SaveStateLANSync::GetDeviceId()` dan `PairingManager::GetLocalPeerId()` punya logika **identik**: `fp.size()>=8 → "PPSSPP-"+fp[0:8]`, else `mac.size()>=4 → "PPSSPP-"+mac[-4]`, else `"PPSSPP-Unknown"`. Total 4–5 tempat (termasuk `LANSyncConfig.cpp:44`).

**Fix:** pindahkan ke `TLSContext::GetDeviceId()` (fingerprint sumbernya di sana), panggil dari kedua fungsi → satu sumber kebenaran.

### TD2 — Race `syncing_` di `SyncWithAllPeers` (BUG USER-FACING)

`syncing_` adalah `atomic<bool>` tunggal. `SyncWithPeer()` pakai `syncing_.exchange(true)` → kalau ≥2 peer, hanya peer pertama yang jalan; sisanya `return` diam-diam (tidak sync). `AutoSyncLoop()` menutupi karena busy-wait antar peer, tapi **"Sync All" manual (`SyncWithAllPeers`) tidak menunggu → cuma 1 device tersinkron, tanpa error**.

**Fix (B, rekomendasi):** ganti `syncing_` dengan `std::set<std::string> activeSyncKeys_` (key = `host:port`) + `atomic<bool> cancelRequested_`. Izinkan sync konkuren antar peer beda, abaikan duplicate request ke peer sama. `IsSyncing()` → `!activeSyncKeys_.empty()`.

### TD3 — Parser JSON Manual Berulang

Tiga implementasi ad-hoc: `ExtractJsonField` (string+number), `extractJsonStr` λ (×2, string only, di `LANSyncPairing.cpp`), `findField` λ (`LANSyncMetadata.cpp`, string+number). Semua berhenti di `"` pertama (tidak handle `\"` escape) → rapuh kalau nilai mengandung `"`. Bukti historis: STATUS CR4 pernah kena double-escape PEM.

**Fix:** satu helper `LANSync/LANSyncJson.h` (`JsonGetString` / `JsonGetNumber`) dengan gason (lib JSON existing PPSSPP). Replace semua ad-hoc parser.

### TD4 — `isdigit` UB (KOREKSI ATRIBUSI)

UB **bukan** di `ExtractJsonField` (sudah benar pakai `isdigit((unsigned char)json[pos])` di `SaveStateLANSync.cpp:696`). UB ada di **`LANSyncMetadata.cpp:42`**:

```cpp
while (pos < data.size() && (isdigit(data[pos]) || data[pos] == '-')) {  // data[pos] = char (signed) → byte >=0x80 negatif → UB
```

**Dampak:** rendah (field `hlc`/`originalMtime`/`peerId` ASCII/hex, tidak memicu praktis), tapi latent UB.

**Fix:** `isdigit((unsigned char)data[pos])`. 1 baris.

### TD6 — Hint penolakan permission tidak menonjol

Pesan *"enable Nearby devices / Location / Local network in Settings…"* saat permission ditolak hanya muncul sebagai teks kecil di `serverStatus_` (`LANSyncScreen.cpp`). Konsisten dengan error discovery lain, tapi kurang menonjol sehingga user bisa kelewat.

**Fix (Session 2026-07-17 part 2):** `UI::MessagePopupScreen` ditampilkan sekali-pakai di `LANSyncScreen::update()` saat `discoveryError_` ter-set. Guard `permissionPopupShown_` mencegah popup berulang; di-reset saat error cleared.

### TD7 — Hint belum dilokalisasi

`buildPermissionHint()` (`LANSyncMDNSHelper.java`) mengembalikan string English literal. PPSSPP punya sistem terjemahan: sisi C++ via `GetSysString` / `lang/*.ini`, sisi Android via `res/values/strings.xml` + `getString()`.

**Fix (Session 2026-07-17 part 2):** Java mengirim kode (`"LANSyncPermNearby"` / `"LANSyncPermLocation"` / `"LANSyncPermLocalNet"` / `"LANSyncPermGeneric"`) melalui `nativeOnDiscoveryError`. C++ translate via `n->T(code)` di `I18NCat::NETWORKING`. Key ditambah ke `en_US.ini`. Fallback otomatis untuk error non-permission (desktop).

### TD1–TD4 Fix Details

| # | Isu | Fix | Files |
|---|-----|-----|-------|
| TD1 | Duplikasi GetDeviceId/GetLocalPeerId | `TLSContext::GetDeviceId()` ditambah di `TLSTransport.h/.cpp`; `SaveStateLANSync::GetDeviceId()` dan `PairingManager::GetLocalPeerId()` delegate ke sana | `LANSync/TLSTransport.h`, `TLSTransport.cpp`, `SaveStateLANSync.cpp`, `LANSyncPairing.cpp` |
| TD2 | `SyncWithAllPeers()` cuma sync 1 peer | `atomic<bool> syncing_` diganti `set<string> activeSyncKeys_` (key `host:port`) + `atomic<bool> cancelRequested_`. `SyncWithAllPeers()` now syncs all peers concurrently. `CancelSync()` pake flag terpisah. | `SaveStateLANSync.h`, `SaveStateLANSync.cpp` |
| TD3 | 4 parser JSON manual rapuh | Buat `LANSyncJson.h` dengan `JsonGetString`/`JsonGetInt64` pake gason (lib JSON PPSSPP existing). Replace semua ad-hoc parser. PEM di transport di-escape (`JsonEscape`) untuk valid JSON. | `LANSync/LANSyncJson.h` (baru), `SaveStateLANSync.cpp`, `LANSyncPairing.cpp`, `LANSyncMetadata.cpp` |
| TD4 | `isdigit` UB | Cast `(unsigned char)`. 1 baris. | `LANSync/LANSyncMetadata.cpp:42` |

**PEM JSON Escaping Note:** CR4 (session 2026-07-10) removed `JsonEscape` from transport because old custom parsers didn't handle escape sequences → double-escape. Sekarang dengan gason (`JsonGetString`) yang handle escape sequences dengan benar, `JsonEscape` dipakai di transport. Flow: `PairWithPeer` escape → kirim → `HandlePairBegin` gason decode → simpan raw → `SavePeer` escape untuk file. Single-escaping, no double-escape.

---

## 🐛 Code Review Findings — Bug, Race, Memory, Security

Hasil review mendalam terhadap **seluruh 34 file** di direktori `LANSync/` — fokus pada security, race condition, memory leak, dan logic bug.

### SR1 — Directory Traversal — **FALSE POSITIVE**

Validasi: `gameId` di-extract via `subpath.substr(0, subpath.find('/'))` → selalu segment tunggal sebelum `/` pertama. Path: `stateDir_ / (gameId + "_" + slot + ".ppst")` → flat filename. Test: `/states/../../etc/passwd/0` → gameId=`".."`, filename=`.._0.ppst` (aman).

### SR2 — JSON Injection — **FIXED**

**Fix (Session 2026-07-14):**
1. `HandleListSaveStates`: `JsonEscape(gameId)` di `snprintf` response
2. `IsValidGameId()` validation di `HandleGetSaveState` + `HandlePutSaveState` — tolak `/`, `\`, `"`, `'`, `..`
3. `LANSyncMetadata::Save`: `JsonEscape(peerId)` di sidecar JSON

Build + 43/43 tests pass.

### SR3 — Data Race `confirmPin_`

```cpp
// PairWithPeer thread (LANSyncPairing.cpp:~200):
{
    std::lock_guard<std::mutex> l(mutex_);          // ← mutex_
    enteredPin = pending_ ? confirmPin_ : "";        // ← READ
}

// UI thread - ConfirmPin (LANSyncPairing.cpp:~280):
{
    std::lock_guard<std::mutex> lk(dialogMutex_);   // ← dialogMutex_ (BEDA!)
    confirmPin_ = pin;                                // ← WRITE
}
```

**Fix:** Akses `confirmPin_` hanya di bawah satu mutex — `PairingManager::ConfirmPin()` kini tulis `confirmPin_` di bawah `mutex_` (sama dengan reader di thread `PairWithPeer`), bukan `dialogMutex_`. Menghilangkan UB.

### SR4 — `currentSSL_` Race Condition

```cpp
// LANSyncServer.cpp - HandleConnection (per thread koneksi):
currentSSL_ = ssl;     // write — di-overwrite koneksi lain!
// ... processing ...
currentSSL_ = nullptr;

// IsPeerTrusted di SaveStateLANSync.cpp:
SSL *ssl = server->GetCurrentSSL();  // bisa dari koneksi LAIN!
```

**Dampak:** Verifikasi TOFU fingerprint bisa mengecek cert peer A pada request peer B → bypass keamanan.

**Fix:** Hapus member `currentSSL_` + getter `GetCurrentSSL()`. Tambah `struct ConnectionCtx { SSL *ssl; std::string peerFingerprint; }`. `RequestHandler` typedef kini membawa param ke-4 `const ConnectionCtx &`. Fingerprint dihitung **sekali** di `HandleConnection` dan di-pass ke handler via closure.

### SR5 — Non-blocking Flag Leak

```cpp
// TLSTransport.cpp - SSLHandshakeWithTimeout
bool wasBlocking = ...;
if (wasBlocking) fd_util::SetNonBlocking(fd, true);
// ... handshake logic ...
if (ret == 1) {
    if (wasBlocking) fd_util::SetNonBlocking(fd, false);  // ✅ restore
    return true;
}
if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
    return false;  // ❌ LEAK: fd tetap NONBLOCK!
```

**Fix:** RAII scope-guard (`restoreBlocking()`) dipanggil di **semua** exit path (success + 3 failure). Tidak ada lagi fd NONBLOCK bocor.

### SR6 — OpenSSL Return Value Unchecked

```cpp
// TLSTransport.cpp - InitServer
SSL_CTX_use_certificate_file(ctxServer_, ..., SSL_FILETYPE_PEM);  // return value ignored
SSL_CTX_use_PrivateKey_file(ctxServer_, ..., SSL_FILETYPE_PEM);   // return value ignored
```

**Fix:** Selalu cek `<= 0` dan panggil `ERR_print_errors_fp`. `InitServer`/`InitClient`: tambah `SSL_CTX_check_private_key`. `LANSyncClient::Connect`: cek `SSL_set_fd`. On failure: free ctx + return false.

### SR7 — Non-portable `uint64_t` Cast

```cpp
// HLC.h - FromString
sscanf(s.c_str(), "%llu:%u",
    (unsigned long long *)&h.physical,  // ⚠️ uint64_t != unsigned long long di ILP64
    &h.logical);
```

**Fix:** `std::from_chars` (`<charconv>`) untuk `physical`/`logical`, dengan cek `ec != errc()`. Menghilangkan type-punning UB dan cek parse error.

### SR8 — AutoSyncLoop Busy-wait

```cpp
// SaveStateLANSync.cpp
for (int i = 0; i < interval * 10 && autoSyncRunning_; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

**Fix:** Loop diganti `sleep_for(seconds(1)) × interval` (responsif ke `autoSyncRunning_`, tidak lagi 100ms×interval×10).

### SR9 — Thread Handle Tidak Direset

```cpp
// LANSyncDiscovery.cpp - Stop
probeCv_.notify_all();
if (probeThread_.joinable())
    probeThread_.join();
// probeThread_ tetap menyimpan handle lama (sudah selesai)
```

**Fix:** `probeThread_ = std::thread();` setelah `join()` untuk clarity.

---

## 🧪 Code Review — Production Hardening (CR1–CR4)

### CR1 — `FindPeer(peer.peerId)` mismatch

**Severity:** HIGH

**Finding:** mDNS returns MAC-based ID, pairing stores fingerprint-based ID. Pre-connect TOFU verification (`FindPeer`) silently skipped — device never trusted even after pairing.

**Fix:** Replaced pre-connect `FindPeer` with post-connect `IsTrusted(fingerprint)`. After TLS handshake, loads all stored peers and checks if the received fingerprint matches any known certPEM. If trusted peers exist and none match, rejects connection. Handles IP changes, peerId mismatches, and first-use TOFU.

### CR2 — `GetPeerCertPEM()` UB

**Severity:** MEDIUM

**Finding:** `std::string(nullptr, 0)` if `BIO_get_mem_data` returns 0 on failed PEM write.

**Fix:** Added `len > 0 && data` guard + explicit `PEM_write_bio_X509` return check + `BIO_new` null check.

### CR3 — `SavePeer()` 2048-byte buffer overflow

**Severity:** MEDIUM

**Finding:** PEM cert (~1-2KB) + JSON wrapper can exceed 2048-byte limit. Save silently fails.

**Fix:** Replaced `char buf[2048]` + `snprintf` with `std::string` concatenation + `JsonEscape` for valid JSON output (both Linux + Android).

### CR4 — PEM embedded in JSON without string escaping

**Severity:** LOW/CRITICAL

**Finding:** PEM embedded in JSON without string escaping — technically invalid JSON. Initial fix applied `JsonEscape` at both transport and file layers, but transport + PendingNonce + SavePeer chain caused double-escape → `GetFingerprintFromPEM` receives `\n` as backslash+n, not newline → parse fails.

**Fix:** `JsonEscape` removed from transport layer (`PairWithPeer` request, `HandlePairBegin` response). **Only `SavePeer` escapes** for file storage. HTTP uses raw PEM (safe: custom parser reads until next `"`, PEM has no `"`). `extractStr` handles both escaped and legacy unescaped files.

---

## 📅 Riwayat Perbaikan (Chronological)

### Session 2026-07-09 — Android Build + Bugfixes

- **Android build**: Built APK for arm64-v8a + armeabi-v7a (AGP 9.2.1, Gradle 9.4.1, NDK 29). Installed and ran on Infinix X6532 (Android).
- **`ext/openssl/build_android.sh`**: Added `-fPIC` (fix `R_ARM_REL32` against `OPENSSL_armcap_P`), `-DOPENSSL_NO_STDIO` + `no-apps` (fix `undefined stdin/stderr`), `no-ui-console`, `no-engine`, `no-cmp` for armeabi-v7a compat.
- **`LANSync/TLSTransport.cpp`**: Converted FILE-based PEM I/O to BIO-based — required by `OPENSSL_NO_STDIO` on Android.
- **`LANSync/MDNS_Linux.cpp`**: Added `AVAHI_LOOKUP_RESULT_LOCAL` check — prevents self-discovery.
- **`LANSync/LANSyncDiscovery.cpp`**: Added device name comparison + loopback/APIPA filter. Removed `fe80:` filter (too broad).
- **`LANSync/LANSyncClient.cpp`**: Fixed upload protocol mismatch — changed `UploadFile()` from `POST` to `PUT`.

### Session 2026-07-10 — Discovery Bugfixes (#11, #12, #14) + TLS Fix (#6)

- **Issues #11, #12, #14**: All fixed & verified (both Linux SDL + Android APK builds pass).
- **Issue #6 — TLS antar-device gagal**: Dual SSL_CTX, TOFU fingerprint, RAND_bytes for nonce. 6 files modified.

### Session 2026-07-10 — Code Review (CR1–CR4)

Four findings from code review of the TLS fix implementation. All fixed.

### Session 2026-07-13 — CR1-CR4 Code Review Hardening + Issues #7 #8 #9 #10

- **CR1–CR4**: All fixed & verified (SDL Linux build passes).
- **Issue #7**: Pairing enforcement at `/states` endpoint — `IsPeerTrusted()` via `GetCurrentSSL()`.
- **Issue #8**: LWW conflict tie gap — SHA256 checksum tie-break.
- **Issue #9**: PUT size limit — 100MB cap.
- **Issue #10**: Parse gameId/slot — `ParseSaveFilename()` helper.

### Session 2026-07-13 (part 2) — Self-Detection via Peer ID (#13)

3 lapis self-detection + TXT `id` diisi cert fingerprint. Fallback untuk old device.

### Session 2026-07-13 (part 3) — TD1–TD4 Fix + TD6-TD7 Design

TD1–TD4 fixed & verified. TD5 tetap OPEN. PEM JSON escaping note documented.

### Session 2026-07-14 — Android mDNS permission auto-restart

`LANSyncMDNSHelper` guarded start + pending state + replay on grant. Minor fixes (grantResults loop, indentation, pending flag clear).

### Session 2026-07-14 (part 2) — Android 13 NSD permission + denial UX

Root cause: `targetSdk = 37` activates Android 13 `NEARBY_WIFI_DEVICES` rule. Manifest + `ensurePermissionsGranted()` + `buildPermissionHint()` + `nativeOnDiscoveryError` + `DiscoveryEvent::ERROR` chain.

### Session 2026-07-16 — Remediasi SR3–SR9 + TD5 (Production Hardening)

- **SR4** (`currentSSL_` race): ConnectionCtx per-connection. 5 files modified.
- **SR3** (`confirmPin_` race): Single mutex.
- **SR5**: RAII restore-blocking on all exit paths.
- **SR6**: OpenSSL return value checks.
- **SR7**: `std::from_chars` for HLC.
- **SR8**: `sleep_for(seconds(1))` instead of 100ms busy-wait.
- **SR9**: `probeThread_` reset after join.
- **TD5**: HLC preparatory (non-breaking, hlcPhysical/hlcLogical on wire).
- **CMake**: `USE_TSAN` option.

**Verifikasi:**
- Core + PPSSPPSDL full build → 0 error.
- Test 7.5 (SR4 concurrent regression) PASS: clientA 200, clientC 200, clientB 403.

### Session 2026-07-17 — TSAN Verification + Smoke Test

- **Build TSAN**: Clang 22.1.8, `-DUSE_TSAN=ON`, 0 error.
- **TSAN runtime**: 16 warnings, **0 di LANSync** (all upstream).
- **SR3 (confirmPin_)**: Zero race — pairing flow + concurrent test clean.
- **SR4 (currentSSL_/ConnectionCtx)**: Zero race — 3 simultaneous connections clean.
- **Full smoke test**: 44/44 individual tests PASS (Test 1–9 incl. 7.5 pairing + concurrent).
- **Verdict:** #11 VERIFIED, #12 VERIFIED.

### Session 2026-07-17 (part 2) — TD6+TD7: Localize Permission Hint + Native Popup

- **TD7**: `buildPermissionHint()` → INI keys (`LANSyncPermNearby/Location/LocalNet/Generic`). C++ translate via `n->T()`. `en_US.ini` 4 keys.
- **TD6**: `UI::MessagePopupScreen` once per error via `permissionPopupShown_` guard. Reset on error cleared.
- Build: 0 error.
