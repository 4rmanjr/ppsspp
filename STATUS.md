# LAN Save State Sync (PPSSPP-LANSYNC) — Status

## 📐 Rules & Constraints

- **Zero upstream deletion**: Semua kode LANSync digate dengan `#ifdef PPSSPP_LANSYNC` + comment `// [PPSSPP-FORK]`
- **Additive-only**: Jangan hapus atau ubah existing logic — tambah di sampingnya
- **Konvensi commit**: `type(lansync): deskripsi` + footer `[PPSSPP-FORK]`
- **PRIVATE define sudah diganti PUBLIC**: Jangan balikin ke PRIVATE
- **File scope**: Semua file baru di `LANSync/`, modify existing file minimal (hanya tambah `#ifdef` block)

## ✅ Done

### Phase 1 — Core Implementation (19 commits)

#### Transport & Crypto
- TLS 1.3 with self-signed ECDSA P-256 cert, auto-generated on first run
- `PlatformKeyStore`: file-based (Linux), stub (Android)
- `TLSContext` / `TLSTransport`: init client + server contexts

#### HTTP Server & Client
- `LANSyncServer`: TLS HTTP/1.1 server, port 27314, routes: `/states` (GET list, GET file, PUT file), `/pair/begin`, `/pair/verify`
- `LANSyncClient`: TLS HTTP client for peer communication

#### Discovery
- `MDNS` abstraction: `MDNSAnnouncer` + `MDNSBrowser`
- `MDNS_Linux.cpp`: Avahi client (functional)
- `MDNS_Android.cpp`: Full NsdManager implementation via JNI
- `LANSyncDiscovery`: mDNS announcer/browser + manual peer + periodic probe thread

#### Pairing
- `PairingManager`: 6-digit PIN via truncated SHA256, TOFU trust model
- HTTP endpoints: `POST /pair/begin` (returns nonce), `POST /pair/verify` (accepts nonce+pin+peerId)

#### Config
- `LANSyncConfig`: device name, port, enabled, auto-sync, paired peers, retry settings
- `Core/Config.h` + `Config.cpp`: `#ifdef PPSSPP_LANSYNC` fields + INI section `[LANSync]`

#### UI Integration
- `LANSyncScreen`: peer list, sync button, progress display, conflict viewer
- `LANSyncPairingDialog`: PIN display (initiator) and PIN entry (receiver)
- `LANSyncConflictScreen`: list `.ppst.conflict` files, keep local/remote actions
- `MainScreen`: "LAN Sync" button in right column
- `GameSettingsScreen`: "LAN Sync" option in Networking tab
- `PauseScreen`: "Sync Saves Now" option
- `NativeApp.cpp`: `g_LANSync` global, init/shutdown, `--lansync-enabled` / `--lansync-disabled` / `--lansync-port` CLI flags

#### Build System
- `CMakeLists.txt`: `option(PPSSPP_LANSYNC)`, `find_package(OpenSSL)`, Avahi linkage
- `android/jni/Locals.mk`: OpenSSL per-ABI linkage for ndk-build
- `android/build.gradle.kts`: `-DPPSSPP_LANSYNC=ON` for normal/gold/vr flavors
- LANSync sources part of `Core` library

#### ODR Fix (Session 2026-07-06)
- `target_compile_definitions PRIVATE → PUBLIC`: PPSSPPSDL now sees same `Config` struct layout as Core
- `discovery_ unique_ptr → shared_ptr`: `LANSyncDiscovery` uses `enable_shared_from_this`
- `Initialize()` auto-calls `StartServer()` + `StartDiscovery()` when `--lansync-enabled`
- Missing `#include "LANSyncScreen.h"` in `MainScreen.cpp` and `GameSettingsScreen.cpp`

### Phase 2 — Complete (8 tasks, 30+ commits)

#### Task 1: Integration Smoke Test
- Script: `test/lansync_smoke_test.sh` — 10/10 tests PASS
- Tests: launch, HTTP /states (empty/populated), PUT/GET save state, LWW conflict rename, PIN pairing flow, error handling, TLS connectivity, large save (100MB+), cleanup

#### Task 2: Manual Peer Input UI (SKIPPED)
- Backend `LANSyncDiscovery::AddManualPeer()` exists
- UI not built — mDNS auto-discovery preferred

#### Task 3: Discovery Event Callback Wiring
- `SaveStateLANSync` forwards `DiscoveryEvent` to `LANSyncScreen`
- UI reacts to `PEER_FOUND`/`PEER_LOST` via `peersDirty_` flag
- 60-frame poll fallback when no events fire

#### Task 4: PIN Pairing Dialog
- `LANSyncPairingDialog.h/.cpp` — dialog screen with PIN display and entry
- `PairWithPeer()` shows dialog instead of auto-confirm; background thread waits on condition variable
- `SetScreenManager()` forwarding from `SaveStateLANSync` → `PairingManager`

#### Task 5: Conflict Viewer Screen
- `LANSyncConflictScreen.h/.cpp` — scans `.ppst.conflict` files
- Actions: "Keep Local" (delete .conflict), "Keep Remote" (rename .conflict → .ppst)
- "View Conflicts" button on `LANSyncScreen`

#### Task 6: Sync Error Handling
- Configurable retry count (default 3) and delay (default 2s)
- Retry loop in `DoSyncWithPeer()` wraps connect + sync
- `CancelSync()` cleans up orphan `.tmp` files
- INI settings: `LANSyncRetryCount`, `LANSyncRetryDelayMs`

#### Task 7: Android mDNS (NsdManager)
- `android/jni/LANSyncMDNSHelper.java` — NsdManager wrapper with discovery + announce
- `LANSync/MDNS_Android.cpp` — full JNI bridge with JNICache, EnsureAttached, callbacks
- `LANSync/MDNS.cpp` — Android factory functions via `PPSSPP_PLATFORM(ANDROID)`
- `android/jni/Android.mk` + `Locals.mk` — added to ndk-build
- `PpssppActivity.java` — init/destroy lifecycle

#### Task 8: Background Auto-Sync
- Periodic polling thread in `SaveStateLANSync`
- Syncs all discovered peers when `bAutoSync` enabled
- Configurable interval (default 60s)
- Start/stop in `Initialize()` / `Shutdown()`

### Android Verification (Session 2026-07-07)
- **CMakeLists.txt**: Android OpenSSL path resolution per ABI + IMPORTED targets (OpenSSL::SSL, OpenSSL::Crypto)
- **native target**: `PPSSPP_LANSYNC` compile definition + include path on Android
- **Lifecycle hooks**: `SaveStateLANSync::Pause()/Resume()` wired to `onPause`/`onResume` via UIMessage
- **MDNS_Android.cpp fix**: Include path `android/jni/app-android.h`, JNI callbacks in `LANSync` namespace, static lambda capture workaround
- **Build verified**: `libppsspp_jni.so` links successfully for arm64-v8a and x86_64
- **`ext/openssl/build_android.sh`**: Build OpenSSL 3.3.0 for all 4 Android ABIs; uses `-fvisibility=hidden` to fix libkirk symbol collision; output layout matches CMakeLists.txt expectations
- **Prebuilt binaries removed from git**: `ext/openssl/android/` now gitignored — run `./ext/openssl/build_android.sh` before Android build

### Post-Phase 2 Fixes (Session 2026-07-07)
- **`fd_util::ConnectWithTimeout()`**: Non-blocking `connect()` + `select()` with proper timeout; uses `getaddrinfo()` for DNS (IPv4+IPv6) and `getsockopt(SO_ERROR)` for connection verification
- **`LANSyncClient::Connect()`**: Replaced blocking `socket()+gethostbyname()+connect()` with `fd_util::ConnectWithTimeout()` — connect timeout now actually respects user-specified timeout (was at mercy of kernel TCP retransmission ~20-120s)
- **`LANSyncDiscovery::TryConnectManual()`**: Same migration; bonus: now supports hostname (was raw IP only via `inet_addr()`)
- **Build artifact cleanup**: APK/idsig files removed from git; `*.apk`/`*.idsig` added to `.gitignore`; `.worktrees/` and `ext/libmgba` removed from branch
- **CLI disable flag**: Added `--lansync-disabled` (transient via `DoNotSaveSetting`, follows PPSSPP `--fullscreen`/`--windowed` pattern); `--lansync-enabled` also made transient + switched from `strncmp` (prefix) to `strcmp` (exact match)
- **TLS non-blocking handshake**: Added `SSLHandshakeWithTimeout()` in `TLSTransport` — sets socket non-blocking, loops on `SSL_connect()`/`SSL_accept()` with `select()` + deadline, restores blocking mode on success. Migrated both `LANSyncClient::Connect()` and `LANSyncServer::HandleConnection()`. Verified with TLS 1.3 via openssl s_client (handshake + HTTP request/response ✅).

### Session 2026-07-09 — Android Build + Bugfixes
- **Android build**: Built APK for arm64-v8a + armeabi-v7a (AGP 9.2.1, Gradle 9.4.1, NDK 29). Installed and ran on Infinix X6532 (Android).
- **`ext/openssl/build_android.sh`**: Added `-fPIC` (fix `R_ARM_REL32` against `OPENSSL_armcap_P`), `-DOPENSSL_NO_STDIO` + `no-apps` (fix `undefined stdin/stderr`), `no-ui-console`, `no-engine`, `no-cmp` for armeabi-v7a compat.
- **`LANSync/TLSTransport.cpp`**: Converted FILE-based PEM I/O (`PEM_read_X509`, `PEM_write_X509`, etc.) to BIO-based (`PEM_read_bio_X509`, `PEM_write_bio_X509`, etc.) — required by `OPENSSL_NO_STDIO` on Android.
- **`LANSync/MDNS_Linux.cpp`**: Added `AVAHI_LOOKUP_RESULT_LOCAL` check in `ResolveCallback()` and `BrowseCallback()` — prevents self-discovery (device seeing own service on all interfaces as 3 separate entries: IPv6, 127.0.0.1, IPv4).
- **`LANSync/LANSyncDiscovery.cpp`**: Added device name comparison + loopback/APIPA filter (`127.0.0.1`, `::1`, `169.254.x.x`) as cross-platform safety net. Removed `fe80:` filter (was too broad — blocked all IPv6 link-local peers including remote Android devices).
- **`LANSync/LANSyncClient.cpp`**: Fixed upload protocol mismatch — changed `UploadFile()` from `POST` to `PUT` (server only handled `PUT`; client silently failed on all uploads with `method_not_allowed`).
- **Linux SDL close bug identified**: Missing `SDL_EVENT_WINDOW_CLOSE_REQUESTED` handler — SDL3 change, not yet fixed.

---

## ⚠️ Limitations & Known Issues

1. ~~**Runtime-untested on Android**~~: ✅ Tested on Infinix X6532 (Android). Build + install + discovery + pairing verified.
2. **OpenSSL required for Android**: `ext/openssl/build_android.sh` must be run (once) before Android build; needs NDK + internet
3. **Linux SDL close button broken**: `SDL_EVENT_WINDOW_CLOSE_REQUESTED` not handled (SDL3 migration gap). Use `--escape-exit` + ESC or Pause Menu → "Exit the emulator".
4. ~~**Linux requires `avahi-daemon`**: mDNS discovery silently fails if avahi-daemon is not running. No error is shown in UI.~~ ✅ Error shown in UI via `DiscoveryEvent::ERROR` (fix #11).
5. **Firewall**: mDNS (UDP 5353) and LANSync data (TCP 27314) must be open on local network.
6. **[2026-07-09] [KRITIS] TLS antar-device gagal (sync tidak jalan) — FIXED**: Lihat session 2026-07-10 di bawah.
7. **[2026-07-09] [TINGGI] Pairing tidak di-enforce di endpoint data — FIXED**: Lihat session 2026-07-13 di bawah.
8. **[2026-07-09] [SEDANG] Resolusi konflik murni mtime (LWW) + celah tie — FIXED**: Commit `1a91fe810e`.
9. **[2026-07-09] [RENDAH] Tidak ada batas ukuran PUT — FIXED**: Commit `40fe1cd2de`.
10. **[2026-07-09] [RENDAH] Parse gameId/slot asumsi 1 underscore — FIXED**: Commit `67421d73bb`.

### Session 2026-07-10 — Discovery Bugfixes (#11, #12, #14) + TLS Fix (#6) - IMPLEMENTED

**Issues #11, #12, #14 — ALL FIXED & VERIFIED (both Linux SDL + Android APK builds pass):**

**Issue #6 — TLS antar-device gagal — FIXED:**

Root cause: `TLSContext` single `ctx_` overwritten by `InitServer()` after `InitClient()`. When `LANSyncClient::Connect()` calls `GetSSLContext()` (which returned the server context after `InitServer()`), the client uses a `TLS_server_method()` SSL → handshake fails.

Fix (6 files modified):
- **Dual SSL_CTX**: `ctx_` split into `ctxServer_`/`ctxClient_`. `InitServer()` uses `TLS_server_method()`, `InitClient()` uses `TLS_client_method()`. Client calls `GetClientContext()`, server calls `GetServerContext()`.
- **SSL_VERIFY_NONE on client**: Fingerprint verified manually after handshake (TOFU model), not via OpenSSL CA chain.
- **Static helpers**: `GetPeerFingerprint()`, `GetPeerCertPEM()`, `GetX509Fingerprint()`, `GetFingerprintFromPEM()` for fingerprint extraction and verification.
- **LANSyncClient::Connect()**: After successful TLS handshake, extracts peer cert fingerprint and certPEM. If `expectedFingerprint_` is set (from stored TrustedPeer), verifies it matches the peer's cert and rejects on mismatch.
- **PairingManager**: `GetLocalPeerId()` uses cert fingerprint prefix (stable cross-session ID). `HandlePairBegin()` receives client certPEM in body, returns server certPEM in response. `HandlePairVerify()` stores client certPEM in TrustedPeer. `PairWithPeer()` sends client certPEM, extracts server certPEM from response, stores in TrustedPeer on success.
- **PlatformKeyStore**: `IsTrusted()` now matches fingerprint against stored `certPEM` fields of all trusted peers (was broken — looked for `${fingerprint}.json` files that never existed).
- **SaveStateLANSync::DoSyncWithPeer()**: After TLS handshake, verifies peer's cert fingerprint against all stored TrustedPeer certs. If any trusted peers exist and this fingerprint isn't among them, connection is rejected.
- **RAND_bytes for nonce**: Replaced weak timestamp-based nonce with OpenSSL CSPRNG.

### Code Review — Production Hardening (Session 2026-07-10)

Four findings from code review of the TLS fix implementation:

| # | Severity | Finding | Fix |
|---|----------|---------|-----|
| CR1 | HIGH | `FindPeer(peer.peerId)` mismatch — mDNS returns MAC-based ID, pairing stores fingerprint-based ID. Pre-connect TOFU verification silently skipped. | Replaced pre-connect `FindPeer` with post-connect `IsTrusted(fingerprint)`. After TLS handshake, loads all stored peers and checks if the received fingerprint matches any known certPEM. If trusted peers exist and none match, rejects connection. Handles IP changes, peerId mismatches, and first-use TOFU. |
| CR2 | MEDIUM | `GetPeerCertPEM()` UB: `std::string(nullptr, 0)` if `BIO_get_mem_data` returns 0 on failed PEM write. | Added `len > 0 && data` guard + explicit `PEM_write_bio_X509` return check + `BIO_new` null check. |
| CR3 | MEDIUM | `SavePeer()` fixed 2048-byte buffer overflow — PEM cert (~1-2KB) + JSON wrapper can exceed limit. Save silently fails. | Replaced `char buf[2048]` + `snprintf` with `std::string` concatenation + `JsonEscape` for valid JSON output (both Linux + Android). |
| CR4 | LOW/CRITICAL | PEM embedded in JSON without string escaping — technically invalid JSON. Initial fix applied `JsonEscape` at both transport and file layers, but transport + PendingNonce + SavePeer chain caused double-escape → `GetFingerprintFromPEM` receives `\n` as backslash+n, not newline → parse fails. | `JsonEscape` removed from transport layer (`PairWithPeer` request, `HandlePairBegin` response). **Only `SavePeer` escapes** for file storage. HTTP uses raw PEM (safe: custom parser reads until next `"`, PEM has no `"`). `extractStr` handles both escaped and legacy unescaped files. |

### Session 2026-07-13 — CR1-CR4 Code Review Hardening + Issues #7 #8 #9 #10 — ALL FIXED

**CR1-CR4 — ALL FIXED & VERIFIED (SDL Linux build passes):**
- **CR1**: Replaced pre-connect `FindPeer` with post-connect `IsTrusted(fingerprint)`. Handles IP changes, peerId mismatches, first-use TOFU.
- **CR2**: Added `len > 0 && data` guard + `PEM_write_bio_X509` return check + `BIO_new` null check.
- **CR3**: Replaced `char buf[2048]` + `snprintf` with `std::string` concatenation + `JsonEscape`.
- **CR4**: `JsonEscape` removed from transport layer (PairWithPeer, HandlePairBegin). Only SavePeer escapes for file. extractStr handles both escaped and legacy unescaped files.

**Issue #7 — Pairing enforcement at /states endpoint — FIXED:**
- `TLSContext`: `InitClient()` loads stored cert into `ctxClient_` via `SSL_CTX_use_certificate_chain_file` + `SSL_CTX_use_PrivateKey_file`.
- `LANSyncServer`: `HandleConnection()` sets `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`, stores `currentSSL_` for handler access.
- `SaveStateLANSync`: `IsPeerTrusted()` checks `GetCurrentSSL()` fingerprint against stored trusted peers. Applied to all 3 `/states` handlers (list, get, put) → returns HTTP 403 with JSON error.
- **Commit**: `4a9203ba0c`.

**Issue #8 — LWW conflict tie gap — FIXED:**
- `ResolveConflict()`: Added `else` branch when mtime equal. Compares SHA256 checksum. Diverged content → backup to `.conflict`, replaces with remote. Identical content → skip (log only).
- **Commit**: `1a91fe810e`.

**Issue #9 — PUT size limit — FIXED:**
- `HandleConnection()`: Reads `Content-Length` header via `atoll`. If > 100MB, sends `413 Payload Too Large` before `Dispatch()`.
- **Commit**: `40fe1cd2de`.

**Issue #10 — Parse gameId/slot — FIXED:**
- `rfind('_')` confirmed correct for `<gameId>_<slot>.ppst` format (slot is `std::to_string(int)`, no underscores).
- Added `ParseSaveFilename()` helper: `strtol` with proper error checking, gameId empty guard, slot range 0-999.
- Refactored 3 duplicated call sites → single function.
- **Commit**: `67421d73bb`.

### Session 2026-07-13 (part 2) — Self-Detection via Peer ID (#13) — FIXED

**Problem**: Android NsdManager tidak punya `AVAHI_LOOKUP_RESULT_LOCAL` seperti Avahi. Self-discovery hanya dimitigasi oleh deviceName comparison (false negative jika nama sama) + loopback/APIPA filter (tidak tangkap LAN IP asli).

**Fix** — 3 lapis self-detection yang saling melengkapi:
| Layer | Mekanisme | Platform |
|---|---|---|
| 1. Avahi LOCAL flag | `AVAHI_LOOKUP_RESULT_LOCAL` (existing) | Linux ✅ |
| 2. Peer ID comparison | TXT `"id"` sekarang berisi cert fingerprint (bukan deviceName). `OnPeerFound`/`OnPeerLost` skip jika `peer.peerId == ourPeerId_`. | **ALL** |
| 3. DeviceName filter | `peer.deviceName == deviceName_` (existing, jadi fallback) | ALL ✅ |
| 4. Loopback/APIPA | `127.0.0.1`, `::1`, `169.254.x.x` (existing) | ALL ✅ |

**Perubahan kunci**:
- **TXT `id` diisi cert fingerprint**: `MDNS_Linux.cpp` (Avahi) dan `MDNS_Android.cpp` (JNI forward) + `LANSyncMDNSHelper.java` (NsdManager setAttribute) — bukan deviceName lagi.
- **Java forward peerId**: `nativeOnPeerFound` signature tambah `String peerId` → diisi dari `resolvedInfo.getAttributes().get("id")` yang sudah di-parse tapi dibuang.
- **Self-check di C++**: `LANSyncDiscovery` simpan `ourPeerId_` dari `tlsCtx_->GetCertFingerprint()`. `OnPeerFound` dan `OnPeerLost` skip jika match.
- **Fallback untuk old device**: Jika TXT `id` masih berisi deviceName (device belum update), `peer.peerId == ourPeerId_` tidak match → layer 3 (deviceName) dan 4 (loopback) tetap jalan.

**Commit**: (this commit).

### Remaining Open Issues (Priority Order)

| # | Priority | Issue | Status |
|---|----------|-------|--------|
| 6 | ~~KRITIS~~ | ~~TLS antar-device gagal (sync tidak jalan)~~ | **FIXED** |
| 7 | ~~TINGGI~~ | ~~Pairing tidak di-enforce di endpoint data~~ | **FIXED** |
| 8 | ~~SEDANG~~ | ~~LWW conflict tie gap (mtime sama, isi beda)~~ | **FIXED** |
| 9 | ~~RENDAH~~ | ~~Tidak ada batas ukuran PUT~~ | **FIXED** |
| 10 | ~~RENDAH~~ | ~~Parse gameId/slot asumsi 1 underscore~~ | **FIXED** |
| 13 | ~~SEDANG~~ | ~~Android LOCAL guard (mitigated by deviceName filter)~~ | **FIXED** |

### Design Decision 2026-07-09 — Transport LAN Sync = IPv4-Only

**Keputusan**: Seluruh LAN sync berjalan di **IPv4 saja**, konsisten dengan server (`LANSyncServer.cpp` → `socket(AF_INET)`, `INADDR_ANY`). IPv6 sengaja tidak dipakai untuk menghindari pitfall scope-id / `fe80::` (alamat link-local IPv6 butuh zone id, sering gagal disambung meski server dual-stack).

**Implementasi (DONE)**:
- `MDNS_Linux.cpp`: `AVAHI_PROTO_UNSPEC` → `AVAHI_PROTO_INET` pada announcer (`MDNSAnnouncerLinux::Start`) dan browser (`MDNSBrowserLinux::Start`) → mDNS hanya IPv4.
- `LANSyncDiscovery.cpp`: `OnPeerFound` & `OnPeerLost` membuang peer dengan host IPv6 (deteksi via `host.find(':') != npos`) + filter loopback/APIPA tetap. Mencover Android (NsdManager tak bisa batasi protokol) dan defense-in-depth.
- `fd_util::ConnectWithTimeout` **tidak diubah** (util shared PPSSPP lain); client otomatis konek IPv4 karena discovery hanya mengembalikan IPv4.

**Dampak**: Ketidakcocokan IPv4(server)/IPv6(discovery) **RESOLVED by design**. Risiko residual: jaringan murni IPv6-only tak terdeteksi (sesuai pilihan IPv4-only).

---

## 🧹 Code Review Findings — Technical Debt (2026-07-13)

Hasil deep-dive alur kode LANSync (`SaveStateLANSync`, `LANSyncServer/Client`, `TLSTransport`, `LANSyncDiscovery`, `LANSyncPairing`, `LANSyncMetadata`, `LANSyncProtocol`). Semua item **OPEN** (belum di-fix). Mayoritas refactor/cleanup + 1 bug user-facing (TD2) + 1 UB latent (TD4). Bukan blocker fungsional.

| # | Isu | Severity | Lokasi | Status |
|---|-----|----------|--------|--------|
| TD1 | Duplikasi `GetDeviceId()` / `GetLocalPeerId()` | Rendah | `SaveStateLANSync.cpp:706,711` · `LANSyncPairing.cpp:68,73` · `LANSyncConfig.cpp:44` | OPEN |
| TD2 | **Bug:** `SyncWithAllPeers()` hanya sync 1 peer (flag `syncing_` global) | Medium | `SaveStateLANSync.cpp:362-378` | OPEN |
| TD3 | 3 parser JSON manual berbeda (`ExtractJsonField`, `extractJsonStr`×2, `findField`) | Rendah | `SaveStateLANSync.cpp:~688` · `LANSyncPairing.cpp` · `LANSyncMetadata.cpp:~32` | OPEN |
| TD4 | **UB:** `isdigit(data[pos])` tanpa cast `(unsigned char)` | Rendah (latent) | `LANSyncMetadata.cpp:42` | OPEN |
| TD5 | HLC dihitung tapi tidak dipakai di `ResolveConflict` (masih murni `mtime`+checksum) | Low–Med | `LANSyncProtocol.h` · `SaveStateLANSync.cpp` (`ResolveConflict`) | OPEN |

### TD1 — Duplikasi Device ID
`SaveStateLANSync::GetDeviceId()` dan `PairingManager::GetLocalPeerId()` punya logika **identik**: `fp.size()>=8 → "PPSSPP-"+fp[0:8]`, else `mac.size()>=4 → "PPSSPP-"+mac[-4]`, else `"PPSSPP-Unknown"`. Total 4–5 tempat (termasuk `LANSyncConfig.cpp:44`).
**Fix:** pindahkan ke `TLSContext::GetDeviceId()` (fingerprint sumbernya di sana), panggil dari kedua fungsi → satu sumber kebenaran.

### TD2 — Race `syncing_` di `SyncWithAllPeers` (BUG USER-FACING)
`syncing_` adalah `atomic<bool>` tunggal. `SyncWithPeer()` pakai `syncing_.exchange(true)` → kalau ≥2 peer, hanya peer pertama yang jalan; sisanya `return` diam-diam (tidak sync).
`AutoSyncLoop()` menutupi karena busy-wait antar peer, tapi **"Sync All" manual (`SyncWithAllPeers`) tidak menunggu → cuma 1 device tersinkron, tanpa error**.
**Fix (A, minimal):** `SyncWithAllPeers()` serialize seperti `AutoSyncLoop` (tunggu `syncing_` false tiap peer).
**Fix (B, rekomendasi):** ganti `syncing_` dengan `std::set<std::string> syncingKeys_` (key = `host:port`) — izinkan sync konkuren antar peer beda, abaikan duplicate request ke peer sama. `IsSyncing()` → `!syncingKeys_.empty()`.

### TD3 — Parser JSON Manual Berulang
Tiga implementasi ad-hoc: `ExtractJsonField` (string+number), `extractJsonStr` λ (×2, string only, di `LANSyncPairing.cpp`), `findField` λ (`LANSyncMetadata.cpp`, string+number). Semua berhenti di `"` pertama (tidak handle `\"` escape) → rapuh kalau nilai mengandung `"`. Bukti historis: STATUS CR4 pernah kena double-escape PEM.
**Fix:** satu helper `LANSync/LANSyncJson.h` (`JsonGetString` / `JsonGetNumber`) dipakai di ketiga lokasi. (Cek apakah PPSSPP punya lib JSON internal yg bisa dipakai — hindari duplikasi util.)

### TD4 — `isdigit` UB (KOREKSI ATRIBUSI)
⚠️ Klarifikasi: UB **bukan** di `ExtractJsonField` (sudah benar pakai `isdigit((unsigned char)json[pos])` di `SaveStateLANSync.cpp:696`). UB ada di **`LANSyncMetadata.cpp:42`**:
```cpp
while (pos < data.size() && (isdigit(data[pos]) || data[pos] == '-')) {  // data[pos] = char (signed) → byte >=0x80 negatif → UB
```
**Dampak:** rendah (field `hlc`/`originalMtime`/`peerId` ASCII/hex, tidak memicu praktis), tapi latent UB.
**Fix:** `isdigit((unsigned char)data[pos])`.

### TD5 — HLC Tidak Dipakai di Resolusi Konflik
`HLC` (`HLC.h`) punya `Tick`/`Merge`/`operator<`/`ConflictsWith` untuk total ordering causal lintas-device, tapi di `DoSyncWithPeer`/`ResolveConflict` HLC hanya di-`Tick` lalu di-`Save` ke sidecar. Keputusan konflik murni `remote.mtime > local.mtime` + checksum tie-break. Kasus #8 (mtime sama, isi beda) ditangani checksum tapi pemenang **arbitrer** ("remote wins"), bukan deterministik via logical clock.
**Catatan — butuh perubahan wire-format:** `SaveFileEntry` harus kirim HLC (`hlcPhysical`/`hlcLogical`); `HandleListSaveStates` kirim dari sidecar + `ParseSaveFileList` parse. Baru `ResolveConflict` bisa pakai `HLC::operator<`/`ConflictsWith`. Perlu dipertimbangkan break kompatibilitas peer lama.
**Severity:** Low–Med.

### Urutan Perbaikan yang Disarankan
1. **TD2** (bug user-facing) — opsi B (per-peer set).
2. **TD4** (1 baris, UB nyata).
3. **TD1 + TD3** (refactor: satukan device-ID & JSON parser).
4. **TD5** ( enhancements, butuh diskusi protocol change).

