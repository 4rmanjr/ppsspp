#include "LANSync/LANSyncDiscovery.h"
#include "LANSync/MDNS.h"
#include "Common/File/FileDescriptor.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Log.h"
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

namespace LANSync {

LANSyncDiscovery::LANSyncDiscovery() {
}

LANSyncDiscovery::~LANSyncDiscovery() {
	Stop();
}

bool LANSyncDiscovery::Start(DiscoveryCallback callback) {
	if (running_)
		return false;

	callback_ = std::move(callback);

	announcer_.reset(CreateMDNSAnnouncer());
	browser_.reset(CreateMDNSBrowser());

	if (announcer_) {
		announcer_->Start(kServiceType, port_, deviceName_);
	}

	if (browser_) {
		auto self = shared_from_this();
		browser_->Start(kServiceType,
			[self](const DiscoveredPeer &peer) { self->OnPeerFound(peer); },
			[self](const DiscoveredPeer &peer) { self->OnPeerLost(peer); }
		);
	}

	running_ = true;
	probeThread_ = std::thread(&LANSyncDiscovery::ManualPeerLoop, this);

	return true;
}

void LANSyncDiscovery::Stop() {
	running_ = false;

	if (browser_) {
		browser_->Stop();
		browser_.reset();
	}
	if (announcer_) {
		announcer_->Stop();
		announcer_.reset();
	}

	probeCv_.notify_all();
	if (probeThread_.joinable())
		probeThread_.join();

	callback_ = DiscoveryCallback();
}

void LANSyncDiscovery::AddManualPeer(const std::string &host, int port) {
	{
		std::lock_guard<std::mutex> lock(manualMutex_);
		auto it = std::find_if(manualPeers_.begin(), manualPeers_.end(),
			[&](const ManualPeer &mp) { return mp.host == host && mp.port == port; });
		if (it != manualPeers_.end())
			return;
		manualPeers_.push_back({host, port});
	}

	TryConnectManual(host, port);
	probeCv_.notify_one();
}

void LANSyncDiscovery::RemoveManualPeer(const std::string &host, int port) {
	DiscoveredPeer removed;
	removed.host = host;
	removed.port = port;

	{
		std::lock_guard<std::mutex> lock(manualMutex_);
		auto it = std::remove_if(manualPeers_.begin(), manualPeers_.end(),
			[&](const ManualPeer &mp) { return mp.host == host && mp.port == port; });
		if (it == manualPeers_.end())
			return;
		manualPeers_.erase(it, manualPeers_.end());
	}

	bool found = false;
	{
		std::lock_guard<std::mutex> lock(peersMutex_);
		auto it = std::remove_if(peers_.begin(), peers_.end(),
			[&](const DiscoveredPeer &p) { return p.host == host && p.port == port; });
		if (it != peers_.end()) {
			removed = *it;
			peers_.erase(it, peers_.end());
			found = true;
		}
	}

	if (found && callback_) {
		DiscoveryEvent ev;
		ev.type = DiscoveryEvent::PEER_LOST;
		ev.peer = removed;
		callback_(ev);
	}
}

std::vector<DiscoveredPeer> LANSyncDiscovery::GetPeers() const {
	std::lock_guard<std::mutex> lock(peersMutex_);
	return peers_;
}

void LANSyncDiscovery::SetDeviceName(const std::string &name) {
	deviceName_ = name;
	if (announcer_ && running_) {
		announcer_->Stop();
		announcer_->Start(kServiceType, port_, deviceName_);
	}
}

std::string LANSyncDiscovery::GetDeviceName() const {
	return deviceName_;
}

void LANSyncDiscovery::OnPeerFound(const DiscoveredPeer &peer) {
	if (!deviceName_.empty() && peer.deviceName == deviceName_)
		return;
	if (peer.host == "127.0.0.1" || peer.host == "::1" ||
		peer.host.compare(0, 4, "169.") == 0) {
		WARN_LOG(Log::System, "Discovery: skipping loopback/APIPA %s", peer.host.c_str());
		return;
	}

	bool isNew = false;
	bool isUpdated = false;

	{
		std::lock_guard<std::mutex> lock(peersMutex_);
		auto it = std::find_if(peers_.begin(), peers_.end(),
			[&](const DiscoveredPeer &p) { return p.peerId == peer.peerId && !peer.peerId.empty(); });

		if (it == peers_.end()) {
			it = std::find_if(peers_.begin(), peers_.end(),
				[&](const DiscoveredPeer &p) { return p.host == peer.host && p.port == peer.port; });
		}

		if (it != peers_.end()) {
			if (it->deviceName != peer.deviceName) {
				*it = peer;
				isUpdated = true;
			}
		} else {
			peers_.push_back(peer);
			isNew = true;
		}
	}

	if (callback_) {
		DiscoveryEvent ev;
		if (isNew) {
			ev.type = DiscoveryEvent::PEER_FOUND;
		} else if (isUpdated) {
			ev.type = DiscoveryEvent::PEER_UPDATED;
		}
		if (isNew || isUpdated) {
			ev.peer = peer;
			callback_(ev);
		}
	}
}

void LANSyncDiscovery::OnPeerLost(const DiscoveredPeer &peer) {
	if (!deviceName_.empty() && peer.deviceName == deviceName_)
		return;

	DiscoveredPeer removed = peer;
	bool found = false;

	{
		std::lock_guard<std::mutex> lock(peersMutex_);
		auto it = std::find_if(peers_.begin(), peers_.end(),
			[&](const DiscoveredPeer &p) { return p.peerId == peer.peerId && !peer.peerId.empty(); });

		if (it == peers_.end()) {
			it = std::find_if(peers_.begin(), peers_.end(),
				[&](const DiscoveredPeer &p) { return p.host == peer.host && p.port == peer.port; });
		}

		if (it != peers_.end()) {
			removed = *it;
			peers_.erase(it);
			found = true;
		}
	}

	if (found && callback_) {
		DiscoveryEvent ev;
		ev.type = DiscoveryEvent::PEER_LOST;
		ev.peer = removed;
		callback_(ev);
	}
}

void LANSyncDiscovery::TryConnectManual(const std::string &host, int port) {
	int fd = fd_util::ConnectWithTimeout(host.c_str(), port, 2);
	if (fd < 0)
		return;

	closesocket(fd);

	{
		DiscoveredPeer peer;
		peer.host = host;
		peer.port = port;
		peer.deviceName = host + ":" + std::to_string(port);

		bool isNew = false;
		{
			std::lock_guard<std::mutex> lock(peersMutex_);
			auto it = std::find_if(peers_.begin(), peers_.end(),
				[&](const DiscoveredPeer &p) { return p.host == host && p.port == port; });
			if (it == peers_.end()) {
				peers_.push_back(peer);
				isNew = true;
			}
		}

		if (isNew && callback_) {
			DiscoveryEvent ev;
			ev.type = DiscoveryEvent::PEER_FOUND;
			ev.peer = peer;
			callback_(ev);
		}
	}
}

void LANSyncDiscovery::ManualPeerLoop() {
	while (running_) {
		std::vector<ManualPeer> toProbe;
		{
			std::lock_guard<std::mutex> lock(manualMutex_);
			toProbe = manualPeers_;
		}

		for (const auto &mp : toProbe) {
			if (!running_) break;
			TryConnectManual(mp.host, mp.port);
		}

		std::unique_lock<std::mutex> lock(manualMutex_);
		probeCv_.wait_for(lock, std::chrono::seconds(10), [this] { return !running_; });
	}
}

} // namespace LANSync
