// PPSSPP Project - LAN Save State Sync
// Save state sync metadata implementation

#include "ppsspp_config.h"

#include <cstdint>

#include "Core/SaveStateSyncMetadata.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

std::string SaveStateSyncMetadata::ToJSON() const {
	std::string json;
	json += "{\n";
	json += StringFromFormat("  \"version\": %d,\n", version);
	json += StringFromFormat("  \"hash\": \"%s\",\n", hash.c_str());
	json += StringFromFormat("  \"hlc\": %s,\n", hlc.ToJSON().c_str());
	json += StringFromFormat("  \"parentHlc\": %s,\n", parentHlc.ToJSON().c_str());
	json += StringFromFormat("  \"lastSyncPeer\": \"%s\",\n", lastSyncPeer.c_str());
	json += StringFromFormat("  \"lastSyncTime\": %lld,\n", (long long)lastSyncTime);
	json += StringFromFormat("  \"ppssppVersion\": \"%s\",\n", ppssppVersion.c_str());
	json += StringFromFormat("  \"saveFormatVersion\": %d,\n", saveFormatVersion);
	json += StringFromFormat("  \"deviceId\": \"%s\",\n", deviceId.c_str());
	json += StringFromFormat("  \"fileSize\": %lld\n", (long long)fileSize);
	json += "}\n";
	return json;
}

SaveStateSyncMetadata SaveStateSyncMetadata::FromJSON(const std::string &json) {
	SaveStateSyncMetadata meta;

	// Simple JSON field extraction (no external JSON library)
	auto getInt = [&json](const char *key) -> int64_t {
		std::string search = std::string("\"") + key + "\":";
		size_t pos = json.find(search);
		if (pos == std::string::npos) return 0;
		pos += search.size();
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
		return strtoll(json.c_str() + pos, nullptr, 10);
	};

	auto getStr = [&json](const char *key) -> std::string {
		std::string search = std::string("\"") + key + "\": \"";
		size_t pos = json.find(search);
		if (pos == std::string::npos) return "";
		pos += search.size();
		size_t end = json.find('"', pos);
		if (end == std::string::npos) return "";
		return json.substr(pos, end - pos);
	};

	meta.version = (int)getInt("version");
	meta.hash = getStr("hash");
	meta.lastSyncPeer = getStr("lastSyncPeer");
	meta.lastSyncTime = getInt("lastSyncTime");
	meta.ppssppVersion = getStr("ppssppVersion");
	meta.saveFormatVersion = (int)getInt("saveFormatVersion");
	meta.deviceId = getStr("deviceId");
	meta.fileSize = getInt("fileSize");

	// Parse HLC objects
	size_t hlcPos = json.find("\"hlc\":");
	if (hlcPos != std::string::npos) {
		meta.hlc = HLC::FromJSON(json.substr(hlcPos + 6));
	}

	size_t parentPos = json.find("\"parentHlc\":");
	if (parentPos != std::string::npos) {
		meta.parentHlc = HLC::FromJSON(json.substr(parentPos + 12));
	}

	return meta;
}

Path SaveStateSyncMetadata::SidecarPath(const Path &ppstPath) {
	return ppstPath.WithExtraExtension(".sync.json");
}

bool SaveStateSyncMetadata::ReadFromFile(const Path &ppstPath, SaveStateSyncMetadata &meta) {
	Path sidecar = SidecarPath(ppstPath);
	if (!File::Exists(sidecar)) {
		return false;
	}

	std::string json;
	if (!File::ReadTextFileToString(sidecar, &json)) {
		ERROR_LOG(Log::System, "LANSync: failed to read metadata file: %s", sidecar.c_str());
		return false;
	}

	meta = FromJSON(json);

	if (!meta.IsValid()) {
		WARN_LOG(Log::System, "LANSync: invalid metadata in %s", sidecar.c_str());
		return false;
	}

	return true;
}

bool SaveStateSyncMetadata::WriteToFile(const Path &ppstPath) const {
	Path sidecar = SidecarPath(ppstPath);
	std::string json = ToJSON();

	bool ok = File::WriteStringToFile(true, json, sidecar);
	if (!ok) {
		ERROR_LOG(Log::System, "LANSync: failed to write metadata: %s", sidecar.c_str());
	}
	return ok;
}
