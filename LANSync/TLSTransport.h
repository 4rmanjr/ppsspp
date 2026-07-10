#pragma once
#include <string>
#include "Common/File/Path.h"

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct x509_st X509;

namespace LANSync {

class TLSConnection;

class TLSContext {
public:
    TLSContext();
    ~TLSContext();

    bool InitServer();
    bool InitClient();
    bool HasCert() const { return certInitialized_; }

    SSL_CTX *GetServerContext() const { return ctxServer_; }
    SSL_CTX *GetClientContext() const { return ctxClient_; }
    std::string GetCertFingerprint() const { return fingerprint_; }
    std::string GetCertPEM() const { return certPEM_; }

    // Static helpers for peer cert verification
    static std::string GetPeerFingerprint(SSL *ssl);
    static std::string GetPeerCertPEM(SSL *ssl);
    static std::string GetX509Fingerprint(X509 *cert);
    static std::string GetFingerprintFromPEM(const std::string &pem);

private:
    bool GenerateSelfSignedCert();
    bool LoadOrCreateCert();

    SSL_CTX *ctxServer_ = nullptr;
    SSL_CTX *ctxClient_ = nullptr;
    bool certInitialized_ = false;
    std::string fingerprint_;
    std::string certPEM_;
    Path certDir_;
};

class TLSConnection {
public:
    TLSConnection(SSL *ssl, int fd);
    ~TLSConnection();

    SSL *GetSSL() const { return ssl_; }
    int Read(void *buf, int num);
    int Write(const void *buf, int num);
    bool Handshake();
    void Close();

private:
    SSL *ssl_ = nullptr;
    int fd_ = -1;
};

// Non-blocking SSL/TLS handshake with select() timeout.
// fd must already be connected. Restores blocking mode on success.
// Returns true if handshake completes within timeoutSec (> 0).
bool SSLHandshakeWithTimeout(SSL *ssl, int fd, int timeoutSec, bool asServer);

// Utility: escape a raw string for embedding in a JSON string value.
// Escapes: backslash, double-quote, newline, carriage return, tab.
inline std::string JsonEscape(const std::string &raw) {
    std::string out;
    out.reserve(raw.size() + 16);
    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

}  // namespace LANSync
