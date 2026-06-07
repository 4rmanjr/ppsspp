// PPSSPP Project - LAN Save State Sync
// Platform Key Store - Android
// Uses file-based XOR-obfuscated storage (same as Unix fallback).
// Full Android Keystore integration via Java KeyStore in a future enhancement.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

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
#include "Common/Crypto/sha256.h"

namespace PlatformKeyStore {

static std::mutex storeMutex;
static std::map<std::string, std::string> cache_;
static Path storePath_;

static std::vector<uint8_t> DeriveKey() {
	const char *seed = "PPSSPP-LANSync-v1-KDF";
	sha256_context ctx;
	uint8_t hash[32];
	sha256_starts(&ctx);
	sha256_update(&ctx, (const uint8_t *)seed, strlen(seed));
	sha256_finish(&ctx, hash);
	return std::vector<uint8_t>(hash, hash + 32);
}

static std::string Obfuscate(const std::vector<uint8_t> &key, const std::string &data) {
	std::string result = data;
	for (size_t i = 0; i < result.size(); i++)
		result[i] ^= key[i % key.size()];
	return result;
}

static Path GetStorePath() {
	if (!storePath_.empty()) return storePath_;
	// Use app-private internal storage
	storePath_ = Path("lansync_secrets.dat");
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
			size_t pos = 0;
			while (pos < plaintext.size()) {
				size_t nl = plaintext.find('\n', pos);
				if (nl == std::string::npos) nl = plaintext.size();
				std::string line = plaintext.substr(pos, nl - pos);
				pos = nl + 1;
				if (line.empty()) continue;
				size_t colon = line.find(':');
				if (colon != std::string::npos)
					cache_[line.substr(0, colon)] = line.substr(colon + 1);
			}
		}
	}
	INFO_LOG(Log::System, "PlatformKeyStore: Android file-based (%d secrets)", (int)cache_.size());
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(storeMutex);
	std::string data;
	for (const auto &[k, v] : cache_)
		data += k + ":" + v + "\n";
	auto key = DeriveKey();
	File::WriteStringToFile(false, Obfuscate(key, data), storePath_);
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

#endif  // PPSSPP_PLATFORM(ANDROID)
