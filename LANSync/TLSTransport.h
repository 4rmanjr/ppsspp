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

    SSL_CTX *GetSSLContext() const { return ctx_; }
    std::string GetCertFingerprint() const { return fingerprint_; }
    std::string GetCertPEM() const { return certPEM_; }

private:
    bool GenerateSelfSignedCert();
    bool LoadOrCreateCert();
    std::string ComputeFingerprint(X509 *cert);

    SSL_CTX *ctx_ = nullptr;
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

}  // namespace LANSync
