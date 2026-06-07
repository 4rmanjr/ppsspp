// PPSSPP Project - LAN Save State Sync
// Linux platform backend - wires together mDNS, UDP, TLS, and PlatformKeyStore
// for the Linux platform (SDL + Qt frontends share this backend).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>

#include "Core/SaveStateLANSync.h"

// LinuxLANSync wraps the core SaveStateLANSync with Linux-specific init.
// It handles:
//   - PlatformKeyStore init (libsecret or file-based)
//   - mDNS discovery (Avahi)
//   - UDP broadcast fallback
//   - TLS server (Phase 5: real TLS, Phase 1-2: plain TCP)
//
// Both SDL and Qt frontends use this same backend.

class LinuxLANSync {
public:
	// Initialize platform subsystems
	bool Init();

	// Shutdown
	void Shutdown();

	// Access the core sync manager
	SaveStateLANSync &Core() { return SaveStateLANSync::Instance(); }

	// Convenience: enable sync (starts server + discovery)
	bool Enable(const std::string &deviceName);

	// Convenience: disable sync
	void Disable();

	// QR Code generation (for server pairing screen)
	// Returns BMP image data using libqrencode.
	// payload: ppsspp-sync://pair?host=192.168.1.50&port=27345&fp=SHA256:...&pin=739281&name=MyPC
	std::vector<uint8_t> GenerateQRCode(const std::string &payload);

	// Get local IP addresses (for display in pairing screen)
	std::vector<std::string> GetLocalIPs() const;

private:
	bool enabled_ = false;
};

// Singleton accessor
LinuxLANSync &GetLinuxLANSync();
