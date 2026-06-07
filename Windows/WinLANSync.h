// PPSSPP Project - LAN Save State Sync
// Windows platform backend
// Phase 4: WinRT DNS-SD (Win10+) + UDP fallback, DPAPI, Firewall (INetFwRule)
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

#include "Core/SaveStateLANSync.h"

class WinLANSync {
public:
	static WinLANSync &Instance();

	bool Init();
	void Shutdown();
	bool Enable(const std::string &deviceName);
	void Disable();

	SaveStateLANSync &Core() { return SaveStateLANSync::Instance(); }

	// QR code generation (libqrencode)
	std::vector<uint8_t> GenerateQRCode(const std::string &payload);

	// Local IPs
	std::vector<std::string> GetLocalIPs() const;

private:
	WinLANSync() = default;
	bool AddFirewallRule(int port);
	void RemoveFirewallRule();
};
