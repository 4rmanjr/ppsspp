// PPSSPP Project - LAN Save State Sync
// TLS server/client with self-signed ECDSA P-256 + TOFU (Trust On First Use).
// Uses OpenSSL when available, falls back to plain TCP otherwise.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <map>

#include "Common/Net/PlatformKeyStore.h"

namespace http {
class Server;
class NewThreadExecutor;
}  // namespace http

namespace tls {

// Server-side TLS context with self-signed certificate
class TLSServerContext {
public:
	TLSServerContext();
	~TLSServerContext();

	// Generate new ECDSA P-256 certificate (self-signed)
	// Returns the SHA-256 fingerprint (for mDNS announcement)
	std::string GenerateCertificate();

	// Load existing certificate from keystore
	bool LoadCertificate(const std::string &certPem, const std::string &keyPem);

	// Get the cert PEM for storage
	std::string GetCertificatePEM() const;
	std::string GetPrivateKeyPEM() const;

	// Get SHA-256 fingerprint (hex string)
	std::string GetFingerprint() const;

	// Store cert+key in PlatformKeyStore
	bool SaveToKeystore();
	bool LoadFromKeystore();

	// Initialize TLS context on existing socket
	// On success, stores SSL* internally; use GetSSL() to retrieve for I/O
	bool AcceptTLS(int clientFd, int &tlsFd);

	// Get SSL* for a given fd (after AcceptTLS). Returns nullptr if not found.
	void *GetSSL(int fd);

	// Close TLS connection for a given fd and free SSL*
	void CloseTLS(int fd);

private:
	void *sslCtx_;  // SSL_CTX* (opaque, avoid OpenSSL header in .h)
	void *cert_;    // X509*
	void *pkey_;    // EVP_PKEY*
	std::string fingerprint_;
	std::map<int, void *> sslMap_;  // fd -> SSL* for lifecycle management
};

// Client-side TOFU (Trust On First Use) cert verification
class TLSClientVerifier {
public:
	TLSClientVerifier();
	~TLSClientVerifier();

	// Connect to TLS server and verify fingerprint
	// Returns true if connected and fingerprint matches (or first use)
	bool Connect(const std::string &host, int port,
	             const std::string &expectedFingerprint);

	// Read data from TLS connection
	int Read(void *buf, int len);

	// Write data to TLS connection
	int Write(const void *buf, int len);

	// Close connection
	void Close();

	// Check if connected
	bool IsConnected() const;

	// Validate a peer's certificate fingerprint against stored value
	static bool ValidateFingerprint(const std::string &peerId,
	                                const std::string &fingerprint);

	// Store a trusted fingerprint for a peer (TOFU)
	static void TrustFingerprint(const std::string &peerId,
	                             const std::string &fingerprint);

private:
	int sock_ = -1;
	void *ssl_;  // SSL*
	void *sslCtx_;  // SSL_CTX*
	bool connected_ = false;
};

}  // namespace tls
