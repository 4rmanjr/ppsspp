# LAN Save State Sync (PPSSPP-LANSYNC) — Status

## ✅ Done (Phase 1 — 19 commits)

### Transport & Crypto
- TLS 1.3 with self-signed ECDSA P-256 cert, auto-generated on first run
- `PlatformKeyStore`: file-based (Linux), stub (Android)
- `TLSContext` / `TLSTransport`: init client + server contexts

### HTTP Server & Client
- `LANSyncServer`: TLS HTTP/1.1 server, port 27314, routes: `/states` (GET list, GET file, PUT file), `/pair/begin`, `/pair/verify`
- `LANSyncClient`: TLS HTTP client for peer communication

### Discovery
- `MDNS` abstraction: `MDNSAnnouncer` + `MDNSBrowser`
- `MDNS_Linux.cpp`: Avahi client (functional)
- `MDNS_Android.cpp`: stub (not implemented)
- `LANSyncDiscovery`: mDNS announcer/browser + manual peer + periodic probe thread

### Pairing
- `PairingManager`: 6-digit PIN via truncated SHA256, TOFU trust model
- HTTP endpoints: `POST /pair/begin` (returns nonce), `POST /pair/verify` (accepts nonce+pin+peerId)

### Config
- `LANSyncConfig`: device name, port, enabled, auto-sync, paired peers, retry settings
- `Core/Config.h` + `Config.cpp`: `#ifdef PPSSPP_LANSYNC` fields + INI section `[LANSync]`

### UI Integration
- `LANSyncScreen`: peer list, sync button, progress display
- `MainScreen`: "LAN Sync" button in right column
- `GameSettingsScreen`: "LAN Sync" option in Networking tab
- `PauseScreen`: "Sync Saves Now" option
- `NativeApp.cpp`: `g_LANSync` global, init/shutdown, `--lansync-enabled` / `--lansync-port` CLI flags

### Build System
- `CMakeLists.txt`: `option(PPSSPP_LANSYNC)`, `find_package(OpenSSL)`, Avahi linkage
- LANSync sources part of `Core` library

### ODR Crash Fixed (Session 2026-07-06)
- `target_compile_definitions PRIVATE → PUBLIC`: PPSSPPSDL now sees same `Config` struct layout as Core
- `discovery_ unique_ptr → shared_ptr`: `LANSyncDiscovery` uses `enable_shared_from_this`
- `Initialize()` auto-calls `StartServer()` + `StartDiscovery()` when `--lansync-enabled`
- Missing `#include "LANSyncScreen.h"` in `MainScreen.cpp` and `GameSettingsScreen.cpp`

---

## 🚧 In Progress — Phase 2

### Task 1: Integration Smoke Test (partial)
- Script: `test/lansync_smoke_test.sh`
- Tests 1 (launch) & 2 (HTTP /states): **PASS**
- Test 3 (PUT/GET save state): **FAIL** — fake data format mismatch, need proper save state file
- Test 4 (LWW conflict rename): **DROPPED** (was mocking, not testing real sync)
- Test 5 (pairing): **NOT REACHED**

## ⬜ Not Started — Phase 2 Tasks

### Task 2: Manual Peer Input UI
- Add `TextEdit` fields (IP, port) + "Add Peer" button to `LANSyncScreen`
- Wire to `LANSyncDiscovery::AddManualPeer()`
- Files: `LANSync/LANSyncScreen.h`, `LANSync/LANSyncScreen.cpp`

### Task 3: Discovery Event Callback Wiring
- Add `DiscoveryCallback` to `SaveStateLANSync` — forward events to `LANSyncScreen`
- UI reacts to `PEER_FOUND`/`PEER_LOST` instead of polling
- Files: `SaveStateLANSync.h/.cpp`, `LANSyncScreen.h/.cpp`

### Task 4: PIN Dialog Screen
- New screen: `LANSyncPairingDialog` — initiator shows PIN, receiver has text entry
- Replace auto-confirm in `PairingManager` with dialog callback
- New files: `LANSync/LANSyncPairingDialog.h/.cpp`
- Modified: `LANSync/LANSyncPairing.h/.cpp`

### Task 5: Conflict Viewer Screen
- New screen: `LANSyncConflictScreen` — list `.ppst.conflict` files
- "Keep Local" / "Keep Remote" actions per conflict
- "View Conflicts" button in `LANSyncScreen`
- New files: `LANSync/LANSyncConflictScreen.h/.cpp`

### Task 6: Sync Error Handling
- Socket read/write timeouts via `setsockopt(SO_RCVTIMEO, SO_SNDTIMEO)`
- Configurable retry count + delay (default 3 attempts, 2s apart)
- Retry loop in `DoSyncWithPeer()`
- `CancelSync()` cleanup orphan `.tmp` files
- Files: `LANSyncClient`, `LANSyncConfig`, `SaveStateLANSync`, `Core/Config`

### Task 7: Android mDNS (NsdManager)
- Full `MDNS_Android.cpp` implementation via JNI
- Java helper: `android/jni/LANSyncMDNSHelper.java`
- Update `MDNS.cpp` factory for Android
- **HIGH EFFORT** — requires understanding existing android/jni patterns

### Task 8: Background Auto-Sync
- Periodic polling thread in `SaveStateLANSync`
- Syncs all discovered peers when `bAutoSync` enabled
- Configurable interval (default 60s)
- Start/stop in `Initialize()` / `Shutdown()`

---

## ⚠️ Limitasi & Known Issues

1. **Android mDNS (Task 7)**: `MDNS_Android.cpp` masih stub — discovery cuma jalan via manual IP di Android
2. **PIN dialog**: PairingManager masih auto-confirm — belum ada UI dialog untuk entry PIN
3. **Conflict viewer**: `.ppst.conflict` rename sudah terjadi otomatis, tapi user gak bisa liat atau resolve
4. **Error handling**: Gak ada timeout di socket, gak ada retry — koneksi failure langsung drop
5. **Auto-sync**: Belum ada background polling thread
6. **Smoke test (Task 1)**: Test 3 (PUT/GET) gagal karena format save state file — perlu disamakan sama yang diharapkan server
7. **Mixed build dirs:** Smoke test default ke `build/PPSSPPSDL`, build di `build-fresh/` — perlu update script atau konsisten
8. **CLI flag `--lansync-enabled`**: Auto-start server + discovery tapi belum ada cara disable dari CLI

## 📐 Rules & Constraints

- **Zero upstream deletion**: Semua kode LANSync digate dengan `#ifdef PPSSPP_LANSYNC` + comment `// [PPSSPP-FORK]`
- **Additive-only**: Jangan hapus atau ubah existing logic — tambah di sampingnya
- **Konvensi commit**: `type(lansync): deskripsi` + footer `[PPSSPP-FORK]`
- **Dependency graph**: Tasks dalam Phase 2 punya urutan (lihat `.opencode/plans/2025-07-06-lansync-phase2.md`)
- **PRIVATE define sudah diganti PUBLIC**: Jangan balikin ke PRIVATE
- **File scope**: Semua file baru di `LANSync/`, modify existing file minimal (hanya tambah `#ifdef` block)
