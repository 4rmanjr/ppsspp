#pragma once
#include <functional>
#include <string>
#include "LANSync/LANSyncProtocol.h"

namespace LANSync {

class MDNSAnnouncer {
public:
	virtual ~MDNSAnnouncer() = default;
	virtual bool Start(const std::string &serviceType, int port, const std::string &deviceName, const std::string &peerId) = 0;
	virtual void Stop() = 0;
};

class MDNSBrowser {
public:
	using OnPeerFound = std::function<void(const DiscoveredPeer &peer)>;
	using OnPeerLost = std::function<void(const DiscoveredPeer &peer)>;
	virtual ~MDNSBrowser() = default;
	virtual bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) = 0;
	virtual void Stop() = 0;
};

MDNSAnnouncer *CreateMDNSAnnouncer();
MDNSBrowser *CreateMDNSBrowser();

}  // namespace LANSync
