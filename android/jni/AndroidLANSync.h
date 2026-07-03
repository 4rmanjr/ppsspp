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

	// Sync progress notification (called from sync worker thread)
	void UpdateSyncProgress(const SaveStateLANSync::SyncProgress &progress);
	void CompleteSync(int uploaded, int downloaded);

	// QR code scanning (ML Kit / ZXing)
	void StartQRScan(std::function<void(const std::string &result)> callback);
	void StopQRScan();

	// [PPSSPP-FORK] LANSync: In-app dialog support for Android
	void ShowConflictDialog(const std::string &slotName, int64_t localTime, int64_t remoteTime,
	                        int64_t localSize, int64_t remoteSize,
	                        std::function<void(int)> callback);
	void ShowServerPairingScreen(std::function<void()> onClose);
	void ShowLargeSaveWarning(const std::string &slotName, int64_t sizeBytes,
	                         std::function<void(bool)> callback);

private:
	AndroidLANSync() = default;
	bool enabled_ = false;
	std::string deviceName_;
};
