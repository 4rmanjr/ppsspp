// PPSSPP Project - LAN Save State Sync
// HLC implementation
#ifdef PPSSPP_LANSYNC

#include "ppsspp_config.h"

#include <chrono>

#include "Common/Data/HLC.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

std::string HLC::ToString() const {
	return StringFromFormat("%lld:%lld:%s", (long long)wallTime, (long long)logical, deviceId.c_str());
}

HLC HLC::FromString(const std::string &str) {
	HLC h;
	size_t colon1 = str.find(':');
	if (colon1 == std::string::npos) return h;
	size_t colon2 = str.find(':', colon1 + 1);
	if (colon2 == std::string::npos) return h;

	h.wallTime = strtoll(str.substr(0, colon1).c_str(), nullptr, 10);
	h.logical = strtoll(str.substr(colon1 + 1, colon2 - colon1 - 1).c_str(), nullptr, 10);
	h.deviceId = str.substr(colon2 + 1);
	return h;
}

std::string HLC::ToJSON() const {
	return StringFromFormat(
		"{\"wallTime\":%lld,\"logical\":%lld,\"deviceId\":\"%s\"}",
		(long long)wallTime, (long long)logical, deviceId.c_str()
	);
}

HLC HLC::FromJSON(const std::string &json) {
	HLC h;
	// Simple JSON parsing without external library
	size_t pos = json.find("\"wallTime\":");
	if (pos != std::string::npos) {
		h.wallTime = strtoll(json.c_str() + pos + 11, nullptr, 10);
	}
	pos = json.find("\"logical\":");
	if (pos != std::string::npos) {
		h.logical = strtoll(json.c_str() + pos + 10, nullptr, 10);
	}
	pos = json.find("\"deviceId\":\"");
	if (pos != std::string::npos) {
		pos += 12;
		size_t end = json.find('"', pos);
		if (end != std::string::npos) {
			h.deviceId = json.substr(pos, end - pos);
		}
	}
	return h;
}

int64_t HLC::GetNowMicros() {
	auto now = std::chrono::system_clock::now();
	auto us = std::chrono::duration_cast<std::chrono::microseconds>(
		now.time_since_epoch()
	);
	return us.count();
}

// Detailed conflict detection logic
ConflictResult DetectConflict(const HLC &localHlc,  const HLC &localParentHlc,
                               const HLC &remoteHlc, const HLC &remoteParentHlc) {
	ConflictResult result;

	// Both are zero → no saved state exists
	if (localHlc.IsZero() && remoteHlc.IsZero()) {
		result.action = ConflictResult::SKIP;
		result.reason = "both sides empty";
		return result;
	}

	// Only local exists
	if (!localHlc.IsZero() && remoteHlc.IsZero()) {
		result.action = ConflictResult::KEEP_LOCAL;
		result.reason = "local only";
		return result;
	}

	// Only remote exists
	if (localHlc.IsZero() && !remoteHlc.IsZero()) {
		result.action = ConflictResult::KEEP_REMOTE;
		result.reason = "remote only";
		return result;
	}

	// Both exist, check if causal relationship

	// Case 1: Remote is causally after local (linear history)
	if (localHlc == remoteParentHlc) {
		// Local was the base, remote built on top
		result.action = ConflictResult::KEEP_REMOTE;
		result.resolved = remoteHlc;
		result.reason = "remote is causally after local";
		return result;
	}

	// Case 2: Local is causally after remote (linear history)
	if (remoteHlc == localParentHlc) {
		// Remote was the base, local built on top
		result.action = ConflictResult::KEEP_LOCAL;
		result.resolved = localHlc;
		result.reason = "local is causally after remote";
		return result;
	}

	// Case 3: Same parent → identical origin, one side simply saved later
	if (localParentHlc == remoteParentHlc) {
		if (remoteHlc > localHlc) {
			result.action = ConflictResult::KEEP_REMOTE;
			result.reason = "same parent, remote is newer";
		} else if (localHlc > remoteHlc) {
			result.action = ConflictResult::KEEP_LOCAL;
			result.reason = "same parent, local is newer";
		} else {
			result.action = ConflictResult::SKIP;
			result.reason = "identical (same HLC)";
		}
		return result;
	}

	// Case 4: Different parents → TRUE CONFLICT
	// Both sides were modified independently since last sync
	result.conflict = true;
	result.action = ConflictResult::MERGE;
	result.reason = "both sides modified independently";
	result.resolved = localHlc.Merge(remoteHlc, localHlc.deviceId);

	WARN_LOG(Log::System, "HLC conflict detected: local(%s) parent(%s) vs remote(%s) parent(%s)",
	         localHlc.ToString().c_str(), localParentHlc.ToString().c_str(),
	         remoteHlc.ToString().c_str(), remoteParentHlc.ToString().c_str());

	return result;
}

#endif // PPSSPP_LANSYNC
