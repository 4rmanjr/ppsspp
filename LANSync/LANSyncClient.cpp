#include "LANSync/LANSyncClient.h"
#include "Common/File/FileDescriptor.h"
#include "Common/Net/SocketCompat.h"
#include "Common/StringUtils.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <atomic>
#include <openssl/ssl.h>

namespace LANSync {

LANSyncClient::LANSyncClient(TLSContext *tlsCtx)
    : tlsCtx_(tlsCtx) {}

LANSyncClient::~LANSyncClient() {
    Disconnect();
}

bool LANSyncClient::Connect(const std::string &host, int port, int timeoutSec) {
    host_ = host;
    port_ = port;

    fd_ = fd_util::ConnectWithTimeout(host.c_str(), port, timeoutSec);
    if (fd_ < 0) return false;

    // Set I/O timeouts for subsequent send/recv
    struct timeval tv;
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Wrap in TLS
    if (tlsCtx_ && tlsCtx_->GetSSLContext()) {
        SSL *ssl = SSL_new(tlsCtx_->GetSSLContext());
        if (!ssl) {
            closesocket(fd_);
            fd_ = -1;
            return false;
        }
        SSL_set_fd(ssl, fd_);

        if (!SSLHandshakeWithTimeout(ssl, fd_, timeoutSec, false)) {
            SSL_free(ssl);
            closesocket(fd_);
            fd_ = -1;
            return false;
        }

        conn_ = new TLSConnection(ssl, fd_);
    }

    connected_ = true;
    return true;
}

void LANSyncClient::Disconnect() {
    connected_ = false;
    if (conn_) {
        delete conn_;
        conn_ = nullptr;
    } else if (fd_ >= 0) {
        closesocket(fd_);
    }
    fd_ = -1;
}

HTTPResponse LANSyncClient::Get(const std::string &path) {
    return SendRequest("GET", path, "", "");
}

HTTPResponse LANSyncClient::Post(const std::string &path, const std::string &contentType, const std::string &body) {
    return SendRequest("POST", path, contentType, body);
}

HTTPResponse LANSyncClient::SendRequest(const std::string &method, const std::string &path,
                                         const std::string &contentType, const std::string &body) {
    HTTPResponse response;
    if (!connected_) return response;

    // Build HTTP request
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + host_ + "\r\n";
    if (!body.empty()) {
        request += "Content-Type: " + contentType + "\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n";
    request += "\r\n";
    request += body;

    // Send
    int totalSent = 0;
    while (totalSent < (int)request.size()) {
        int n;
        if (conn_) {
            n = conn_->Write(request.data() + totalSent, (int)request.size() - totalSent);
        } else {
            n = (int)send(fd_, request.data() + totalSent, request.size() - totalSent, 0);
        }
        if (n <= 0) {
            Disconnect();
            return response;
        }
        totalSent += n;
    }

    // Read response
    if (!ReadResponse(response)) {
        Disconnect();
    }

    return response;
}

bool LANSyncClient::ReadResponse(HTTPResponse &response) {
    std::string raw;
    char buf[4096];
    bool headersDone = false;
    int contentLength = 0;
    size_t bodyStart = 0;

    while (true) {
        int n;
        if (conn_) {
            n = conn_->Read(buf, sizeof(buf) - 1);
        } else {
            n = (int)recv(fd_, buf, sizeof(buf) - 1, 0);
        }

        if (n <= 0) break;
        buf[n] = '\0';
        raw.append(buf, n);

        if (!headersDone) {
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headersDone = true;
                bodyStart = headerEnd + 4;

                // Parse status code
                if (raw.compare(0, 9, "HTTP/1.1 ") == 0) {
                    response.statusCode = atoi(raw.c_str() + 9);
                } else if (raw.compare(0, 9, "HTTP/1.0 ") == 0) {
                    response.statusCode = atoi(raw.c_str() + 9);
                }

                // Parse Content-Length
                size_t clPos = raw.find("Content-Length:");
                if (clPos == std::string::npos)
                    clPos = raw.find("content-length:");
                if (clPos != std::string::npos) {
                    clPos = raw.find(':', clPos) + 1;
                    while (clPos < raw.size() && raw[clPos] == ' ') clPos++;
                    contentLength = atoi(raw.c_str() + clPos);
                }

                // Extract headers
                std::string headerSection = raw.substr(0, headerEnd);
                size_t lineStart = 0;
                while (lineStart < headerSection.size()) {
                    size_t lineEnd = headerSection.find("\r\n", lineStart);
                    if (lineEnd == std::string::npos) break;
                    std::string line = headerSection.substr(lineStart, lineEnd - lineStart);
                    if (line.find("HTTP/") != 0 && !line.empty()) {
                        response.headers.push_back(line);
                    }
                    lineStart = lineEnd + 2;
                }
            }
        }

        if (headersDone) {
            size_t receivedBody = raw.size() - bodyStart;
            if (contentLength > 0 && receivedBody >= (size_t)contentLength) {
                response.body = raw.substr(bodyStart, contentLength);
                return true;
            }
            if (contentLength == 0) {
                response.body = raw.substr(bodyStart);
                return true;
            }
        }
    }

    // If no Content-Length, return what we got
    if (headersDone && !raw.empty()) {
        response.body = raw.substr(bodyStart);
        return response.statusCode > 0;
    }

    return false;
}

bool LANSyncClient::DownloadFile(const std::string &urlPath, const Path &outputPath) {
    HTTPResponse resp = Get(urlPath);
    if (resp.statusCode != 200 || resp.body.empty())
        return false;

    // Write to temp file and rename atomically
    Path tmpPath = Path(outputPath.ToString() + ".tmp");
    FILE *f = fopen(tmpPath.ToString().c_str(), "wb");
    if (!f) return false;

    size_t written = fwrite(resp.body.data(), 1, resp.body.size(), f);
    fclose(f);

    if (written != resp.body.size()) {
        remove(tmpPath.ToString().c_str());
        return false;
    }

    if (rename(tmpPath.ToString().c_str(), outputPath.ToString().c_str()) != 0) {
        remove(tmpPath.ToString().c_str());
        return false;
    }

    return true;
}

bool LANSyncClient::UploadFile(const std::string &urlPath, const Path &filePath, const std::string &contentType) {
    FILE *f = fopen(filePath.ToString().c_str(), "rb");
    if (!f) return false;

    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string data;
    data.resize(fileSize);
    size_t n = fread(&data[0], 1, fileSize, f);
    fclose(f);

    if ((long)n != fileSize) return false;

    HTTPResponse resp = SendRequest("PUT", urlPath, contentType, data);
    return resp.statusCode == 200;
}

}  // namespace LANSync
