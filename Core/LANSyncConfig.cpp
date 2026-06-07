// PPSSPP Project - LAN Save State Sync
// LANSyncConfig implementation

#include "Core/LANSyncConfig.h"
#include "Common/Log.h"

LANSyncConfig g_LANSyncConfig;

bool LANSyncConfig::ResetToDefault(std::string_view blockName) {
	if (blockName == "LANSync" || blockName.empty()) {
		bEnabled = false;
		sDeviceName.clear();
		bAutoDiscover = true;
		iMaxPeers = 5;
		iConflictResolution = 0;
		sPairedPeers.clear();
		iHttpPort = 0;
		bUseTLS = true;
		bAutoSync = false;
		return true;
	}
	return false;
}
