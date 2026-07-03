// [PPSSPP-FORK] LANSync: Save state sync metadata sidecar file (.ppst.sync.json)
// Only add new lines. Do not delete/modify upstream lines.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#ifdef PPSSPP_LANSYNC

#include <string>
#include <string_view>
#include <cstdint>

#include "Common/File/Path.h"
#include "Common/Data/HLC.h"

// Metadata stored alongside each save state (.ppst) file for sync tracking.
// File: <savestate_dir>/ULUS12345_1.00_1.ppst.sync.json

struct SaveStateSyncMetadata {
	// Sync version (increments when format changes)
	int version = 2;

	// SHA-256 hash of the .ppst file content
	std::string hash;

	// HLC timestamp (causally-ordered)
	HLC hlc;

	// Parent HLC (the HLC of the state this was synced from)
	// Used to detect conflicts: if both sides have different parent HLC,
	// then both were modified independently → conflict.
	HLC parentHlc;

	// The last peer we synced this save state with
	std::string lastSyncPeer;

	// When we last synced (Unix timestamp, seconds)
	int64_t lastSyncTime = 0;

	// PPSSPP version that created/synced this save
	std::string ppssppVersion;

	// Save state format version (from SaveState::saveStateGeneration)
	// Used to reject incompatible saves before transfer
	int saveFormatVersion = 0;

	// Device that created/synced this
	std::string deviceId;

	// File size of the .ppst file (for progress reporting)
	int64_t fileSize = 0;

	// Serialize to JSON string
	std::string ToJSON() const;

	// Deserialize from JSON string
	static SaveStateSyncMetadata FromJSON(const std::string &json);

	// Read from sidecar file
	// Returns true if file exists and was parsed successfully
	static bool ReadFromFile(const Path &ppstPath, SaveStateSyncMetadata &meta);

	// Write to sidecar file
	bool WriteToFile(const Path &ppstPath) const;

	// Get sidecar file path for a given .ppst path
	static Path SidecarPath(const Path &ppstPath);

	// Check if two save states are identical (same hash = same content)
	bool IsIdenticalTo(const SaveStateSyncMetadata &other) const {
		return hash == other.hash && !hash.empty();
	}

	// Check if this is a valid (non-empty) metadata
	bool IsValid() const {
		return !hash.empty() && !hlc.IsZero();
	}
};

#endif // PPSSPP_LANSYNC
