// [PPSSPP-FORK] Log.h must be included before LANSync/TLSTransport.h /
// Core/Config.h: the Config chain transitively breaks the Log macros/enum
// visibility if Log.h is pulled in after them.
#include "Common/Log.h"
#include "LANSync/TLSTransport.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/File/FileDescriptor.h"
#include "Common/Net/SocketCompat.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace LANSync {

static bool ssl_global_init = false;

static void EnsureSSLInit() {
    if (!ssl_global_init) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ssl_global_init = true;
    }
}

TLSContext::TLSContext() {
    EnsureSSLInit();
    certDir_ = GetSysDirectory(DIRECTORY_SAVESTATE);
}

TLSContext::~TLSContext() {
    if (ctxServer_) SSL_CTX_free(ctxServer_);
    if (ctxClient_) SSL_CTX_free(ctxClient_);
}

bool TLSContext::LoadOrCreateCert() {
    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";

    if (File::Exists(certPath) && File::Exists(keyPath)) {
        std::string certData, keyData;
        if (File::ReadBinaryFileToString(certPath, &certData) && File::ReadBinaryFileToString(keyPath, &keyData)) {
            BIO *certBio = BIO_new_mem_buf(certData.data(), (int)certData.size());
            BIO *keyBio = BIO_new_mem_buf(keyData.data(), (int)keyData.size());
            X509 *cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
            EVP_PKEY *key = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
            BIO_free(certBio);
            BIO_free(keyBio);

            if (cert && key) {
                EVP_PKEY_free(key);
                fingerprint_ = GetX509Fingerprint(cert);
                X509_free(cert);
                certInitialized_ = true;
                return true;
            }
            if (cert) X509_free(cert);
            if (key) EVP_PKEY_free(key);
        }
    }

    return GenerateSelfSignedCert();
}

bool TLSContext::GenerateSelfSignedCert() {
    EVP_PKEY *pkey = EVP_EC_gen("P-256");
    if (!pkey) return false;

    X509 *cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 3650 * 24 * 3600);

    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"PPSSPP LAN Sync", -1, -1, 0);
    X509_set_issuer_name(cert, name);

    X509_set_pubkey(cert, pkey);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    if (!X509_sign(cert, pkey, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";

    BIO *certOut = BIO_new(BIO_s_mem());
    BIO *keyOut = BIO_new(BIO_s_mem());
    if (certOut && keyOut) {
        PEM_write_bio_X509(certOut, cert);
        PEM_write_bio_PrivateKey(keyOut, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        char *certData, *keyData;
        long certLen = BIO_get_mem_data(certOut, &certData);
        long keyLen = BIO_get_mem_data(keyOut, &keyData);
        if (certData && certLen > 0) File::WriteStringToFile(false, std::string(certData, certLen), certPath);
        if (keyData && keyLen > 0) File::WriteStringToFile(false, std::string(keyData, keyLen), keyPath);
    }
    BIO_free(certOut);
    BIO_free(keyOut);

    fingerprint_ = GetX509Fingerprint(cert);

    BIO *bio = BIO_new(BIO_s_mem());
    if (bio) {
        PEM_write_bio_X509(bio, cert);
        char *data;
        long len = BIO_get_mem_data(bio, &data);
        if (data && len > 0) {
            certPEM_ = std::string(data, len);
        }
        BIO_free(bio);
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);
    certInitialized_ = true;
    return true;
}

std::string TLSContext::GetX509Fingerprint(X509 *cert) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    if (X509_digest(cert, EVP_sha256(), hash, &hashLen)) {
        char hex[EVP_MAX_MD_SIZE * 2 + 1];
        for (unsigned int i = 0; i < hashLen; i++) {
            snprintf(hex + i * 2, 3, "%02x", hash[i]);
        }
        return std::string(hex);
    }
    return "";
}

/*static*/ std::string TLSContext::GetPeerFingerprint(SSL *ssl) {
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) return "";
    std::string fp = GetX509Fingerprint(cert);
    X509_free(cert);
    return fp;
}

/*static*/ std::string TLSContext::GetPeerCertPEM(SSL *ssl) {
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) return "";
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        X509_free(cert);
        return "";
    }
    if (!PEM_write_bio_X509(bio, cert)) {
        BIO_free(bio);
        X509_free(cert);
        return "";
    }
    char *data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem;
    if (len > 0 && data) {
        pem.assign(data, len);
    }
    BIO_free(bio);
    X509_free(cert);
    return pem;
}

/*static*/ std::string TLSContext::GetFingerprintFromPEM(const std::string &pem) {
    BIO *bio = BIO_new_mem_buf(pem.data(), pem.size());
    if (!bio) return "";
    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) return "";
    std::string fp = GetX509Fingerprint(cert);
    X509_free(cert);
    return fp;
}

/*static*/ std::string TLSContext::GetDeviceId() const {
    if (!fingerprint_.empty() && fingerprint_.size() >= 8) {
        return "PPSSPP-" + fingerprint_.substr(0, 8);
    }
    std::string mac = g_Config.sMACAddress;
    if (mac.size() >= 4) {
        return "PPSSPP-" + mac.substr(mac.size() - 4);
    }
    return "PPSSPP-Unknown";
}

bool SSLHandshakeWithTimeout(SSL *ssl, int fd, int timeoutSec, bool asServer) {
    if (!ssl || fd < 0 || timeoutSec <= 0)
        return false;

    int flags = fcntl(fd, F_GETFL, 0);
    bool wasBlocking = (flags != -1) && !(flags & O_NONBLOCK);
    if (wasBlocking)
        fd_util::SetNonBlocking(fd, true);

    // [PPSSPP-FORK] SR5: restore blocking mode on EVERY exit path. Previously
    // a failed/timeout handshake leaked the non-blocking flag on the fd, making
    // all subsequent I/O on that socket unpredictable.
    auto restoreBlocking = [&]() {
        if (wasBlocking)
            fd_util::SetNonBlocking(fd, false);
    };

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);

    while (true) {
        int ret = asServer ? SSL_accept(ssl) : SSL_connect(ssl);
        if (ret == 1) {
            restoreBlocking();
            return true;
        }

        int err = SSL_get_error(ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            restoreBlocking();
            return false;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            restoreBlocking();
            return false;
        }

        double remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
        bool forWrite = (err == SSL_ERROR_WANT_WRITE);
        if (!fd_util::WaitUntilReady(fd, remaining, forWrite)) {
            restoreBlocking();
            return false;
        }
    }
}

bool TLSContext::InitServer() {
    if (!LoadOrCreateCert()) return false;

    ctxServer_ = SSL_CTX_new(TLS_server_method());
    if (!ctxServer_) return false;

    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";

    // [PPSSPP-FORK] SR6: check every OpenSSL load return value. A corrupt or
    // unreadable cert/key must abort instead of silently continuing with a
    // half-initialised context.
    if (SSL_CTX_use_certificate_file(ctxServer_, certPath.ToString().c_str(), SSL_FILETYPE_PEM) <= 0) {
        WARN_LOG(Log::System, "LANSync: failed to load server cert %s", certPath.ToString().c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxServer_);
        ctxServer_ = nullptr;
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(ctxServer_, keyPath.ToString().c_str(), SSL_FILETYPE_PEM) <= 0) {
        WARN_LOG(Log::System, "LANSync: failed to load server key %s", keyPath.ToString().c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxServer_);
        ctxServer_ = nullptr;
        return false;
    }
    if (!SSL_CTX_check_private_key(ctxServer_)) {
        WARN_LOG(Log::System, "LANSync: server cert/key mismatch");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxServer_);
        ctxServer_ = nullptr;
        return false;
    }

    SSL_CTX_set_cipher_list(ctxServer_, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
    SSL_CTX_set_ecdh_auto(ctxServer_, 1);

    // Request client cert for mutual TLS. Accept any cert — TOFU verification at app level.
    SSL_CTX_set_verify(ctxServer_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
        [](int preverify_ok, X509_STORE_CTX *ctx) -> int {
            (void)preverify_ok;
            (void)ctx;
            return 1;
        });

    return true;
}

bool TLSContext::InitClient() {
    if (!LoadOrCreateCert()) return false;

    ctxClient_ = SSL_CTX_new(TLS_client_method());
    if (!ctxClient_) return false;

    // Load client cert + key so server can request it for mutual TLS
    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";
    if (SSL_CTX_use_certificate_file(ctxClient_, certPath.ToString().c_str(), SSL_FILETYPE_PEM) <= 0) {
        WARN_LOG(Log::System, "LANSync: failed to load client cert %s", certPath.ToString().c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxClient_);
        ctxClient_ = nullptr;
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(ctxClient_, keyPath.ToString().c_str(), SSL_FILETYPE_PEM) <= 0) {
        WARN_LOG(Log::System, "LANSync: failed to load client key %s", keyPath.ToString().c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxClient_);
        ctxClient_ = nullptr;
        return false;
    }
    if (!SSL_CTX_check_private_key(ctxClient_)) {
        WARN_LOG(Log::System, "LANSync: client cert/key mismatch");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctxClient_);
        ctxClient_ = nullptr;
        return false;
    }

    SSL_CTX_set_verify(ctxClient_, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_cipher_list(ctxClient_, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
    SSL_CTX_set_ecdh_auto(ctxClient_, 1);

    return true;
}

TLSConnection::TLSConnection(SSL *ssl, int fd)
    : ssl_(ssl), fd_(fd) {}

TLSConnection::~TLSConnection() {
    Close();
}

bool TLSConnection::Handshake() {
    if (!ssl_) return false;
    int ret = SSL_accept(ssl_);
    if (ret != 1) {
        ret = SSL_connect(ssl_);
        if (ret != 1) {
            return false;
        }
    }
    return true;
}

int TLSConnection::Read(void *buf, int num) {
    if (!ssl_) return -1;
    ERR_clear_error();
    return SSL_read(ssl_, buf, num);
}

int TLSConnection::Write(const void *buf, int num) {
    if (!ssl_) return -1;
    ERR_clear_error();
    return SSL_write(ssl_, buf, num);
}

void TLSConnection::Close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (fd_ >= 0) {
        closesocket(fd_);
        fd_ = -1;
    }
}

}  // namespace LANSync
