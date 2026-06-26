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
| `Core/` | Config, SaveState LANSync (new files, not modifications) |
| `SDL/` | LAN sync implementation for SDL/Linux |
| `Windows/` | LAN sync implementation for Windows |
| `macOS/` | LAN sync implementation for macOS |
| `UI/` | LAN Peer List Screen, Settings (new UI files) |

## Additional Rules

1. **Don't touch `Core/SaveState.cpp` for new logic.** All sync logic lives in `SaveStateLANSync.cpp`. Upstream files only get minimal hooks.
2. **Platform-specific code** separated per platform (`SDL/`, `Windows/`, `macOS/`), not mixed in `Common/`.
3. **Every new feature** must have its own `#ifdef` flag (see `fork-maintenance.md`).
4. **Test** — every change must build with `./b.sh --debug` without errors.
5. **End-to-end test** available in `test_e2e_lansync.cpp` and `test_e2e_full.cpp` — run before submitting changes.
