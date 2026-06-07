// PPSSPP Project - LAN Save State Sync
// mDNS generic stub (for platforms without native mDNS support)

#include "ppsspp_config.h"

#include "Common/Net/MDNS.h"
#include "Common/Log.h"

// Fallback stub implementations for platforms without mDNS
// These are used when the platform-specific file is not compiled

#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(MAC)

namespace mDNS {

// === Null Browser ===
class NullBrowser : public Browser {
public:
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) override {
		WARN_LOG(Log::System, "mDNS: browsing not supported on this platform");
		if (onError)
			onError("mDNS not available on this platform");
		return false;
	}
	void Stop() override {}
	bool IsRunning() const override { return false; }
};

// === Null Announcer ===
class NullAnnouncer : public Announcer {
public:
	bool Register(const ServiceInfo &info, ErrorCallback onError) override {
		WARN_LOG(Log::System, "mDNS: announcing not supported on this platform");
		if (onError)
			onError("mDNS not available on this platform");
		return false;
	}
	bool Update(const ServiceInfo &info) override { return false; }
	void Unregister() override {}
	bool IsRegistered() const override { return false; }
};

Browser *Browser::Create() {
	return new NullBrowser();
}

Announcer *Announcer::Create() {
	return new NullAnnouncer();
}

}  // namespace mDNS

#endif  // !supported platforms
