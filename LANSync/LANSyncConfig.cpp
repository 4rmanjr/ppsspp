#include "LANSync/LANSyncConfig.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Common/Log.h"
#include <cstring>
#include <unistd.h>

namespace LANSync {

void LANSyncConfigInfo::Load() {
#ifdef PPSSPP_LANSYNC
    bEnabled = g_Config.bLANSyncEnabled;
    bAutoSync = g_Config.bLANSyncAutoSync;
    iAutoSyncInterval = g_Config.iLANSyncAutoSyncInterval;
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
    g_Config.iLANSyncAutoSyncInterval = iAutoSyncInterval;
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
    if (mac.size() < 4) {
        WARN_LOG(Log::System, "LANSync: sMACAddress too short (%zu), Config post-load may not have run", mac.size());
    }
    if (mac.size() >= 4) {
        return "PPSSPP-" + mac.substr(mac.size() - 4);
    }
    // Last resort — should never reach here on a properly loaded config
    char hostname[256] = "Unknown";
    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    std::string suffix = std::to_string((uintptr_t)this);
    return std::string("PPSSPP-") + hostname + "-" + suffix.substr(suffix.size() - 4);
}

}  // namespace LANSync
