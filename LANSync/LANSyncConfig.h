#pragma once
#include <string>
#include "Common/File/Path.h"

namespace LANSync {

struct LANSyncConfigInfo {
    bool bEnabled = false;
    bool bAutoSync = false;
    std::string sDeviceName;
    int iPort = 27314;

    void Load();
    void Save();
    std::string GetDeviceName() const;
};

}  // namespace LANSync
