#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace LANSync {

constexpr const char *kServiceType = "_ppsspp-sync._tcp";
constexpr int kDefaultPort = 27314;

struct PeerInfo {
    std::string deviceName;
    std::string version;
    std::string peerId;
    int protocolVersion = 1;
};

struct SaveFileEntry {
    std::string gameId;
    int slot = 0;
    std::string checksum;
    uint64_t mtime = 0;
    int64_t size = 0;
};

struct SyncResponse {
    std::vector<SaveFileEntry> files;
};

struct PairBeginResponse {
    std::string nonce;
    std::string certFingerprint;
};

struct PairVerifyRequest {
    std::string nonce;
    std::string pin;
    std::string peerId;
};

struct PairVerifyResponse {
    bool success = false;
    std::string peerId;
};

struct SyncProgress {
    int totalFiles = 0;
    int completedFiles = 0;
    std::string currentFile;
    enum Status { IDLE, DISCOVERING, PAIRING, SYNCING, COMPLETED, ERROR };
    Status status = IDLE;
    std::string errorMessage;
};

struct DiscoveredPeer {
    std::string host;
    int port = kDefaultPort;
    std::string deviceName;
    std::string peerId;
};

}  // namespace LANSync
