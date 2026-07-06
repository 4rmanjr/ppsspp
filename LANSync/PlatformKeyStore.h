#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Common/File/Path.h"

namespace LANSync {

struct TrustedPeer {
    std::string peerId;
    std::string deviceName;
    std::string certPEM;
    std::string lastIP;
    uint64_t pairedAt = 0;
};

class PlatformKeyStore {
public:
    static bool SavePeer(const TrustedPeer &peer);
    static std::vector<TrustedPeer> LoadPeers();
    static bool IsTrusted(const std::string &fingerprint);
    static const TrustedPeer *FindPeer(const std::string &peerId);
    static bool RemovePeer(const std::string &peerId);
    static Path StorageDir();
};

}  // namespace LANSync
