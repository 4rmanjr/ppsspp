// [PPSSPP-FORK] LANSync: LANSyncConfig implementation
// Only add new lines. Do not delete/modify upstream lines.

#include "LANSync/LANSyncConfig.h"
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
