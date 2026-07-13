#include "LANSync/LANSyncServer.h"
#include "Common/Net/SocketCompat.h"
#include "Common/StringUtils.h"
#include <openssl/ssl.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

namespace LANSync {

LANSyncServer::LANSyncServer() {}

LANSyncServer::~LANSyncServer() {
    Stop();
}

bool LANSyncServer::Start(int port, TLSContext *tlsCtx) {
    tlsCtx_ = tlsCtx;
    port_ = port;

    listenerFd_ = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (listenerFd_ < 0) return false;

    int opt = 1;
    setsockopt(listenerFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenerFd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        closesocket(listenerFd_);
        listenerFd_ = -1;
        return false;
    }

    if (listen(listenerFd_, 10) < 0) {
        closesocket(listenerFd_);
        listenerFd_ = -1;
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread(&LANSyncServer::AcceptLoop, this);
    return true;
}

void LANSyncServer::Stop() {
    running_ = false;
    if (listenerFd_ >= 0) {
        closesocket(listenerFd_);
        listenerFd_ = -1;
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void LANSyncServer::RegisterHandler(const std::string &pathPrefix, RequestHandler handler) {
    handlers_[pathPrefix] = std::move(handler);
}

void LANSyncServer::ClearHandlers() {
    handlers_.clear();
}

void LANSyncServer::AcceptLoop() {
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = (int)accept(listenerFd_, (struct sockaddr *)&clientAddr, &addrLen);
        if (clientFd < 0) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        HandleConnection(clientFd, nullptr);
    }
}

void LANSyncServer::HandleConnection(int fd, SSL *ssl) {
    bool ownedSSL = false;
    if (!ssl && tlsCtx_ && tlsCtx_->GetServerContext()) {
        ssl = SSL_new(tlsCtx_->GetServerContext());
        if (ssl) {
            SSL_set_fd(ssl, fd);
            if (!SSLHandshakeWithTimeout(ssl, fd, 10, true)) {
                SSL_free(ssl);
                closesocket(fd);
                return;
            }
            ownedSSL = true;
        }
    }

    currentSSL_ = ssl;

    std::string rawRequest;
    char buf[4096];
    bool headersDone = false;
    int contentLength = 0;

    while (running_) {
        int n = ssl ? SSL_read(ssl, buf, sizeof(buf) - 1) : (int)recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;

        buf[n] = '\0';
        rawRequest.append(buf, n);

        if (!headersDone) {
            size_t headerEnd = rawRequest.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headersDone = true;
                std::string headers = rawRequest.substr(0, headerEnd);

                size_t clPos = headers.find("Content-Length:");
                if (clPos != std::string::npos) {
                    clPos += 15;
                    while (clPos < headers.size() && headers[clPos] == ' ') clPos++;
                    contentLength = atoi(headers.c_str() + clPos);
                }

                size_t bodyStart = headerEnd + 4;
                size_t receivedBody = rawRequest.size() - bodyStart;
                if (contentLength > 0 && receivedBody >= (size_t)contentLength) {
                    break;
                }
                if (contentLength == 0) {
                    break;
                }
            }
        } else {
            size_t headerEnd = rawRequest.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                size_t bodyStart = headerEnd + 4;
                size_t receivedBody = rawRequest.size() - bodyStart;
                if ((size_t)contentLength <= receivedBody) {
                    break;
                }
            }
        }
    }

    if (!rawRequest.empty()) {
        std::string method, path, body;
        if (ParseHTTP(rawRequest, method, path, body)) {
            std::string response = Dispatch(method, path, body);

            // Map JSON error responses to HTTP status codes
            int statusCode = 200;
            if (response.find("\"error\"") != std::string::npos) {
                if (response.find("forbidden") != std::string::npos) statusCode = 403;
                else if (response.find("not_found") != std::string::npos) statusCode = 404;
                else if (response.find("method_not_allowed") != std::string::npos) statusCode = 405;
                else statusCode = 400;
            }
            SendResponse(fd, ssl, statusCode, "application/json", response);
        } else {
            SendResponse(fd, ssl, 400, "text/plain", "Bad Request");
        }
    }

    if (ownedSSL && ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    currentSSL_ = nullptr;
    closesocket(fd);
}

bool LANSyncServer::ParseHTTP(const std::string &raw, std::string &method, std::string &path, std::string &body) {
    size_t firstSpace = raw.find(' ');
    if (firstSpace == std::string::npos) return false;

    method = raw.substr(0, firstSpace);

    size_t secondSpace = raw.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) return false;

    path = raw.substr(firstSpace + 1, secondSpace - firstSpace - 1);

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        body = raw.substr(headerEnd + 4);
    }

    return true;
}

std::string LANSyncServer::Dispatch(const std::string &method, const std::string &path, const std::string &body) {
    for (const auto &[prefix, handler] : handlers_) {
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return handler(method, path, body);
        }
    }
    return "{\"error\":\"not_found\"}";
}

void LANSyncServer::SendResponse(int fd, SSL *ssl, int statusCode, const std::string &contentType, const std::string &body, bool isBinary) {
    std::string statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 405: statusText = "Method Not Allowed"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "Unknown"; break;
    }

    std::string header;
    if (isBinary) {
        header = StringFromFormat(
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            statusCode, statusText.c_str(),
            contentType.c_str(),
            body.size());
    } else {
        header = StringFromFormat(
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n",
            statusCode, statusText.c_str(),
            contentType.c_str(),
            body.size());
    }

    if (ssl) {
        SSL_write(ssl, header.data(), header.size());
        if (!body.empty())
            SSL_write(ssl, body.data(), body.size());
    } else {
        send(fd, header.data(), header.size(), 0);
        if (!body.empty())
            send(fd, body.data(), body.size(), 0);
    }
}

}  // namespace LANSync
