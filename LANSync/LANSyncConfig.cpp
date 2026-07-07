#include "LANSync/LANSyncConfig.h"
#include "Core/Config.h"
#include "Core/System.h"

namespace LANSync {

void LANSyncConfigInfo::Load() {
#ifdef PPSSPP_LANSYNC
    bEnabled = g_Config.bLANSyncEnabled;
    bAutoSync = g_Config.bLANSyncAutoSync;
    sDeviceName = g_Config.sLANSyncDeviceName;
    iPort = g_Config.iLANSyncPort;
    iSyncRetryCount = g_Config.iLANSyncRetryCount;
    iSyncRetryDelayMs = g_Config.iLANSyncRetryDelayMs;
#endif
}

void LANSyncConfigInfo::Save() {
#ifdef PPSSPP_LANSYNC
    g_Config.bLANSyncEnabled = bEnabled;
    g_Config.bLANSyncAutoSync = bAutoSync;
    g_Config.sLANSyncDeviceName = sDeviceName;
    g_Config.iLANSyncPort = iPort;
    g_Config.iLANSyncRetryCount = iSyncRetryCount;
    g_Config.iLANSyncRetryDelayMs = iSyncRetryDelayMs;
    g_Config.Save("LANSyncConfig");
#endif
}

std::string LANSyncConfigInfo::GetDeviceName() const {
    if (!sDeviceName.empty())
        return sDeviceName;

    std::string mac = g_Config.sMACAddress;
    if (mac.size() >= 4) {
        return "PPSSPP-" + mac.substr(mac.size() - 4);
    }
    return "PPSSPP-Unknown";
}

}  // namespace LANSync
