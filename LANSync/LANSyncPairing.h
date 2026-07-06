#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace LANSync {

class LANSyncServer;
class LANSyncClient;
class TLSContext;

class PairingManager {
public:
    using PairingCompleteCallback = std::function<void(bool success, const std::string &peerId)>;

    PairingManager(TLSContext *tlsCtx);
    ~PairingManager();

    void RegisterHandlers(LANSyncServer *server);

    bool PairWithPeer(const std::string &host, int port, PairingCompleteCallback callback);

    void CancelPairing();
    void ConfirmPin(const std::string &pin);
    bool IsPairingInProgress() const;

private:
    std::string HandlePairBegin(const std::string &method, const std::string &path, const std::string &body);
    std::string HandlePairVerify(const std::string &method, const std::string &path, const std::string &body);

    static std::string ComputePin(const std::string &nonce);
    static std::string GenerateNonce();
    static std::string GetLocalPeerId();

    struct PendingPairing {
        std::string host;
        int port = 0;
        std::string nonce;
        std::string peerFingerprint;
        std::string expectedPin;
        PairingCompleteCallback callback;
    };

    TLSContext *tlsCtx_ = nullptr;
    std::unique_ptr<PendingPairing> pending_;
    mutable std::mutex mutex_;

    struct PendingNonce {
        std::string nonce;
        uint64_t createdAt;
        std::string peerFingerprint;
    };
    std::vector<PendingNonce> pendingNonces_;
    mutable std::mutex nonceMutex_;
};

} // namespace LANSync
