#pragma once
#include <functional>
#include <map>
#include <thread>
#include <atomic>
#include <string>
#include "Common/File/Path.h"
#include "LANSync/TLSTransport.h"

namespace LANSync {

class LANSyncServer {
public:
    // [PPSSPP-FORK] Connection-scoped context handed to request handlers.
    // The peer certificate fingerprint is computed once inside HandleConnection
    // (on that connection's own thread) and passed here, instead of being
    // stored in a shared member. This prevents concurrent connections from
    // clobbering each other's TOFU verification (see SR4).
    struct ConnectionCtx {
        SSL *ssl = nullptr;
        std::string peerFingerprint;
    };

    using RequestHandler = std::function<std::string(const std::string &method, const std::string &path, const std::string &body, const ConnectionCtx &ctx)>;

    LANSyncServer();
    ~LANSyncServer();

    bool Start(int port, TLSContext *tlsCtx);
    void Stop();
    bool IsRunning() const { return running_; }
    int Port() const { return port_; }

    void RegisterHandler(const std::string &pathPrefix, RequestHandler handler);
    void ClearHandlers();

private:
    void AcceptLoop();
    void HandleConnection(int fd, SSL *ssl);
    bool ParseHTTP(const std::string &raw, std::string &method, std::string &path, std::string &body);
    std::string Dispatch(const std::string &method, const std::string &path, const std::string &body, const ConnectionCtx &ctx);
    void SendResponse(int fd, SSL *ssl, int statusCode, const std::string &contentType, const std::string &body, bool isBinary = false);

    int listenerFd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    TLSContext *tlsCtx_ = nullptr;
    std::map<std::string, RequestHandler, std::less<>> handlers_;
};

}  // namespace LANSync
