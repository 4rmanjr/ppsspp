# LAN Sync Save State — File Map

**Branch:** `feature/lan-sync`
**Scope:** PSP-only LAN save state synchronization

---

## Core Orchestration

| File | Description |
|------|-------------|
| `Core/SaveStateLANSync.h` | Main sync manager singleton — discovery, server, pairing, sync, conflict resolution |
| `Core/SaveStateLANSync.cpp` | Full implementation (~2200 lines): mDNS+UDP discovery, HTTP/TLS server, pairing protocol, sync engine, API handlers |
| `Core/SaveStateSyncMetadata.h` | Sidecar `.ppst.sync.json` — version, SHA-256, HLC timestamp, peer info |
| `Core/SaveStateSyncMetadata.cpp` | JSON serialization, file I/O for sidecar metadata |

## Configuration

| File | Description |
|------|-------------|
| `Core/LANSyncConfig.h` | `LANSyncConfig` struct — enabled, deviceName, autoDiscover, conflictResolution, pairedPeers, httpPort, useTLS, autoSync |
| `Core/LANSyncConfig.cpp` | ResetToDefault implementation |
| `Core/Config.h` | `#include "Core/LANSyncConfig.h"` + `LANSyncConfig lanSync` member |
| `Core/Config.cpp` | `lansyncSettings[]` array (9 settings) registered as `"LANSync"` section |

## HLC (Hybrid Logical Clock)

| File | Description |
|------|-------------|
| `Common/Data/HLC.h` | `HLC` struct — wallTime, logical counter, deviceId. Increment/Merge/IsAfter/comparison. `DetectConflict()` |
| `Common/Data/HLC.cpp` | GetNowMicros, ToString/FromString, ToJSON/FromJSON |

## Networking Infrastructure

| File | Description |
|------|-------------|
| `Common/Net/MDNS.h` / `.cpp` | mDNS service browser/announcer — `_ppsspp-sync._tcp.local.` |
| `Common/Net/MDNS_Unix.cpp` | Linux/macOS backend (Avahi / dns_sd.h) |
| `Common/Net/MDNS_Windows.cpp` | Windows backend (WinRT DNS-SD) |
| `Common/Net/MDNS_Android.cpp` | Android backend (NsdManager) |
| `Common/Net/UDPDiscovery.h` / `.cpp` | UDP broadcast fallback — JSON on `255.255.255.255:27313` |
| `Common/Net/TLSServer.h` / `.cpp` | TLS 1.3 server — self-signed ECDSA, TOFU model |
| `Common/Net/PlatformKeyStore.h` | Abstract secure key storage interface |
| `Common/Net/PlatformKeyStore_Unix.cpp` | Linux (libsecret / file fallback) |
| `Common/Net/PlatformKeyStore_Windows.cpp` | Windows (DPAPI) |
| `Common/Net/PlatformKeyStore_Android.cpp` | Android Keystore |

## Save State Hooks (upstream files, additive)

| File | Description |
|------|-------------|
| `Core/SaveState.cpp` | `OnSaveStateLoaded`/`OnSaveStateSaved` hooks when `g_Config.lanSync.bEnabled` |

## UI Screens

| File | Description |
|------|-------------|
| `UI/LANPeerListScreen.h` / `.cpp` | Pairing popup — peer discovery, manual IP, PIN entry |
| `UI/LANSyncSettings.h` / `.cpp` | Qt UI — settings, pairing, progress dialogs |
| `UI/GameSettingsScreen.cpp` | "LAN Save State Sync" section in Settings > Networking |
| `UI/EmuScreen.cpp` | SDLLANSync include + `renderImDebugger()` renders LAN sync ImGui panels |
| `UI/NativeApp.cpp` | LAN sync init/shutdown (Android + Linux/macOS), auto-enable from config |

## Platform Backends

| File | Description |
|------|-------------|
| `SDL/LinuxLANSync.h` / `.cpp` | Linux/macOS — Init, Enable/Disable, QR code, GetLocalIPs |
| `SDL/SDLLANSync.h` / `.cpp` | SDL ImGui UI — settings, pairing, progress, conflict dialogs |
| `Windows/WinLANSync.h` / `.cpp` | Windows — WinRT DNS-SD, QR, firewall rules, DPAPI keystore |
| `macOS/MacLANSync.h` / `.mm` | macOS — alias to LinuxLANSync |
| `macOS/CocoaLANSync.h` / `.mm` | macOS Cocoa NSPanel/NSWindow UI |
| `android/jni/AndroidLANSync.h` / `.cpp` | Android JNI bridge — NSD, Keystore, ForegroundService |

## Android Java Layer

| File | Description |
|------|-------------|
| `android/src/org/ppsspp/ppsspp/LANSyncManager.java` | NSD discovery coordinator |
| `android/src/org/ppsspp/ppsspp/LANSyncService.java` | Foreground service for background sync |
| `android/src/org/ppsspp/ppsspp/LANSyncKeystore.java` | Android Keystore TLS cert/key storage |
| `android/src/org/ppsspp/ppsspp/LANSyncQRScanActivity.java` | QR code scanning camera activity |
| `android/src/org/ppsspp/ppsspp/test/LANSyncTestActivity.java` | Manual test activity |

## Tests

| File | Description |
|------|-------------|
| `test_lansync.cpp` | HLC edge cases + sync logic unit tests |
| `test_e2e_lansync.cpp` | E2E: bidirectional sync, conflict detection, pairing, network interruption |
| `test_lansync_android.sh` | Android device test runner |

## Build Integration

| File | Description |
|------|-------------|
| `CMakeLists.txt` | HLC, MDNS, UDPDiscovery, TLSServer, PlatformKeyStore, LANSyncConfig, SaveStateLANSync, LANSyncSettings, platform backends. OpenSSL + Avahi + QREncode linking |
| `android/build.gradle.kts` | `security-crypto`, `mlkit:barcode-scanning` dependencies |
| `android/jni/Android.mk` | AndroidLANSync source files |

## Documentation

| File | Description |
|------|-------------|
| `docs/agents/lansync-development.md` | Agent rules — scope, architecture, file mapping |
| `LAN_SYNC_IMPROVEMENTS.md` | 6-phase improvement plan |
| `ai_instructions/lan_sync_save_state_plan.md` | Original design doc — architecture, protocol, UI specs |

---

## Upstream Hooks (additive, zero deletion)

| Upstream File | What Was Added |
|---------------|----------------|
| `Core/Config.h` | `#include "Core/LANSyncConfig.h"` + `LANSyncConfig lanSync` member |
| `Core/Config.cpp` | `lansyncSettings[]` array + registration in `g_sectionMeta` |
| `Core/SaveState.cpp` | 3 hooks: `OnSaveStateLoaded`/`OnSaveStateSaved` + `ProcessPendingCallback` |
| `UI/GameSettingsScreen.cpp` | "LAN Save State Sync" UI section (~70 lines) |
| `UI/NativeApp.cpp` | LAN sync includes, init (Android + Linux/macOS), shutdown, auto-enable |
| `UI/EmuScreen.cpp` | SDLLANSync include + `renderImDebugger()` LAN sync panel rendering |
| `SDL/SDLJoystick.h` | (none — PushSDLAudio removed during GBA cleanup) |
| `SDL/SDLMain.cpp` | SDLLANSync include |
| `android/build.gradle.kts` | security-crypto + mlkit dependencies |
| `android/jni/Locals.mk` | (none — PPSSPP_MULTICORE removed during GBA cleanup) |

---

## Compliance (vs AGENTS.md)

| Rule | Status | Note |
|------|--------|------|
| Custom files in non-core dirs | ⚠️ | `Core/SaveStateLANSync.*` + `Core/LANSyncConfig.*` in Core/ |
| `[PPSSPP-FORK]` markers | ⚠️ | Not yet added to LAN sync files |
| Feature flag `PPSSPP_LANSYNC` | ❌ | Not yet implemented — always compiled |
| Upstream hooks additive | ✅ | All changes are additive, zero deletions |
| Config isolation `[LANSync]` | ✅ | Separate INI section |
