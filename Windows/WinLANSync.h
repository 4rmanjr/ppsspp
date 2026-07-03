// [PPSSPP-FORK] LANSync: Windows platform backend
// Only add new lines. Do not delete/modify upstream lines.

#ifdef PPSSPP_LANSYNC

#pragma once

#include <string>
#include <vector>

#include "LANSync/SaveStateLANSync.h"

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

#endif // PPSSPP_LANSYNC
