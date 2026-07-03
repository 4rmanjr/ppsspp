// [PPSSPP-FORK] LANSync: macOS backend implementation
// Only add new lines. Do not delete/modify upstream lines.

#ifdef PPSSPP_LANSYNC

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC)

#include "macOS/MacLANSync.h"
#include "Common/Log.h"

// Note: Full macOS native implementation in Phase 9.
// For now, GetMacLANSync() returns GetLinuxLANSync() which handles
// PPSSPP_PLATFORM(MAC) via MDNS_Unix.cpp (Bonjour) and PlatformKeyStore_Unix.cpp (Keychain fallback).

#endif // PPSSPP_LANSYNC
#endif  // PPSSPP_PLATFORM(MAC)
