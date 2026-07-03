// PPSSPP Project - LAN Save State Sync
// TLS server/client with self-signed ECDSA P-256 + TOFU (Trust On First Use).
// Uses OpenSSL on Linux, falls back to plain TCP if OpenSSL unavailable.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#include "ppsspp_config.h"

#include <cstring>
#include <cstdint>
#include <string>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

#include "Common/Net/TLSServer.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Net/PlatformKeyStore.h"
#include "Common/Crypto/sha256.h"
#include "Common/Log.h"

// OpenSSL availability check
#if defined(__has_include)
#  if __has_include(<openssl/ssl.h>) && __has_include(<openssl/evp.h>) && __has_include(<openssl/x509.h>)
#    define HAS_OPENSSL 1
#  else
#    define HAS_OPENSSL 0
#  endif
#else
#  define HAS_OPENSSL 0
#endif

#if HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#endif

namespace tls {

// ==================== SHA-256 Utilities ====================

static std::string ComputeSHA256Hex(const void *data, size_t len) {
	sha256_context ctx;
	uint8_t hash[32];
	sha256_starts(&ctx);
	sha256_update(&ctx, (const uint8_t *)data, len);
	sha256_finish(&ctx, hash);

	std::ostringstream oss;
	for (int i = 0; i < 32; i++) {
		oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
	}
	return oss.str();
}

static std::string ComputeIdentityFingerprint() {
	char hostname[256] = {0};
	gethostname(hostname, sizeof(hostname));
	return ComputeSHA256Hex(hostname, strlen(hostname)).substr(0, 16);
}

#if HAS_OPENSSL

// ==================== OpenSSL Helpers ====================

static std::string GetSSLErrorString() {
	unsigned long err = ERR_get_error();
	if (err == 0) return "unknown error";
	char buf[256];
	ERR_error_string_n(err, buf, sizeof(buf));
	return std::string(buf);
}

static std::string ComputeCertFingerprint(X509 *cert) {
	unsigned char *der = nullptr;
	int derLen = i2d_X509(cert, &der);
	if (derLen <= 0) return "";

	std::string fp = ComputeSHA256Hex(der, derLen);
	OPENSSL_free(der);
	return fp;
}

// ==================== TLSServerContext (OpenSSL) ====================

TLSServerContext::TLSServerContext()
	: sslCtx_(nullptr), cert_(nullptr), pkey_(nullptr) {
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	sslCtx_ = SSL_CTX_new(TLS_server_method());
	if (sslCtx_) {
		SSL_CTX_set_min_proto_version((SSL_CTX *)sslCtx_, TLS1_2_VERSION);
		SSL_CTX_set_verify((SSL_CTX *)sslCtx_, SSL_VERIFY_NONE, nullptr);
	}
}

TLSServerContext::~TLSServerContext() {
	if (cert_) X509_free((X509 *)cert_);
	if (pkey_) EVP_PKEY_free((EVP_PKEY *)pkey_);
	if (sslCtx_) SSL_CTX_free((SSL_CTX *)sslCtx_);
}

std::string TLSServerContext::GenerateCertificate() {
	if (!sslCtx_) {
		fingerprint_ = ComputeIdentityFingerprint();
		return fingerprint_;
	}

	// Generate ECDSA P-256 keypair
	EVP_PKEY *pkey = EVP_PKEY_new();
	if (!pkey) {
		ERROR_LOG(Log::System, "TLS: EVP_PKEY_new failed");
		return "";
	}

	EC_KEY *ecKey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (!ecKey) {
		EVP_PKEY_free(pkey);
		ERROR_LOG(Log::System, "TLS: EC_KEY_new_by_curve_name failed");
		return "";
	}

	EC_KEY_set_asn1_flag(ecKey, OPENSSL_EC_NAMED_CURVE);
	if (!EC_KEY_generate_key(ecKey)) {
		EC_KEY_free(ecKey);
		EVP_PKEY_free(pkey);
		ERROR_LOG(Log::System, "TLS: EC_KEY_generate_key failed");
		return "";
	}

	EVP_PKEY_set1_EC_KEY(pkey, ecKey);
	EC_KEY_free(ecKey);
	pkey_ = pkey;

	// Create self-signed X.509 certificate
	X509 *cert = X509_new();
	if (!cert) {
		ERROR_LOG(Log::System, "TLS: X509_new failed");
		return "";
	}

	X509_set_version(cert, 2);  // v3
	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

	// Validity: now to 1 year
	X509_gmtime_adj(X509_get_notBefore(cert), 0);
	X509_gmtime_adj(X509_get_notAfter(cert), 365 * 86400);

	X509_set_pubkey(cert, pkey);

	// Subject: CN = PPSSPP-LANSync-<hostname>
	char hostname[256] = {0};
	gethostname(hostname, sizeof(hostname));
	std::string cn = std::string("PPSSPP-LANSync-") + hostname;
	X509_NAME *name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
	                           (const unsigned char *)cn.c_str(), -1, -1, 0);
	X509_set_issuer_name(cert, name);  // Self-signed

	// Sign with SHA-256
	X509_sign(cert, pkey, EVP_sha256());
	cert_ = cert;

	// Set in SSL context
	SSL_CTX_use_certificate((SSL_CTX *)sslCtx_, cert);
	SSL_CTX_use_PrivateKey((SSL_CTX *)sslCtx_, pkey);

	// Compute fingerprint
	fingerprint_ = ComputeCertFingerprint(cert);

	INFO_LOG(Log::System, "TLS: generated ECDSA P-256 self-signed cert, CN=%s, fingerprint=%s",
	         cn.c_str(), fingerprint_.c_str());

	return fingerprint_;
}

bool TLSServerContext::LoadCertificate(const std::string &certPem, const std::string &keyPem) {
	if (!sslCtx_ || certPem.empty() || keyPem.empty()) return false;

	BIO *bio = BIO_new_mem_buf(certPem.c_str(), certPem.size());
	if (!bio) return false;

	X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (!cert) {
		ERROR_LOG(Log::System, "TLS: failed to parse certificate PEM");
		return false;
	}

	bio = BIO_new_mem_buf(keyPem.c_str(), keyPem.size());
	if (!bio) { X509_free(cert); return false; }

	EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (!pkey) {
		ERROR_LOG(Log::System, "TLS: failed to parse private key PEM");
		X509_free(cert);
		return false;
	}

	if (cert_) X509_free((X509 *)cert_);
	if (pkey_) EVP_PKEY_free((EVP_PKEY *)pkey_);
	cert_ = cert;
	pkey_ = pkey;

	SSL_CTX_use_certificate((SSL_CTX *)sslCtx_, cert);
	SSL_CTX_use_PrivateKey((SSL_CTX *)sslCtx_, pkey);

	fingerprint_ = ComputeCertFingerprint(cert);

	INFO_LOG(Log::System, "TLS: loaded certificate, fingerprint=%s", fingerprint_.c_str());
	return true;
}

std::string TLSServerContext::GetCertificatePEM() const {
	if (!cert_) return "";
	BIO *bio = BIO_new(BIO_s_mem());
	PEM_write_bio_X509(bio, (X509 *)cert_);
	char *data;
	long len = BIO_get_mem_data(bio, &data);
	std::string result(data, len);
	BIO_free(bio);
	return result;
}

std::string TLSServerContext::GetPrivateKeyPEM() const {
	if (!pkey_) return "";
	BIO *bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(bio, (EVP_PKEY *)pkey_, nullptr, nullptr, 0, nullptr, nullptr);
	char *data;
	long len = BIO_get_mem_data(bio, &data);
	std::string result(data, len);
	BIO_free(bio);
	return result;
}

bool TLSServerContext::AcceptTLS(int clientFd, int &tlsFd) {
	if (!sslCtx_) {
		tlsFd = clientFd;
		return true;  // Fallback to plain TCP
	}

	// [PPSSPP-FORK] LANSync: Peek first byte to detect TLS vs plain HTTP.
	// Without this, SSL_accept consumes the HTTP request data on failure,
	// making it unavailable for the plain TCP handler below.
	char firstByte = 0;
	int peeked = recv(clientFd, (char *)&firstByte, 1, MSG_PEEK);
	if (peeked <= 0 || firstByte != 0x16) {  // 0x16 = TLS ContentType::handshake
		tlsFd = clientFd;
		return false;  // Not TLS — caller falls back to plain TCP
	}

	SSL *ssl = SSL_new((SSL_CTX *)sslCtx_);
	if (!ssl) return false;

	SSL_set_fd(ssl, clientFd);
	int ret = SSL_accept(ssl);
	if (ret <= 0) {
		int err = SSL_get_error(ssl, ret);
		WARN_LOG(Log::System, "TLS: SSL_accept failed: %d (%s)", err, GetSSLErrorString().c_str());
		SSL_free(ssl);
		return false;
	}

	// Store SSL* for lifecycle management
	sslMap_[clientFd] = ssl;
	tlsFd = clientFd;

	INFO_LOG(Log::System, "TLS: SSL_accept succeeded, cipher=%s", SSL_get_cipher_name(ssl));
	return true;
}

void *TLSServerContext::GetSSL(int fd) {
	auto it = sslMap_.find(fd);
	return (it != sslMap_.end()) ? it->second : nullptr;
}

void TLSServerContext::CloseTLS(int fd) {
	auto it = sslMap_.find(fd);
	if (it != sslMap_.end()) {
		SSL *ssl = (SSL *)it->second;
		if (ssl) {
			SSL_shutdown(ssl);
			SSL_free(ssl);
		}
		sslMap_.erase(it);
	}
}

#else  // !HAS_OPENSSL

// ==================== Fallback: Plain TCP ====================

TLSServerContext::TLSServerContext()
	: sslCtx_(nullptr), cert_(nullptr), pkey_(nullptr) {
	fingerprint_ = ComputeIdentityFingerprint();
	INFO_LOG(Log::System, "TLS: OpenSSL not available, using plain TCP");
}

TLSServerContext::~TLSServerContext() {}

std::string TLSServerContext::GenerateCertificate() {
	fingerprint_ = ComputeIdentityFingerprint();
	INFO_LOG(Log::System, "TLS: using plain-text transport, fingerprint=%s",
	         fingerprint_.c_str());
	return fingerprint_;
}

bool TLSServerContext::LoadCertificate(const std::string &certPem, const std::string &keyPem) {
	return true;  // No-op in plain TCP mode
}

std::string TLSServerContext::GetCertificatePEM() const { return ""; }
std::string TLSServerContext::GetPrivateKeyPEM() const { return ""; }

bool TLSServerContext::AcceptTLS(int clientFd, int &tlsFd) {
	tlsFd = clientFd;
	return true;  // Pass-through
}

void *TLSServerContext::GetSSL(int fd) {
	return nullptr;  // No TLS in fallback mode
}

void TLSServerContext::CloseTLS(int fd) {
	// No-op in plain TCP mode
}

#endif  // HAS_OPENSSL

// ==================== Common Methods ====================

std::string TLSServerContext::GetFingerprint() const {
	return fingerprint_;
}

bool TLSServerContext::SaveToKeystore() {
	bool ok = PlatformKeyStore::Save("ppsspp-lansync-fingerprint", fingerprint_);
#if HAS_OPENSSL
	if (ok && cert_ && pkey_) {
		std::string certPem = GetCertificatePEM();
		std::string keyPem = GetPrivateKeyPEM();
		if (!certPem.empty() && !keyPem.empty()) {
			PlatformKeyStore::Save("ppsspp-lansync-cert", certPem);
			PlatformKeyStore::Save("ppsspp-lansync-key", keyPem);
		}
	}
#endif
	return ok;
}

bool TLSServerContext::LoadFromKeystore() {
	std::string fp = PlatformKeyStore::Load("ppsspp-lansync-fingerprint");
	if (!fp.empty()) {
		fingerprint_ = fp;
	}

#if HAS_OPENSSL
	std::string certPem = PlatformKeyStore::Load("ppsspp-lansync-cert");
	std::string keyPem = PlatformKeyStore::Load("ppsspp-lansync-key");
	if (!certPem.empty() && !keyPem.empty()) {
		return LoadCertificate(certPem, keyPem);
	}
#endif
	return !fp.empty();
}

// ==================== TLSClientVerifier ====================

TLSClientVerifier::TLSClientVerifier()
	: sock_(-1), ssl_(nullptr), sslCtx_(nullptr), connected_(false) {
}

TLSClientVerifier::~TLSClientVerifier() {
	Close();
}

bool TLSClientVerifier::Connect(const std::string &host, int port,
                                const std::string &expectedFingerprint) {
	// TCP connect
	sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock_ < 0) return false;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

	if (connect(sock_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		closesocket(sock_);
		sock_ = -1;
		WARN_LOG(Log::System, "TLS: TCP connect to %s:%d failed", host.c_str(), port);
		return false;
	}

#if HAS_OPENSSL
	// TLS handshake
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		closesocket(sock_);
		sock_ = -1;
		return false;
	}

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	// Don't verify server cert (we use TOFU instead)
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

	SSL *ssl = SSL_new(ctx);
	if (!ssl) {
		SSL_CTX_free(ctx);
		closesocket(sock_);
		sock_ = -1;
		return false;
	}

	SSL_set_fd(ssl, sock_);
	int ret = SSL_connect(ssl);
	if (ret <= 0) {
		int err = SSL_get_error(ssl, ret);
		ERROR_LOG(Log::System, "TLS: SSL_connect failed: %d", err);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		closesocket(sock_);
		sock_ = -1;
		return false;
	}

	// TOFU: verify server certificate fingerprint
	X509 *serverCert = SSL_get_peer_certificate(ssl);
	if (serverCert) {
		std::string actualFp = ComputeCertFingerprint(serverCert);
		X509_free(serverCert);

		if (!expectedFingerprint.empty() && actualFp != expectedFingerprint) {
			ERROR_LOG(Log::System, "TLS: fingerprint mismatch! Expected=%s Got=%s",
			          expectedFingerprint.c_str(), actualFp.c_str());
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			closesocket(sock_);
			sock_ = -1;
			return false;
		}

		// Store fingerprint for future TOFU
		TrustFingerprint(host + ":" + std::to_string(port), actualFp);
	}

	ssl_ = ssl;
	sslCtx_ = ctx;
	connected_ = true;

	INFO_LOG(Log::System, "TLS: connected to %s:%d, cipher=%s",
	         host.c_str(), port, SSL_get_cipher_name(ssl));
#else
	// Plain TCP fallback
	connected_ = true;
	INFO_LOG(Log::System, "TLS: plain TCP connected to %s:%d (no TLS)", host.c_str(), port);
#endif

	return true;
}

int TLSClientVerifier::Read(void *buf, int len) {
	if (!connected_ || sock_ < 0) return -1;
#if HAS_OPENSSL
	if (ssl_) return SSL_read((SSL *)ssl_, buf, len);
#endif
	return recv(sock_, (char *)buf, len, 0);
}

int TLSClientVerifier::Write(const void *buf, int len) {
	if (!connected_ || sock_ < 0) return -1;
#if HAS_OPENSSL
	if (ssl_) return SSL_write((SSL *)ssl_, buf, len);
#endif
	return send(sock_, (const char *)buf, len, 0);
}

void TLSClientVerifier::Close() {
#if HAS_OPENSSL
	if (ssl_) {
		SSL_shutdown((SSL *)ssl_);
		SSL_free((SSL *)ssl_);
		ssl_ = nullptr;
	}
	if (sslCtx_) {
		SSL_CTX_free((SSL_CTX *)sslCtx_);
		sslCtx_ = nullptr;
	}
#endif
	if (sock_ >= 0) {
		closesocket(sock_);
		sock_ = -1;
	}
	connected_ = false;
}

bool TLSClientVerifier::IsConnected() const {
	return connected_;
}

bool TLSClientVerifier::ValidateFingerprint(const std::string &peerId,
                                            const std::string &fingerprint) {
	std::string stored = PlatformKeyStore::Load("ppsspp-peer-fp-" + peerId);
	if (stored.empty()) return true;  // First use, trust
	return stored == fingerprint;
}

void TLSClientVerifier::TrustFingerprint(const std::string &peerId,
                                         const std::string &fingerprint) {
	PlatformKeyStore::Save("ppsspp-peer-fp-" + peerId, fingerprint);
	INFO_LOG(Log::System, "TLS: trusted fingerprint for peer %s", peerId.c_str());
}

}  // namespace tls
