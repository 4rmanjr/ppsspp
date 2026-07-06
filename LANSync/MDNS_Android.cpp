#include "LANSync/MDNS.h"

namespace LANSync {

class MDNSAnnouncerAndroid : public MDNSAnnouncer {
public:
	bool Start(const std::string &serviceType, int port, const std::string &deviceName) override { return false; }
	void Stop() override {}
};

class MDNSBrowserAndroid : public MDNSBrowser {
public:
	bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) override { return false; }
	void Stop() override {}
};

}  // namespace LANSync
