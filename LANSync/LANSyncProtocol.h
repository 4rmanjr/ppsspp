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
    int protocolVersion = 2;
};

struct SaveFileEntry {
    std::string gameId;
    int slot = 0;
    std::string checksum;
    uint64_t mtime = 0;
    int64_t size = 0;
    // [PPSSPP-FORK] TD5 (preparatory, non-breaking): hybrid logical
    // clock carried on the wire so conflict resolution can eventually use a
    // causal ordering instead of pure mtime + checksum. Not yet used by
    // ResolveConflict (still mtime+LWW) to stay compatible with peers on
    // protocol version 1. Populated from the .sync.json sidecar when present.
    uint64_t hlcPhysical = 0;
    uint32_t hlcLogical = 0;
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
