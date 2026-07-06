#pragma once
#include <string>
#include <cstdint>
#include "Common/File/Path.h"
#include "LANSync/HLC.h"

class LANSyncMetadata {
public:
    static bool Load(const Path &ppstPath, HLC &hlc, uint64_t &originalMtime, std::string &peerId);
    static bool Save(const Path &ppstPath, const HLC &hlc, uint64_t originalMtime, const std::string &peerId);
    static Path SidecarPath(const Path &ppstPath);
    static void Delete(const Path &ppstPath);
    static std::string ComputeChecksum(const Path &path);
};
