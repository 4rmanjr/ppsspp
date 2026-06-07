// PPSSPP Project - LAN Save State Sync
// Platform-specific secure key storage (abstraction over Keystore/DPAPI/libsecret/Keychain)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <vector>

// Abstract interface for platform-specific secure key storage.
// Each platform implements this using its native keystore:
//   Android:  Android Keystore (KeyGenParameterSpec / KeyStore)
//   Windows:  DPAPI (CryptProtectData / CryptUnprotectData)
//   Linux:    libsecret (secret_password_store_sync / secret_password_lookup_sync)
//   macOS:    Keychain Services (SecItemAdd / SecItemCopyMatching)
//
// Fallback: encrypted file using OpenSSL (AES-256-GCM) with device-specific key.

namespace PlatformKeyStore {

// Save a named secret (TLS cert, TLS key, pairing token)
// Returns true on success, false on failure.
bool Save(const std::string &name, const std::string &data);

// Load a named secret. Returns empty string if not found.
std::string Load(const std::string &name);

// Delete a named secret. Returns true if deleted (or didn't exist).
bool Remove(const std::string &name);

// Check if a named secret exists.
bool Exists(const std::string &name);

// Save binary data (for cert/key PEM blobs)
bool SaveBinary(const std::string &name, const std::vector<uint8_t> &data);

// Load binary data
bool LoadBinary(const std::string &name, std::vector<uint8_t> &data);

// Initialize the keystore (called once at startup)
void Init();

// Shutdown (cleanup)
void Shutdown();

}  // namespace PlatformKeyStore
