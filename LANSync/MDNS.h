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
	using OnError = std::function<void(const std::string &msg)>;
	virtual ~MDNSBrowser() = default;
	virtual bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) = 0;
	virtual void Stop() = 0;
	// Optional error reporting (e.g. permission denied on Android). Default no-op
	// so platforms without an error channel (Linux) are unaffected.
	virtual void SetErrorCallback(OnError cb) { (void)cb; }
};

MDNSAnnouncer *CreateMDNSAnnouncer();
MDNSBrowser *CreateMDNSBrowser();

}  // namespace LANSync
