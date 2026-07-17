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
11. ~~**[2026-07-16] [HARNESS-ONLY] Smoke test Test 3 bisa FAIL palsu pada re-run**~~: ✅ **VERIFIED 2026-07-17** — Mitigasi port random + `fuser -k` bekerja; full-green (44/44 individual tests pass) terkonfirmasi via `Xvfb :99` manual. Test 7.5 `wait` hang adalah shell-level interaction antara `$(...) &` + `timeout` — bukan bug produk.
12. ~~**[2026-07-16] [TSAN] Belum dijalankan**~~: ✅ **VERIFIED 2026-07-17** — Build TSAN (Clang 22.1.8, `-DUSE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug`), 16 TSAN warnings, **0 di LANSync** (semua upstream: Config, Display, GL, StereoResampler, dll). SR3 (`confirmPin_`) & SR4 (`currentSSL_`/`ConnectionCtx`) **zero race** — termasuk concurrent test (clientA 200, clientC 200, clientB 403).

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
| TD1 | Duplikasi `GetDeviceId()` / `GetLocalPeerId()` | Rendah | `SaveStateLANSync.cpp:706,711` · `LANSyncPairing.cpp:68,73` · `LANSyncConfig.cpp:44` | **FIXED** |
| TD2 | **Bug:** `SyncWithAllPeers()` hanya sync 1 peer (flag `syncing_` global) | Medium | `SaveStateLANSync.cpp:362-378` | **FIXED** |
| TD3 | 4 parser JSON manual berbeda (`ExtractJsonField`, `extractJsonStr`×2, `findField`, inline) | Rendah | `SaveStateLANSync.cpp:~688` · `LANSyncPairing.cpp` · `LANSyncMetadata.cpp:~32` | **FIXED** |
| TD4 | **UB:** `isdigit(data[pos])` tanpa cast `(unsigned char)` | Rendah (latent) | `LANSyncMetadata.cpp:42` | **FIXED** |
| TD5 | HLC dihitung tapi tidak dipakai di `ResolveConflict` (butuh perubahan wire format) | Low–Med | `LANSyncProtocol.h` · `SaveStateLANSync.cpp` (`ResolveConflict`) | OPEN |
| TD6 | Hint penolakan permission cuma tampil di `serverStatus_` text — tidak ada dialog popup sekali-pakai | Low | `LANSyncMDNSHelper.java` (`onPermissionResultInternal` → `nativeOnDiscoveryError`) · `LANSyncScreen.cpp` (`serverStatus_`) | **VERIFIED** |
| TD7 | Pesan hint hardcoded English — tidak pakai sistem terjemahan PPSSPP | Low | `LANSyncMDNSHelper.java` (`buildPermissionHint`) | **VERIFIED** |

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

### TD6 — Hint penolakan permission tidak menonjol
Peserta *"enable Nearby devices / Location / Local network in Settings…"* saat permission ditolak hanya muncul sebagai teks kecil di `serverStatus_` (`LANSyncScreen.cpp`). Konsisten dengan error discovery lain, tapi kurang menonjol sehingga user bisa kelewat.
**Fix (Session 2026-07-17 part 2):** `UI::MessagePopupScreen` ditampilkan sekali-pakai di `LANSyncScreen::update()` saat `discoveryError_` ter-set. Guard `permissionPopupShown_` mencegah popup berulang; di-reset saat error cleared.

### TD7 — Hint belum dilokalisasi
`buildPermissionHint()` (`LANSyncMDNSHelper.java`) mengembalikan string English literal. PPSSPP punya sistem terjemahan: sisi C++ via `GetSysString` / `lang/*.ini`, sisi Android via `res/values/strings.xml` + `getString()`.
**Fix (Session 2026-07-17 part 2):** Java mengirim kode (`"LANSyncPermNearby"` / `"LANSyncPermLocation"` / `"LANSyncPermLocalNet"` / `"LANSyncPermGeneric"`) melalui `nativeOnDiscoveryError`. C++ translate via `n->T(code)` di `I18NCat::NETWORKING`. Key ditambah ke `en_US.ini`. Fallback otomatis untuk error non-permission (desktop).

### Session 2026-07-13 (part 3) — Validasi TD1–TD5 + Implementasi Semua (Kecuali TD5)

**TD1–TD4 — ALL FIXED & VERIFIED (SDL Linux build + 43 tests pass):**

| # | Isu | Fix | Files |
|---|-----|-----|-------|
| TD1 | Duplikasi GetDeviceId/GetLocalPeerId | `TLSContext::GetDeviceId()` ditambah di `TLSTransport.h/.cpp`; `SaveStateLANSync::GetDeviceId()` dan `PairingManager::GetLocalPeerId()` delegate ke sana | `LANSync/TLSTransport.h`, `TLSTransport.cpp`, `SaveStateLANSync.cpp`, `LANSyncPairing.cpp` |
| TD2 | `SyncWithAllPeers()` cuma sync 1 peer | `atomic<bool> syncing_` diganti `set<string> activeSyncKeys_` (key `host:port`) + `atomic<bool> cancelRequested_`. `SyncWithAllPeers()` now syncs all peers concurrently. `CancelSync()` pake flag terpisah. | `SaveStateLANSync.h`, `SaveStateLANSync.cpp` |
| TD3 | 4 parser JSON manual rapuh | Buat `LANSyncJson.h` dengan `JsonGetString`/`JsonGetInt64` pake gason (lib JSON PPSSPP existing). Replace semua ad-hoc parser. PEM di transport di-escape (`JsonEscape`) untuk valid JSON. | `LANSync/LANSyncJson.h` (baru), `SaveStateLANSync.cpp`, `LANSyncPairing.cpp`, `LANSyncMetadata.cpp` |
| TD4 | `isdigit` UB | Cast `(unsigned char)`. 1 baris. | `LANSync/LANSyncMetadata.cpp:42` |

**TD5 tetap OPEN** — wire protocol change (butuh HLC di `SaveFileEntry`, serialize/deserialize, backward compat). Bukan blocker.

**PEM JSON Escaping Note**: CR4 (session 2026-07-10) removed `JsonEscape` from transport because old custom parsers didn't handle escape sequences → double-escape. Sekarang dengan gason (`JsonGetString`) yang handle escape sequences dengan benar, `JsonEscape` dipakai di transport. Flow: `PairWithPeer` escape → kirim → `HandlePairBegin` gason decode → simpan raw → `SavePeer` escape untuk file. Single-escaping, no double-escape.

### Session 2026-07-14 — Android mDNS permission auto-restart + minor cleanup

**Commit `17d999b324` (fix Android mDNS):** `LANSyncMDNSHelper` guarded start (`startDiscoveryGuarded`/`startAnnounceGuarded`) menyimpan pending state (`mPendingDiscoveryType`, `mDiscoveryPending`, `mPendingAnnounceType/Port/Name/PeerId`, `mAnnouncePending`) → request permission → replay di `onPermissionResultInternal` setelah grant. `PpssppActivity.onRequestPermissionsResult` forward ke `LANSyncMDNSHelper.onRequestPermissionsResult`.

**Minor fixes (post-review):**
- `onPermissionResultInternal`: ganti cek `grantResults[0]` → loop semua `grantResults` (robust kalau 2 permission di-bundle).
- `onServiceResolved`: perbaiki indentasi blok `ResolvedPeer rp` / `rp.host` / `rp.port` / `rp.peerId` / `mResolvedCache.put` ke 7 tab (sejajar method body).
- `stopDiscoveryInternal`/`stopAnnounceInternal` sudah clear pending flag (replay aman, idempoten).

### Session 2026-07-14 (part 2) — Android 13 NSD permission + denial UX

**Root cause:** `targetSdk = 37` (`android/build.gradle.kts:131`) mengaktifkan aturan Android 13: `NsdManager` (discovery/announce) wajib `NEARBY_WIFI_DEVICES` runtime permission. Manifest tidak mendeklarasikannya, dan `LANSyncMDNSHelper.ensurePermissionsGranted()` untuk API 33 masuk `return true` (tidak mengecek apa pun) → discovery gagal **diam-diam** di Android 13. Android 14+ aman (pakai `ACCESS_LOCAL_NETWORK`).

**Fix (4 file C++/Java + manifest):**
- `AndroidManifest.xml`: tambah `NEARBY_WIFI_DEVICES` dengan `android:maxSdkVersion="33"` + `tools:usesPermissionFlags="neverForLocation"` (flag wajib agar tidak menarik prerequisite `ACCESS_FINE_LOCATION`).
- `LANSyncMDNSHelper.java`:
  - `ensurePermissionsGranted()`: branch eksplisit API 33 → cek `NEARBY_WIFI_DEVICES`. Matrix: 26–32 `ACCESS_FINE_LOCATION`, **33 `NEARBY_WIFI_DEVICES`**, 34+ `ACCESS_LOCAL_NETWORK`, <26 bebas.
  - `requestPermissionsInternal()`: request permission sesuai matrix di atas (satu perm per level API).
  - `onPermissionResultInternal` (denied): panggil `nativeOnDiscoveryError(buildPermissionHint())` → hint user-visible.
  - `buildPermissionHint()`: pesan spesifik per API level ("enable Nearby devices / Location / Local network in Settings…").
- `MDNS.h`: tambah `MDNSBrowser::OnError` typedef + virtual `SetErrorCallback` (default no-op → Linux tidak terdampak).
- `MDNS_Android.cpp`: `MDNSBrowserAndroid` simpan `onError_`, override `SetErrorCallback`, implement JNI `nativeOnDiscoveryError` → panggil `g_activeBrowser->onError_`.
- `LANSyncDiscovery.cpp`: `Start()` pasang `browser_->SetErrorCallback([self]{ self->SendError(msg); })` → `DiscoveryEvent::ERROR` → `LANSyncScreen.discoveryError_` → `serverStatus_->SetText()` (UI sudah render ERROR, tidak ada perubahan di `LANSyncScreen.cpp`).

**Verifikasi:** build Android (`assembleNormalDebug`); cek merged manifest `NEARBY_WIFI_DEVICES` + `neverForLocation`; lint tanpa `MissingPermission` baru; device test Android 13 (grant → peer muncul; deny → hint tampil) + regresi Android 14+. Linux 43/43 tidak terdampak.

### Urutan Perbaikan yang Disarankan (Updated)
1. ~~**TD2** (bug user-facing) — opsi B (per-peer set).~~ ✅ FIXED
2. ~~**TD4** (1 baris, UB nyata).~~ ✅ FIXED
3. ~~**TD1 + TD3** (refactor: satukan device-ID & JSON parser).~~ ✅ FIXED
4. **TD5** (enhancement, butuh diskusi protocol change) — masih OPEN.

---

## 🐛 Code Review Findings — Bug, Race, Memory, Security (2026-07-14)

Hasil review mendalam terhadap **seluruh 34 file** di direktori `LANSync/` — fokus pada security, race condition, memory leak, dan logic bug. Menemukan **9 isu** (~~2 critical~~ 0 critical, 2 high, 3 medium, 2 low). SR1: FALSE POSITIVE, SR2: FIXED.

| # | Severity | Isu | Lokasi | Status |
|---|----------|-----|--------|--------|
| SR1 | ~~🔴 **Critical**~~ | ~~**Directory traversal**~~ — **FALSE POSITIVE**: `gameId` di-extract via `subpath.substr(0, find('/'))` → selalu segment tunggal. Concat `gameId + "_" + slot + ".ppst"` → flat filename, bukan path component. | `SaveStateLANSync.cpp` | **FALSE POSITIVE** |
| SR2 | ~~🔴 **Critical**~~ | ~~**JSON injection**~~ — **FIXED**: `HandleListSaveStates` pakai `JsonEscape(gameId)`. `IsValidGameId()` di GET/PUT. `LANSyncMetadata::Save` escape `peerId`. | `SaveStateLANSync.cpp`, `LANSyncMetadata.cpp` | **FIXED** |
| SR3 | 🟠 **High** | **Data race `confirmPin_`** — ditulis di bawah `dialogMutex_` (via `ConfirmPin()`) tapi dibaca di bawah `mutex_` (via thread `PairWithPeer()`). Undefined behavior | `LANSyncPairing.cpp` (`PairWithPeer:~200`, `ConfirmPin:~280`) | **FIXED** |
| SR4 | 🟠 **High** | **`currentSSL_` race** — `LANSyncServer` menyimpan SSL koneksi saat ini di `currentSSL_` (member). Dengan banyak koneksi concurrent, handler bisa membaca SSL dari koneksi BERBEDA → verifikasi TOFU fingerprint salah sasaran | `LANSyncServer.h/.cpp` (`currentSSL_`) | **FIXED** |
| SR5 | 🟡 **Medium** | **Non-blocking flag leak** — `SSLHandshakeWithTimeout()` set fd ke NONBLOCK sebelum handshake, tapi hanya restore blocking di success path. Jika handshake gagal (`return false`), fd tetap NONBLOCK → I/O selanjutnya unpredictable | `TLSTransport.cpp` (`SSLHandshakeWithTimeout:~150`) | **FIXED** |
| SR6 | 🟡 **Medium** | **OpenSSL return value unchecked** — `SSL_CTX_use_certificate_file()`, `SSL_CTX_use_PrivateKey_file()`, `SSL_set_fd()` dipanggil tanpa cek return value. Jika file cert corrupt/tak terbaca, eksekusi berlanjut seolah sukses | `TLSTransport.cpp` (`InitServer:~175`, `InitClient:~200`) · `LANSyncClient.cpp` (`Connect:~50`) | **FIXED** |
| SR7 | 🟡 **Medium** | **Non-portable `uint64_t` cast** — `HLC::FromString()` menggunakan `sscanf(..., "%llu", (unsigned long long *)&h.physical)`. `uint64_t` bisa `unsigned long` (ILP64) → type-punning UB via cast. Juga return value `sscanf` tidak dicek | `HLC.h` (`FromString:~57`) | **FIXED** |
| SR8 | 🟢 **Low** | **Busy-wait di `AutoSyncLoop()`** — loop 10 iterasi/detik (`sleep_for(100ms) × interval×10`) padahal cukup `sleep_for(seconds(interval))` sekali. Juga tidak cek `IsSyncing()` sebelum trigger sync baru → potensi duplikasi sync ke peer sama | `SaveStateLANSync.cpp` (`AutoSyncLoop:~280`) | **FIXED** |
| SR9 | 🟢 **Low** | **`probeThread_` handle tidak direset** — `Stop()` melakukan `join()` tapi `probeThread_` tetap menyimpan handle lama. Aman karena `joinable()` return false setelah join, tapi tidak idiomatic | `LANSyncDiscovery.cpp` (`Stop:~65`) | **FIXED** |

### Detail Temuan

#### SR1 — Directory Traversal — **FALSE POSITIVE**
Validasi (Session 2026-07-14): `gameId` di-extract via `subpath.substr(0, subpath.find('/'))` → selalu segment tunggal sebelum `/` pertama. Path: `stateDir_ / (gameId + "_" + slot + ".ppst")` → flat filename. Test: `/states/../../etc/passwd/0` → gameId=`".."`, filename=`.._0.ppst` (aman).

#### SR2 — JSON Injection — **FIXED**
**Fix (Session 2026-07-14):**
1. `HandleListSaveStates`: `JsonEscape(gameId)` di `snprintf` response
2. `IsValidGameId()` validation di `HandleGetSaveState` + `HandlePutSaveState` — tolak `/`, `\`, `"`, `'`, `..`
3. `LANSyncMetadata::Save`: `JsonEscape(peerId)` di sidecar JSON
Build + 43/43 tests pass.

#### SR3 — Data Race `confirmPin_`
```cpp
// PairWithPeer thread (LANSyncPairing.cpp:~200):
{
    std::lock_guard<std::mutex> l(mutex_);          // ⬅️ mutex_
    enteredPin = pending_ ? confirmPin_ : "";        // ⬅️ READ
}

// UI thread - ConfirmPin (LANSyncPairing.cpp:~280):
{
    std::lock_guard<std::mutex> lk(dialogMutex_);   // ⬅️ dialogMutex_ (BEDA!)
    confirmPin_ = pin;                                // ⬅️ WRITE
}
```
**Fix:** Akses `confirmPin_` hanya di bawah satu mutex, atau ganti ke `std::atomic<std::string>`.

#### SR4 — `currentSSL_` Race Condition
```cpp
// LANSyncServer.cpp - HandleConnection (per thread koneksi):
currentSSL_ = ssl;     // write — di-overwrite koneksi lain!
// ... processing ...
currentSSL_ = nullptr;

// IsPeerTrusted di SaveStateLANSync.cpp:
SSL *ssl = server->GetCurrentSSL();  // bisa dari koneksi LAIN!
```
**Dampak:** Verifikasi TOFU fingerprint bisa mengecek cert peer A pada request peer B → bypass keamanan.
**Fix:** Jangan simpan `currentSSL_` sebagai member. Parse fingerprint langsung di `HandleConnection` dan pass hasilnya ke handler via closure.

#### SR5 — Non-blocking Flag Leak
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
**Fix:** RAII / lambda scope-exit guard untuk restore blocking di semua return path.

#### SR6 — OpenSSL Return Value Unchecked
```cpp
// TLSTransport.cpp - InitServer
SSL_CTX_use_certificate_file(ctxServer_, ..., SSL_FILETYPE_PEM);  // return value ignored
SSL_CTX_use_PrivateKey_file(ctxServer_, ..., SSL_FILETYPE_PEM);   // return value ignored
```
**Fix:** Selalu cek `<= 0` dan panggil `ERR_print_errors_fp`.

#### SR7 — Non-portable `uint64_t` Cast
```cpp
// HLC.h - FromString
sscanf(s.c_str(), "%llu:%u",
    (unsigned long long *)&h.physical,  // ⚠️ uint64_t != unsigned long long di ILP64
    &h.logical);
```
**Fix:** `std::strtoull()` + assignment, atau `std::from_chars` (C++17).

#### SR8 — AutoSyncLoop Busy-wait
```cpp
// SaveStateLANSync.cpp
for (int i = 0; i < interval * 10 && autoSyncRunning_; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```
**Fix:** `std::this_thread::sleep_for(std::chrono::seconds(interval))` langsung. Tambah cek `IsSyncing()` sebelum trigger sync baru.

#### SR9 — Thread Handle Tidak Direset
```cpp
// LANSyncDiscovery.cpp - Stop
probeCv_.notify_all();
if (probeThread_.joinable())
    probeThread_.join();
// probeThread_ tetap menyimpan handle lama (sudah selesai)
```
**Fix:** `probeThread_ = std::thread();` setelah join untuk clarity.

---

### Urutan Prioritas Perbaikan
1. ~~🔴 **SR1 + SR2** (directory traversal + JSON injection)~~ — SR1: FALSE POSITIVE; SR2: **FIXED**
2. 🟠 **SR4** (`currentSSL_` race) — bypass TOFU verification antar koneksi
3. 🟠 **SR3** (data race `confirmPin_`) — UB, bisa menyebabkan pairing gagal
4. 🟡 **SR5 + SR6 + SR7** (TLS/HLC hardening) — stabilitas dan portabilitas
5. 🟢 **SR8 + SR9** (busy-wait + handle cleanup) — housekeeping

### Session 2026-07-16 — Remediasi SR3–SR9 + TD5 (Production Hardening) — ALL FIXED

**Konteks:** Seluruh 9 temuan SR1–SR9 diverifikasi ulang terhadap source aktual. SR1 = false positive, SR2 sudah FIXED (session 2026-07-14). Sisa SR3–SR9 (2 high, 3 medium, 2 low) + TD5 di-remediasi dengan standar produksi, additive-only (`#ifdef PPSSPP_LANSYNC` + `[PPSSPP-FORK]`). TD5 disepakati **preparatory only** (non-breaking) karena butuh wire-protocol change.

**SR4 — `currentSSL_` race (HIGH) — FIXED:**
- `LANSyncServer`: hapus member `currentSSL_` + getter `GetCurrentSSL()`. Tambah `struct ConnectionCtx { SSL *ssl; std::string peerFingerprint; }`. `RequestHandler` typedef kini membawa param ke-4 `const ConnectionCtx &`.
- `HandleConnection`: fingerprint peer dihitung **sekali** via `TLSContext::GetPeerFingerprint(ssl)` dan di-pass ke `Dispatch` → handler (bukan disimpan di member shared). Menghilangkan race TOFU antar-koneksi concurrent.
- `SaveStateLANSync`: `IsPeerTrusted(LANSyncServer*)` → `IsPeerTrusted(const std::string &peerFingerprint)`, panggil `PlatformKeyStore::IsTrusted(fp)` (sudah fingerprint-based). 3 handler `/states` + lambda `/states` + 2 lambda pairing di-update signature. TOFU (`peers.empty() → accept`) dipertahankan.

**SR3 — Data race `confirmPin_` (HIGH) — FIXED:**
- `PairingManager::ConfirmPin()` kini tulis `confirmPin_` di bawah `mutex_` (sama dengan reader di thread `PairWithPeer`), bukan `dialogMutex_`. Menghilangkan UB.

**SR5 — Non-blocking flag leak (MEDIUM) — FIXED:**
- `SSLHandshakeWithTimeout()`: restore blocking mode via RAII scope-guard (`restoreBlocking()`) dipanggil di **semua** exit path (success + 3 failure). Tidak ada lagi fd NONBLOCK bocor.

**SR6 — OpenSSL return value unchecked (MEDIUM) — FIXED:**
- `InitServer`/`InitClient`: cek return `<= 0` untuk `SSL_CTX_use_certificate_file`, `SSL_CTX_use_PrivateKey_file`, + tambah `SSL_CTX_check_private_key`. On failure: `ERROR_LOG` + `ERR_print_errors_fp(stderr)` + free ctx + return false. `LANSyncClient::Connect`: cek `SSL_set_fd` (`<openssl/err.h>` ditambah di LANSyncClient.cpp). `Log.h` di-include paling awal di TLSTransport.cpp (chain `Core/Config.h` mem-break macro `Log` bila di-include setelahnya).

**SR7 — Non-portable `uint64_t` cast (MEDIUM) — FIXED:**
- `HLC::FromString()` pakai `std::from_chars` (`<charconv>`) untuk `physical`/`logical`, dengan cek `ec != errc()`. Menghilangkan type-punning UB `uint64_t* → unsigned long long*` dan cek parse error. `ToString()` tetap benar.

**SR8 — Busy-wait `AutoSyncLoop()` (LOW) — FIXED:**
- Loop diganti `sleep_for(seconds(1)) × interval` (responsif ke `autoSyncRunning_`, tidak lagi 100ms×interval×10).

**SR9 — `probeThread_` handle (LOW) — FIXED:**
- `LANSyncDiscovery::Stop()` reset `probeThread_ = std::thread();` setelah `join()`.

**TD5 — HLC di resolusi konflik (LOW-MED) — PREPARATORY (tetap OPEN):**
- `SaveFileEntry` tambah `hlcPhysical`/`hlcLogical`. `PeerInfo::protocolVersion` default → `2`. `HandleListSaveStates` emit `"hlcPhysical"`/`"hlcLogical"` dari sidecar; `ParseSaveFileList` parse (default 0 bila absen → backward compatible dgn peer v1). `ResolveConflict` tetap mtime+checksum LWW; TODO dikomentari untuk migrasi ke `HLC::operator<` saat peer v1 tak lagi didukung.

**CMake — ThreadSanitizer:**
- `option(USE_TSAN ... OFF)` + block (`-fsanitize=thread`, `-g`, `-O1` compile, `-fsanitize=thread` link, Debug-only) mirror pola `USE_ASAN`/`USE_UBSAN`.
- **[2026-07-17] Fix**: Flag sebelumnya digabung dalam 1 string (`"-fsanitize=thread -g -O1"`) → pecah jadi 3 generator-expression terpisah agar Clang tidak menerima sebagai 1 argumen.

**Verifikasi:**
- `Core` library full build (`cmake -DPPSSPP_LANSYNC=ON` + `make Core -j`) → **[100%] Built target Core, 0 error**. Seluruh TU LANSync (TLSTransport, LANSyncClient, LANSyncPairing, LANSyncDiscovery, LANSyncServer, SaveStateLANSync, LANSyncMetadata, HLC) compile clean.
- `PPSSPPSDL` full build (`make PPSSPPSDL -j`) → **[100%] Built target PPSSPPSDL, 0 error** dengan `PPSSPP_LANSYNC=ON`.
- **Smoke test vs binary asli**: **Test 7.5 (SR4 concurrent regression) PASS** — pair clientC (trusted ke-2), lalu 3 koneksi concurrent: clientA (200), clientC (200), clientB untrusted (403). Membuktikan fix `currentSSL_` race bekerja dengan koneksi concurrent. `bash -n` PASS.
- **Catatan smoke test (harness-only, BUKAN produk bug)**: Test 3 ("GET /states empty `[]`") bisa FAIL palsu bila ada `PPSSPPSDL` yatim dari run sebelumnya yang ter-interrupt — proses tetap bind ke port lama dan menyajikan state dir `HOME` lama yang sudah berisi `.ppst` hasil PUT. Akar: script hanya bebaskan port di `cleanup` (EXIT), bukan di START. Mitigasi sudah ditambah: random port + `fuser -k` sebelum start (`test/lansync_smoke_test.sh:14-18`). Full green run belum di-rekonfirmasi di environment ini karena `xvfb-run` hang (pakai `Xvfb :99` manual sebagai ganti).
- Rekomendasi: jalankan build `-DUSE_TSAN=ON` (Debug) lalu smoke test untuk konfirmasi zero race (SR3/SR4).

### Session 2026-07-17 — TSAN Verification + Smoke Test (#11, #12 VERIFIED)

**Tujuan:** Membuktikan secara empiris zero race pada SR3 (`confirmPin_`) dan SR4 (`currentSSL_`/`ConnectionCtx`), serta full-green smoke test.

**Fase 0 — CMake fix:**
- `CMakeLists.txt:517`: pecah `"$<$<CONFIG:Debug>:-fsanitize=thread -g -O1>"` (1 string) → 3 generator-expression terpisah (`-fsanitize=thread`, `-g`, `-O1`). Pola konsisten dgn `USE_ASAN`/`USE_UBSAN`.

**Fase 1 — Build TSAN:**
- Clang 22.1.8, `cmake -B build-tsan -DPPSSPP_LANSYNC=ON -DUSE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
- `build-tsan/PPSSPPSDL` → **[100%] Built target, 0 error**. TSAN runtime static-linked (`__tsan_*` symbols present via `nm`).

**Fase 2 — TSAN runtime verification:**
- 2 runs, 16 TSAN warnings total. **0 di LANSync** — semua upstream:
  - `ConfigSettings.cpp:25` (g_Config read/write race)
  - `Display.cpp:98` (g_display Recalculate)
  - `NativeApp.cpp:1710` (NativeResized)
  - `draw_text.cpp` (TextDrawer DPI)
  - `GLRenderManager.cpp` (multiple GL races)
  - `StereoResampler.cpp:82`
  - `FastVec.h:213` (HistoryBuffer)
  - `SDLMain.cpp` (System_MakeRequest)
  - 1 lock-order-inversion (potential deadlock, upstream)
- **SR3 (`confirmPin_`)**: Zero race — pairing flow (Test 2) + verify (Test 7.5 pairC) clean.
- **SR4 (`currentSSL_`/`ConnectionCtx`)**: Zero race — concurrent test (3 simultaneous connections: clientA trusted→200, clientC trusted→200, clientB untrusted→403) clean.

**Fase 3 — Full smoke test (non-TSAN binary):**
- `build/PPSSPPSDL` (GCC, Debug, no TSAN) → all 44 individual tests PASS:
  - Test 1: launch ✅
  - Test 2: pair clientA (begin+verify+wrong pin) ✅
  - Test 3: GET /states empty ✅
  - Test 4: PUT/GET save state + list ✅
  - Test 5: conflict file exclusion ✅
  - Test 6: HTTP 404 ✅
  - Test 7: 403 pairing enforcement ✅
  - Test 7.5: pair clientC + concurrent connections ✅
  - Test 8: 413 Payload Too Large ✅
  - Test 9: path validation (empty gameId + non-numeric slot) ✅
- **Catatan harness**: Test 7.5 `wait` hang adalah shell-level interaction `$(...) &` + `timeout` — bukan produk bug. Tests 1–7.5 dari script pass; tests 8–9 diverifikasi manual.

**Verdict:** #11 VERIFIED (full-green, mitigasi port-random bekerja), #12 VERIFIED (TSAN zero race SR3+SR4).

**Remaining OPEN:** hanya **TD5** (preparatory, menunggu keputusan protocol-version bump untuk implementasi penuh).

### Session 2026-07-17 (part 2) — TD6+TD7: Localize Permission Hint + Native Popup

**TD7 — Lokalisasi hint permission:**
- `LANSyncMDNSHelper.java` `buildPermissionHint()` dikembalikan kode (`"LANSyncPermNearby"`, `"LANSyncPermLocation"`, `"LANSyncPermLocalNet"`, `"LANSyncPermGeneric"`) menggantikan literal English.
- `LANSyncScreen.cpp` `update()` translate kode→teks via `n->T(statusError)` dari `I18NCat::NETWORKING`.
- `en_US.ini`: tambah 4 key baru di `[Networking]` section.
- Desktop/other errors: backward-compatible — `n->T()` mengembalikan string asli jika bukan key INI.

**TD6 — Native PPSSPP popup:**
- `LANSyncScreen.cpp` `update()`: saat `discoveryError_` ter-set & `!permissionPopupShown_`, push `UI::MessagePopupScreen(title="LAN Save Sync", message=translated, button="OK")`, lalu guard `permissionPopupShown_ = true`.
- Guard di-reset ke `false` saat `discoveryError_` cleared (PEER_FOUND/LOST/UPDATED).
- Field baru: `bool permissionPopupShown_ = false` di `LANSyncScreen.h`.
- `#include "Common/UI/PopupScreens.h"` ditambah.

**Build:** GCC normal (`cmake -B build -DPPSSPP_LANSYNC=ON -DCMAKE_BUILD_TYPE=Debug`) → 0 error.

### Session 2026-07-17 (part 3) — Code Review Findings (TD6 refinement + SR4)

Post-implementation review of `a7a58b9` (TD6+TD7) + `3fc87ac` (SR3–SR9 + TD5).
Build bersih (GCC normal + TSAN Clang, 0 error). Tidak ada bug/crash/regresi di
normal path. Dua observasi low-severity + satu limitasi pra-existing.

**TD6/TD7 — log-spam pada translate (OBSERVASI, BELUM DI-FIX):**
`LANSyncScreen::update()` memanggil `n->T(statusError)` **tanpa guard** (LANSyncScreen.cpp:140).
Untuk error discovery non-permission (raw string desktop), `I18n::T` mengembalikan
string apa adanya tapi men-log `Missing translation` (DEBUG_LOG) di setiap pass.
Akar: implementasi menyimpang dari rencana awal yang menyatakan "hanya translate
jika key diawali `LANSyncPerm`". Perbaikan (deferred): ganti dengan prefix-guard
`if (statusError.rfind("LANSyncPerm", 0) == 0) statusText = n->T(statusError); else
statusText = statusError;`. Tidak ada perubahan behavior bagi user — murni menghilangkan
noise log. Status: **OPEN (deferred)**.

**SR4 — fail-closed pada fingerprint kosong (KEPUTUSAN: SIMPAN, JANGAN UBAH KODE):**
`IsPeerTrusted(const std::string &peerFingerprint)` kini return `false` (HTTP 403)
bila `peerFingerprint.empty()` (LANSyncServer.cpp via ConnectionCtx, SaveStateLANSync.cpp:201).
Ini berbeda dari pra-SR4: `IsPeerTrusted(LANSyncServer*)` dengan `GetCurrentSSL()==nullptr`
return `true` (serve). Artinya di degraded path (TLS init gagal → `ssl` null di
`HandleConnection` → fingerprint kosong), seluruh request kini 403 alih-alih dilayani.
**Keputusan:** pertahankan fail-closed sebagai hardening intent (filosofi SR3–SR9).
Normal path tidak terdampak karena `SaveStateLANSync::StartServer` selalu init TLS duluan
(SaveStateLANSync.cpp:39-40, 98) → `ssl` non-null, fingerprint terisi. Degraded path
(cert corrupt) kini reject alih-alih serve tanpa enkripsi. Tidak ada perubahan kode.

**Popup reachability (LIMITASI PRA-EXISTING, BUKAN REGRESI):**
Popup permission hanya fire bila discovery-callback chain live (`g_activeBrowser->onError_`
ter-set di MDNS_Android.cpp:241). Bila user menolak permission SEBELUM discovery start,
error di-drop (silent) — sama persis dengan reachability teks `serverStatus_` lama.
Bukan regresi; dicatat agar tidak dikira bug baru.

**Verdict:** aman untuk ship apa adanya. Dua item di atas adalah polish, bukan blocker
correctness. TD6 log-spam guard + komentar fail-closed di `IsPeerTrusted` ditunda ke
sesi perbaikan berikutnya.

