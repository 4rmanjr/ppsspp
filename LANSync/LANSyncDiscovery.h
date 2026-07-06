#pragma once

#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>
#include "LANSync/LANSyncProtocol.h"

namespace LANSync {

struct DiscoveryEvent {
	enum Type {
		PEER_FOUND,
		PEER_LOST,
		PEER_UPDATED,
		ERROR,
	};
	Type type;
	DiscoveredPeer peer;
	std::string errorMessage;
};

using DiscoveryCallback = std::function<void(const DiscoveryEvent &event)>;

class LANSyncDiscovery : public std::enable_shared_from_this<LANSyncDiscovery> {
public:
	LANSyncDiscovery();
	~LANSyncDiscovery();

	bool Start(DiscoveryCallback callback);
	void Stop();

	void AddManualPeer(const std::string &host, int port);
	void RemoveManualPeer(const std::string &host, int port);

	std::vector<DiscoveredPeer> GetPeers() const;

	void SetDeviceName(const std::string &name);
	std::string GetDeviceName() const;

	bool IsRunning() const { return running_; }

private:
	void OnPeerFound(const DiscoveredPeer &peer);
	void OnPeerLost(const DiscoveredPeer &peer);
	void TryConnectManual(const std::string &host, int port);
	void ManualPeerLoop();

	std::unique_ptr<class MDNSAnnouncer> announcer_;
	std::unique_ptr<class MDNSBrowser> browser_;
	DiscoveryCallback callback_;

	mutable std::mutex peersMutex_;
	std::vector<DiscoveredPeer> peers_;
	std::string deviceName_;
	int port_ = 27314;
	std::atomic<bool> running_{false};

	struct ManualPeer { std::string host; int port; };
	std::vector<ManualPeer> manualPeers_;

	std::thread probeThread_;
	mutable std::mutex manualMutex_;
	std::condition_variable probeCv_;
};

} // namespace LANSync
