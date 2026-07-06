#pragma once
#include <string>
#include <vector>
#include "Common/File/Path.h"
#include "LANSync/TLSTransport.h"

namespace LANSync {

struct HTTPResponse {
    int statusCode = 0;
    std::string body;
    std::vector<std::string> headers;
};

class LANSyncClient {
public:
    explicit LANSyncClient(TLSContext *tlsCtx);
    ~LANSyncClient();

    bool Connect(const std::string &host, int port, int timeoutSec = 10);
    void Disconnect();
    bool IsConnected() const { return connected_; }

    HTTPResponse Get(const std::string &path);
    HTTPResponse Post(const std::string &path, const std::string &contentType, const std::string &body);

    // Download a file from the peer to a local path
    bool DownloadFile(const std::string &urlPath, const Path &outputPath);

    // Upload a local file
    bool UploadFile(const std::string &urlPath, const Path &filePath, const std::string &contentType = "application/octet-stream");

private:
    HTTPResponse SendRequest(const std::string &method, const std::string &path,
                             const std::string &contentType, const std::string &body);
    bool ReadResponse(HTTPResponse &response);

    TLSContext *tlsCtx_ = nullptr;
    TLSConnection *conn_ = nullptr;
    std::string host_;
    int port_ = 0;
    int fd_ = -1;
    bool connected_ = false;
};

}  // namespace LANSync
