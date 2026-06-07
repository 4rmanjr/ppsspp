// PPSSPP Project - LAN Save State Sync
// Platform Key Store - Unix implementation
//
// Phase 1: Simple encrypted file storage using internal SHA-256 for key derivation.
// Phase 5: Upgrade to libsecret (Linux) / Keychain (macOS) + AES-GCM.
//
// This uses only PPSSPP-internal dependencies (Common/Crypto/sha256.h).
// Zero external library dependencies to avoid breaking existing builds.

#include "ppsspp_config.h"

#if (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC)) && !PPSSPP_PLATFORM(ANDROID)

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <cstring>

#include "Common/Net/PlatformKeyStore.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Crypto/sha256.h"

namespace PlatformKeyStore {

static std::mutex storeMutex;
static std::map<std::string, std::string> cache_;
static Path storePath_;

// Derive 256-bit key from device-specific data.
// Phase 1: Simple hash-based derivation.
// Phase 5: Native platform key (libsecret / Keychain).
static std::vector<uint8_t> DeriveKey() {
	const char *seed = "PPSSPP-LANSync-v1-KDF";
	sha256_context ctx;
	uint8_t hash[32];
	sha256_starts(&ctx);
	sha256_update(&ctx, (const uint8_t *)seed, strlen(seed));

	// Mix in hostname for per-machine determinism
	char hostname[256] = {0};
	if (gethostname(hostname, sizeof(hostname)) == 0) {
		sha256_update(&ctx, (const uint8_t *)hostname, strlen(hostname));
	}
	sha256_finish(&ctx, hash);

	return std::vector<uint8_t>(hash, hash + 32);
}

// Phase 1: XOR obfuscation (not encryption).
// Keys are stored obfuscated, not encrypted. Phase 5 will use AES-256-GCM
// via platform-native APIs (libsecret / Keychain).
static std::string Obfuscate(const std::vector<uint8_t> &key, const std::string &data) {
	std::string result = data;
	for (size_t i = 0; i < result.size(); i++) {
		result[i] ^= key[i % key.size()];
	}
	return result;
}

static Path GetStorePath() {
	if (!storePath_.empty()) return storePath_;
	storePath_ = Path("~/.config/ppsspp/lansync_secrets.dat");
	return storePath_;
}

void Init() {
	std::lock_guard<std::mutex> lock(storeMutex);
	storePath_ = GetStorePath();

	if (File::Exists(storePath_)) {
		std::string fileData;
		if (File::ReadBinaryFileToString(storePath_, &fileData)) {
			auto key = DeriveKey();
			std::string plaintext = Obfuscate(key, fileData);

			// Parse key:value\n format
			size_t pos = 0;
			while (pos < plaintext.size()) {
				size_t nl = plaintext.find('\n', pos);
				if (nl == std::string::npos) nl = plaintext.size();
				std::string line = plaintext.substr(pos, nl - pos);
				pos = nl + 1;
				if (line.empty()) continue;

				size_t colon = line.find(':');
				if (colon != std::string::npos) {
					cache_[line.substr(0, colon)] = line.substr(colon + 1);
				}
			}
			INFO_LOG(Log::System, "PlatformKeyStore: loaded %d secrets",
			         (int)cache_.size());
		}
	}
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(storeMutex);

	// Serialize
	std::string data;
	for (const auto &[keyName, value] : cache_) {
		data += keyName + ":" + value + "\n";
	}

	auto key = DeriveKey();
	std::string obfuscated = Obfuscate(key, data);
	File::WriteStringToFile(false, obfuscated, storePath_);
}

bool Save(const std::string &name, const std::string &data) {
	std::lock_guard<std::mutex> lock(storeMutex);
	cache_[name] = data;
	return true;
}

std::string Load(const std::string &name) {
	std::lock_guard<std::mutex> lock(storeMutex);
	auto it = cache_.find(name);
	return (it != cache_.end()) ? it->second : std::string();
}

bool Remove(const std::string &name) {
	std::lock_guard<std::mutex> lock(storeMutex);
	return cache_.erase(name) > 0;
}

bool Exists(const std::string &name) {
	std::lock_guard<std::mutex> lock(storeMutex);
	return cache_.find(name) != cache_.end();
}

bool SaveBinary(const std::string &name, const std::vector<uint8_t> &data) {
	return Save(name, std::string((const char *)data.data(), data.size()));
}

bool LoadBinary(const std::string &name, std::vector<uint8_t> &data) {
	std::string str = Load(name);
	if (str.empty()) return false;
	data.assign(str.begin(), str.end());
	return true;
}

}  // namespace PlatformKeyStore

#endif  // (LINUX || MAC) && !ANDROID
