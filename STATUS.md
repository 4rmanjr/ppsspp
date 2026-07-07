# LAN Save State Sync (PPSSPP-LANSYNC) — Status

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
- `NativeApp.cpp`: `g_LANSync` global, init/shutdown, `--lansync-enabled` / `--lansync-port` CLI flags

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

---

## ⚠️ Limitations & Known Issues

1. **Runtime-untested on Android**: Build passes for arm64-v8a and x86_64 but never ran on device; `LANSyncMDNSHelper.java` untested with real NsdManager
2. **Mixed build dirs**: Smoke test defaults to `build/PPSSPPSDL`, build may be in `build-fresh/` — script needs update or consistency
3. **CLI flag only enable**: `--lansync-enabled` auto-starts server + discovery but no CLI disable
4. **TLS handshake blocking**: `SSL_connect()` is still blocking (though `SO_RCVTIMEO`/`SO_SNDTIMEO` are respected for socket I/O, unlike `connect()`)
5. **OpenSSL required for Android**: `ext/openssl/build_android.sh` must be run (once) before Android build; requires NDK + internet

## 📐 Rules & Constraints

- **Zero upstream deletion**: Semua kode LANSync digate dengan `#ifdef PPSSPP_LANSYNC` + comment `// [PPSSPP-FORK]`
- **Additive-only**: Jangan hapus atau ubah existing logic — tambah di sampingnya
- **Konvensi commit**: `type(lansync): deskripsi` + footer `[PPSSPP-FORK]`
- **PRIVATE define sudah diganti PUBLIC**: Jangan balikin ke PRIVATE
- **File scope**: Semua file baru di `LANSync/`, modify existing file minimal (hanya tambah `#ifdef` block)
