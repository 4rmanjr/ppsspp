# LAN Sync Development — Specific Rules

## Feature Scope

Current custom features:
- **LAN Sync Core** — save state synchronization between devices over local network
- **MDNS Discovery** — automatic peer discovery on local network
- **TLS Server** — encrypted communication between peers
- **UDP Discovery** — alternative discovery protocol
- **QR Code Pairing** — connection via QR scan
- **Platform Key Store** — secure credential storage per platform

## Modular Architecture

All LAN sync features are distributed across non-core directories:

| Directory | Contents |
|-----------|----------|
| `Common/Net/` | MDNS, UDP Discovery, TLS, Key Store (cross-platform) |
| `LANSync/` | Core sync engine (`SaveStateLANSync.*`), config (`LANSyncConfig.*`), metadata (`SaveStateSyncMetadata.*`) |
| `SDL/` | LAN sync implementation for SDL/Linux + ImGui UI |
| `Windows/` | LAN sync implementation for Windows |
| `macOS/` | LAN sync implementation for macOS + Cocoa UI |
| `UI/` | LAN Peer List Screen, Settings screens |
| `android/jni/` | Android platform backend (JNI bridge) |

## Additional Rules

1. **Hooks di upstream file** di-wrap dengan `#ifdef PPSSPP_LANSYNC`. Hooks minimal (≤3 lines) di `Core/SaveState.cpp`, `Core/Config.h`, `Core/Config.cpp`, `UI/NativeApp.cpp`, `UI/EmuScreen.cpp`, `UI/GameSettingsScreen.cpp`. Seluruh logika sync ada di `LANSync/SaveStateLANSync.cpp`.
2. **Platform-specific code** separated per platform (`SDL/`, `Windows/`, `macOS/`), not mixed in `Common/`.
3. **Every new feature** must have its own `#ifdef` flag (see `fork-maintenance.md`). LAN Sync menggunakan `PPSSPP_LANSYNC` (didefinisikan di `CMakeLists.txt` dengan `option(PPSSPP_LANSYNC ON)`).
4. **Test** — every change must build with `./b.sh --debug` without errors.
5. **End-to-end test** available in `test_e2e_lansync.cpp` and `test_e2e_full.cpp` — run before submitting changes.
