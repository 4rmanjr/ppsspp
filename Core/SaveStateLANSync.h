// PPSSPP Project - LAN Save State Sync
// Save State LAN Sync Manager - core orchestration layer.
// Coordinates mDNS discovery, UDP fallback, TLS connections,
// HTTP sync protocol, conflict resolution, and metadata management.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

#include "Common/File/Path.h"
#include "Common/Data/HLC.h"
#include "Core/SaveStateSyncMetadata.h"

namespace mDNS {
class Browser;
class Announcer;
}  // namespace mDNS

namespace UDPDiscovery {
class Announcer;
class Listener;
}  // namespace UDPDiscovery

namespace tls {
class TLSServerContext;
}

class SaveStateLANSync {
public:
	// === Types ===

	enum class SyncDirection {
		PUSH_ONLY,      // Only upload local saves
		PULL_ONLY,      // Only download remote saves
		BIDIRECTIONAL   // Both upload and download
	};

	enum class ConflictResolution {
		NEWEST_WINS,    // Auto-resolve by HLC comparison
		KEEP_LOCAL,     // Never overwrite local
		KEEP_REMOTE,    // Always pull remote
		PROMPT          // Ask user per conflict
	};

	enum class SyncStatus {
		IDLE,
		DISCOVERING,
		SCANNING,
		SYNCING,
		DONE,
		ERROR,
		CANCELLED
	};

	struct PeerInfo {
		std::string id;
		std::string name;
		std::string device;         // "PC" or "Android"
		std::string address;        // IP:port
		std::string host;
		int port = 0;
		std::string certFingerprint;
		std::string token;          // Auth token (from pairing)
		bool paired = false;
		bool online = false;
		int64_t lastSeen = 0;
	};

	struct SyncProgress {
		SyncStatus status = SyncStatus::IDLE;
		int totalGames = 0;
		int completedGames = 0;
		int totalSlots = 0;
		int completedSlots = 0;
		std::string currentGame;
		std::string currentFile;
		std::string currentPeer;
		std::string error;
	};

	struct ConflictInfo {
		std::string gameId;
		int slot = 0;
		HLC localHlc;
		HLC remoteHlc;
		HLC localParentHlc;
		HLC remoteParentHlc;
		int64_t localSize = 0;
		int64_t remoteSize = 0;
		std::string localHash;
		std::string remoteHash;
		std::string peerId;   // Which peer has the remote version
	};

	struct SyncResult {
		bool success = false;
		int uploaded = 0;
		int downloaded = 0;
		int conflicts = 0;
		int skipped = 0;
		int failed = 0;
		std::vector<ConflictInfo> unresolvedConflicts;
	};

	struct PendingPairRequest {
		std::string requestId;
		std::string peerId;
		std::string peerName;
		std::string device;
		std::string host;
		int port = 0;
		std::string token;          // Auth token (set when accepted, returned on poll)
		std::string nonce;          // Random nonce for numeric comparison
		std::string verificationCode; // Computed 6-digit code shown to both users
		double timestamp = 0;
		bool accepted = false;
		bool rejected = false;
		bool clientConfirmed = false;  // Client user confirmed codes match
		bool serverConfirmed = false;  // Server user confirmed codes match
	};

	using ProgressCallback = std::function<void(const SyncProgress &progress)>;
	using DoneCallback = std::function<void(const SyncResult &result)>;

	// === Singleton ===
	static SaveStateLANSync &Instance();

	// === Lifecycle ===
	void Init();
	void Shutdown();

	// === Discovery ===
	void StartDiscovery();
	void StopDiscovery();
	std::vector<PeerInfo> GetDiscoveredPeers() const;

	// Called by platform backends (e.g. Android JNI NsdManager callback)
	// to feed discovered peers into the core's peer list.
	void AddDiscoveredPeer(const PeerInfo &peer);
	void RemoveDiscoveredPeer(const std::string &id);

	// Called by platform backends to set device name and type after init
	// (in case user changed the name after LoadConfig)
	void SetDeviceInfo(const std::string &name, const std::string &type);

	// === Server ===
	bool StartServer();
	void StopServer();
	int GetServerPort() const;

	// === Pairing ===
	std::string GeneratePairingPin();  // Returns 6-digit PIN
	void CancelPairing();

	// Called by client after user enters PIN
	void PairWithPeer(const std::string &peerId, const std::string &pin,
	                  std::function<void(bool success, const std::string &error)> callback);
	// Called by server when PIN is validated
	void AcceptPairing(const std::string &peerId, const std::string &peerName,
	                   std::function<void(bool success)> callback);
	void UnpairPeer(const std::string &peerId);
	void AutoPairWithPeer(const std::string &host, int port,
	                      std::function<void(bool success, const std::string &error)> callback);
	std::vector<PendingPairRequest> GetPendingRequests() const;

	// === Sync ===
	void SyncWithPeer(const std::string &peerId,
	                  SyncDirection direction,
	                  ProgressCallback onProgress,
	                  DoneCallback onDone);
	void CancelSync();

	// === Conflict Resolution ===
	void ResolveConflict(const ConflictInfo &conflict, ConflictResolution resolution);
	void ResolveAllConflicts(ConflictResolution resolution);

	// === Hooks (called from SaveState.cpp) ===
	void OnSaveStateSaved(const std::string &gamePrefix, int slot);
	void OnSaveStateLoaded(const std::string &gamePrefix, int slot);

	// === State ===
	bool IsServerRunning() const;
	SyncStatus GetStatus() const;
	SyncProgress GetProgress() const;
	std::string GetCurrentPin() const;

	// === Pairing server handlers (API endpoints) ===
	void HandlePairRequest(const std::string &body, std::string &response);
	void HandleAutoPairRequest(const std::string &body, const std::string &clientHost, std::string &response);
	void HandlePairRespond(const std::string &body, std::string &response);
	void HandlePairStatus(const std::string &query, std::string &response);
	void HandlePairVerify(const std::string &body, std::string &response);
	void HandleSaveList(const std::string &gameId, std::string &response);
	void HandleSaveDownload(const std::string &gameId, int slot,
	                        std::vector<uint8_t> &data);
	void HandleSaveUpload(const std::string &gameId, int slot,
	                      const std::vector<uint8_t> &data, const std::string &hash,
	                      std::string &response);

	// Device identity (public for platform backends)
	std::string GetDeviceId() const;

private:
	SaveStateLANSync() = default;
	~SaveStateLANSync();

	SaveStateLANSync(const SaveStateLANSync &) = delete;
	SaveStateLANSync &operator=(const SaveStateLANSync &) = delete;

	// Internal
	void DiscoveryLoop();
	void AcceptConnectionsLoop();
	SyncResult DoSync(const PeerInfo &peer, ProgressCallback onProgress);

	bool LoadConfig();
	bool SaveConfig();
	void UpdatePeerTimestamp(const std::string &peerId);

	// State
	std::string deviceId_;
	std::string deviceName_;
	std::string deviceType_;
	std::string pairingPin_;
	int serverPort_ = 0;

	// Discovery
	std::unique_ptr<mDNS::Browser> mdnsBrowser_;
	std::unique_ptr<mDNS::Announcer> mdnsAnnouncer_;
	std::unique_ptr<UDPDiscovery::Announcer> udpAnnouncer_;
	std::unique_ptr<UDPDiscovery::Listener> udpListener_;

	// TLS
	std::unique_ptr<tls::TLSServerContext> tlsCtx_;

	// Peer tracking
	std::vector<PeerInfo> pairedPeers_;
	std::vector<PeerInfo> discoveredPeers_;
	mutable std::mutex peerMutex_;

	// Sync state
	std::atomic<SyncStatus> syncStatus_{SyncStatus::IDLE};
	SyncProgress syncProgress_;
	std::atomic<bool> syncCancelled_{false};
	mutable std::mutex syncMutex_;

	// Conflict queue
	std::vector<ConflictInfo> pendingConflicts_;
	mutable std::mutex conflictMutex_;

	// Server state
	std::atomic<bool> serverRunning_{false};
	int listenSock_ = -1;
	mutable std::mutex serverMutex_;

	// Pending pair requests
	mutable std::vector<PendingPairRequest> pendingRequests_;
	mutable std::mutex pendingMutex_;
	mutable int pendingRequestCounter_ = 0;

	// Background threads (must be joined before destruction)
	std::vector<std::thread> backgroundThreads_;
	mutable std::mutex threadMutex_;
	void AddBackgroundThread(std::thread t);
	void JoinAllThreads();
};
