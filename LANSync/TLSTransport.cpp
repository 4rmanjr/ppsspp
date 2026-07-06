#include "LANSync/TLSTransport.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Net/SocketCompat.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
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
    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
}

bool TLSContext::LoadOrCreateCert() {
    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";

    if (File::Exists(certPath) && File::Exists(keyPath)) {
        FILE *certFile = File::OpenCFile(certPath, "rb");
        FILE *keyFile = File::OpenCFile(keyPath, "rb");
        if (certFile && keyFile) {
            X509 *cert = PEM_read_X509(certFile, nullptr, nullptr, nullptr);
            EVP_PKEY *key = PEM_read_PrivateKey(keyFile, nullptr, nullptr, nullptr);
            fclose(certFile);
            fclose(keyFile);

            if (cert && key) {
                EVP_PKEY_free(key);
                fingerprint_ = ComputeFingerprint(cert);
                X509_free(cert);
                certInitialized_ = true;
                return true;
            }
            if (cert) X509_free(cert);
            if (key) EVP_PKEY_free(key);
        } else {
            if (certFile) fclose(certFile);
            if (keyFile) fclose(keyFile);
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

    FILE *certFile = File::OpenCFile(certPath, "wb");
    if (certFile) {
        PEM_write_X509(certFile, cert);
        fclose(certFile);
    }

    FILE *keyFile = File::OpenCFile(keyPath, "wb");
    if (keyFile) {
        PEM_write_PrivateKey(keyFile, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(keyFile);
    }

    fingerprint_ = ComputeFingerprint(cert);

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

std::string TLSContext::ComputeFingerprint(X509 *cert) {
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

bool TLSContext::InitServer() {
    if (!LoadOrCreateCert()) return false;

    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) return false;

    Path certPath = certDir_ / "sync_cert.pem";
    Path keyPath = certDir_ / "sync_key.pem";

    SSL_CTX_use_certificate_file(ctx_, certPath.ToString().c_str(), SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx_, keyPath.ToString().c_str(), SSL_FILETYPE_PEM);

    SSL_CTX_set_cipher_list(ctx_, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
    SSL_CTX_set_ecdh_auto(ctx_, 1);

    return true;
}

bool TLSContext::InitClient() {
    if (!LoadOrCreateCert()) return false;

    ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ctx_) return false;

    SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_verify_depth(ctx_, 1);

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
