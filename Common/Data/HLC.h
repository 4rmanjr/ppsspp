// PPSSPP Project - LAN Save State Sync
// Hybrid Logical Clock (HLC) for causal ordering of save states.
// Provides total ordering without requiring clock synchronization.
// Based on the HLC algorithm used by Riak, CockroachDB, etc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <cstdint>
#include <string>
#include <ctime>

// Hybrid Logical Clock: combines wall clock with logical counter
// to provide causally-ordered timestamps across distributed devices.
//
// wallTime: microseconds since epoch (max of local clock and received times)
// logical:  incremented when events happen at same wallTime
// deviceId: identifies the device that created this timestamp
//
// Ordering rule: (w1, l1) > (w2, l2) iff w1 > w2 OR (w1 == w2 AND l1 > l2)
// Equality:      (w1, l1) == (w2, l2) iff w1 == w2 AND l1 == l2
// Conflict:      parent differs → both modified independently

struct HLC {
	int64_t wallTime = 0;      // Microseconds since Unix epoch
	int64_t logical = 0;       // Logical counter per wallTime tick
	std::string deviceId;      // Device that created this timestamp

	HLC() = default;
	HLC(int64_t wt, int64_t l, const std::string &did)
		: wallTime(wt), logical(l), deviceId(did) {}

	bool operator>(const HLC &other) const {
		if (wallTime != other.wallTime)
			return wallTime > other.wallTime;
		return logical > other.logical;
	}

	bool operator<(const HLC &other) const {
		if (wallTime != other.wallTime)
			return wallTime < other.wallTime;
		return logical < other.logical;
	}

	bool operator==(const HLC &other) const {
		return wallTime == other.wallTime && logical == other.logical;
	}

	bool operator!=(const HLC &other) const {
		return !(*this == other);
	}

	// Called on every save state creation.
	// Returns a new HLC that is causally after this one.
	HLC Increment(const std::string &device) const {
		HLC next = *this;
		int64_t now = GetNowMicros();

		next.wallTime = std::max(wallTime, now);
		if (next.wallTime == wallTime) {
			// Same wall clock tick → increment logical counter
			next.logical++;
		} else {
			// New wall clock tick → reset logical
			next.logical = 0;
		}
		next.deviceId = device;
		return next;
	}

	// Called when receiving a save state from a peer during sync.
	// Merges local and remote HLCs after successful sync.
	HLC Merge(const HLC &remote, const std::string &device) const {
		HLC result;
		result.wallTime = std::max(wallTime, remote.wallTime);

		if (result.wallTime == wallTime && result.wallTime == remote.wallTime) {
			// Both at same wall time → max logical + 1
			result.logical = std::max(logical, remote.logical) + 1;
		} else if (result.wallTime == wallTime) {
			result.logical = logical + 1;
		} else if (result.wallTime == remote.wallTime) {
			result.logical = remote.logical + 1;
		} else {
			// New wall time → reset
			result.logical = 0;
		}
		result.deviceId = device;
		return result;
	}

	// Returns true if this HLC is causally after other HLC
	// (same parent → no conflict)
	bool IsAfter(const HLC &other) const {
		if (wallTime > other.wallTime) return true;
		if (wallTime == other.wallTime && logical > other.logical) return true;
		return false;
	}

	// Returns true if this is "zero" (never set)
	bool IsZero() const {
		return wallTime == 0 && logical == 0;
	}

	// Serialization helpers
	std::string ToString() const;
	static HLC FromString(const std::string &str);

	// JSON helpers for sync metadata
	std::string ToJSON() const;
	static HLC FromJSON(const std::string &json);

private:
	static int64_t GetNowMicros();
};

// Conflict detection helper
struct ConflictResult {
	bool conflict = false;         // true if both sides modified independently
	HLC resolved;                   // merged HLC after resolution
	std::string reason;            // human-readable reason

	enum Action { KEEP_LOCAL, KEEP_REMOTE, MERGE, SKIP };
	Action action = SKIP;
};

// Detects conflicts and determines the correct action
// Returns the action and resolved HLC
ConflictResult DetectConflict(const HLC &localHlc,  const HLC &localParentHlc,
                               const HLC &remoteHlc, const HLC &remoteParentHlc);
