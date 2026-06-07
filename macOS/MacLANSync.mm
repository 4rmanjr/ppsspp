// PPSSPP Project - LAN Save State Sync
// macOS backend implementation - thin wrapper.
// Real implementation in Phase 9 (Cocoa UI + Keychain native).
//
// Phase 1-2: Uses LinuxLANSync shared code via PPSSPP_PLATFORM(MAC) path.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC)

#include "macOS/MacLANSync.h"
#include "Common/Log.h"

// Note: Full macOS native implementation in Phase 9.
// For now, GetMacLANSync() returns GetLinuxLANSync() which handles
// PPSSPP_PLATFORM(MAC) via MDNS_Unix.cpp (Bonjour) and PlatformKeyStore_Unix.cpp (Keychain fallback).

#endif  // PPSSPP_PLATFORM(MAC)
