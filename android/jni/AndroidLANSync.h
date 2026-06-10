// PPSSPP Project - LAN Save State Sync
// Android platform backend - NsdManager, Keystore, ForegroundService
// Full implementation in Phase 3.
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

#include "Core/SaveStateLANSync.h"

class AndroidLANSync {
public:
	static AndroidLANSync &Instance();

	bool Init();  // JNI init
	void Shutdown();
	bool Enable(const std::string &deviceName);
	void Disable();

	SaveStateLANSync &Core() { return SaveStateLANSync::Instance(); }

	// QR code scanning (ML Kit / ZXing)
	void StartQRScan(std::function<void(const std::string &result)> callback);
	void StopQRScan();

private:
	AndroidLANSync() = default;
	bool enabled_ = false;
	std::string deviceName_;
};
