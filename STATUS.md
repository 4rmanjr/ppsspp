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
6. **[2026-07-09] [KRITIS] TLS antar-device gagal (sync tidak jalan)**: `LANSyncClient` set `SSL_VERIFY_PEER` (`TLSTransport.cpp:209`) tapi tidak pernah memuat sertifikat peer ke trust store. Tiap device punya self-signed cert sendiri → handshake ke device lain ditolak (`Connect()` gagal) → sync batal. Smoke test 10/10 lulus hanya lewat tool eksternal (`openssl s_client`/`curl`), bukan kode `LANSyncClient`. Model TOFU pairing tidak di-wire ke layer TLS. Status: BELUM DIFIX.
7. **[2026-07-09] [TINGGI] Pairing tidak di-enforce di endpoint data**: `/states` GET/PUT (`SaveStateLANSync.cpp:80-91`) didaftarkan tanpa cek daftar paired peer; `AutoSyncLoop`/`SyncWithAllPeers` sync ke semua peer terdeteksi. Begitu #6 diperbaiki, peer LAN mana pun bisa baca/timpa `.ppst` tanpa pairing. Pairing saat ini kosmetik. Status: BELUM DIFIX.
8. **[2026-07-09] [SEDANG] Resolusi konflik murni mtime (LWW) + celah tie**: `ResolveConflict` (`SaveStateLANSync.cpp:508-539`) hanya bertindak bila satu mtime > lainnya. Jika mtime sama tapi isi beda, tidak ada branch jalan → konflik dibuang diam-diam, salinan divergen, tak ada `.conflict`. Tak ada tiebreak checksum. Status: BELUM DIFIX.
9. **[2026-07-09] [RENDAH] Tidak ada batas ukuran PUT**: `HandlePutSaveState` tulis seluruh body ke disk tanpa cap → potensi disk-fill DoS. Status: BELUM DIFIX.
10. **[2026-07-09] [RENDAH] Parse gameId/slot asumsi 1 underscore — FIXED**: Analisis: `rfind('_')` sudah benar untuk format `<gameId>_<slot>.ppst`. Ditambah: helper `ParseSaveFilename()` (strtol + errno check, gameId kosong/tidak valid di-skip, validasi slot 0-999), 3 call site refactored ke satu fungsi. Commit: (soon).

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
| CR3 | MEDIUM | `SavePeer()` fixed 2048-byte buffer overflow — PEM cert (~1-2KB) + JSON wrapper can exceed limit. Save silently fails. | Replaced `char buf[2048]` + `snprintf` with `std::string` concatenation (both Linux + Android). |
| CR3 | MEDIUM | `SavePeer()` fixed 2048-byte buffer overflow — PEM cert (~1-2KB) + JSON wrapper can exceed limit. Save silently fails. | Replaced `char buf[2048]` + `snprintf` with `std::string` concatenation + `JsonEscape` for valid JSON output (both Linux + Android). |
| CR4 | LOW/CRITICAL | PEM embedded in JSON without string escaping — technically invalid JSON. Initial fix applied `JsonEscape` at both transport and file layers, but transport + PendingNonce + SavePeer chain caused double-escape → `GetFingerprintFromPEM` receives `\n` as backslash+n, not newline → parse fails. | `JsonEscape` removed from transport layer (`PairWithPeer` request, `HandlePairBegin` response). **Only `SavePeer` escapes** for file storage. HTTP uses raw PEM (safe: custom parser reads until next `"`, PEM has no `"`). `extractStr` handles both escaped and legacy unescaped files. |

### Remaining Open Issues (Priority Order)

| # | Priority | Issue | Status |
|---|----------|-------|--------|
| 6 | ~~KRITIS~~ | ~~TLS antar-device gagal (sync tidak jalan)~~ | **FIXED** |
| 7 | TINGGI | Pairing tidak di-enforce di endpoint data | **PARTIALLY FIXED** (TOFU verification in `DoSyncWithPeer()` rejects unpaired peers when trusted list is non-empty; endpoint-level enforcement still pending) |
| 8 | ~~SEDANG~~ | ~~LWW conflict tie gap (mtime sama, isi beda)~~ | **FIXED** |
| 9 | ~~RENDAH~~ | ~~Tidak ada batas ukuran PUT~~ | **FIXED** |
| 10 | ~~RENDAH~~ | ~~Parse gameId/slot asumsi 1 underscore~~ | **FIXED** |
| 13 | SEDANG | Android LOCAL guard (mitigated by deviceName filter) | MITIGATED |

### Design Decision 2026-07-09 — Transport LAN Sync = IPv4-Only

**Keputusan**: Seluruh LAN sync berjalan di **IPv4 saja**, konsisten dengan server (`LANSyncServer.cpp` → `socket(AF_INET)`, `INADDR_ANY`). IPv6 sengaja tidak dipakai untuk menghindari pitfall scope-id / `fe80::` (alamat link-local IPv6 butuh zone id, sering gagal disambung meski server dual-stack).

**Implementasi (DONE)**:
- `MDNS_Linux.cpp`: `AVAHI_PROTO_UNSPEC` → `AVAHI_PROTO_INET` pada announcer (`MDNSAnnouncerLinux::Start`) dan browser (`MDNSBrowserLinux::Start`) → mDNS hanya IPv4.
- `LANSyncDiscovery.cpp`: `OnPeerFound` & `OnPeerLost` membuang peer dengan host IPv6 (deteksi via `host.find(':') != npos`) + filter loopback/APIPA tetap. Mencover Android (NsdManager tak bisa batasi protokol) dan defense-in-depth.
- `fd_util::ConnectWithTimeout` **tidak diubah** (util shared PPSSPP lain); client otomatis konek IPv4 karena discovery hanya mengembalikan IPv4.

**Dampak**: Ketidakcocokan IPv4(server)/IPv6(discovery) **RESOLVED by design**. Risiko residual: jaringan murni IPv6-only tak terdeteksi (sesuai pilihan IPv4-only).
