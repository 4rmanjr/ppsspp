// [PPSSPP-FORK] LANSync: Configuration block for LAN sync settings.
// Storage: Integrated into Config struct as g_Config.lanSync.
// Load/save via Config::Load()/Config::Save() in Config.cpp.
// Only add new lines. Do not delete/modify upstream lines.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <string_view>

#include "Core/ConfigValues.h"

// Follows PPSSPP config naming convention: bBool, iInt, sString, fFloat
struct LANSyncConfig : public ConfigBlock {
	bool bEnabled = false;
	std::string sDeviceName;
	bool bAutoDiscover = true;
	int iMaxPeers = 5;
	int iConflictResolution = 0;    // 0=NEWEST_WINS, 1=KEEP_LOCAL, 2=KEEP_REMOTE, 3=PROMPT
	std::string sPairedPeers;      // JSON array of peer info
	int iHttpPort = 0;             // 0 = OS assigns ephemeral port
	bool bUseTLS = true;
	bool bAutoSync = false;

	bool CanResetToDefault() const override { return true; }
	bool ResetToDefault(std::string_view blockName) override;
	size_t Size() const override { return sizeof(LANSyncConfig); }
};
