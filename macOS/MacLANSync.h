// PPSSPP Project - LAN Save State Sync
// macOS backend - wraps LinuxLANSync (shared Unix code) with macOS-specific init.
// Uses Bonjour dns_sd.h (via MDNS_Unix.cpp) and Keychain (via PlatformKeyStore_Unix.cpp).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <vector>

#include "SDL/LinuxLANSync.h"

// MacLANSync wraps LinuxLANSync for macOS.
// Same API, different platform key storage (Keychain vs libsecret)
// and mDNS (Bonjour vs Avahi) - both handled by MDNS_Unix.cpp.

using MacLANSync = LinuxLANSync;

inline MacLANSync &GetMacLANSync() {
	return GetLinuxLANSync();
}
