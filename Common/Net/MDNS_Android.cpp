// PPSSPP Project - LAN Save State Sync
// mDNS implementation for Android
// Pass-through: actual NsdManager discovery is handled by AndroidLANSync via JNI.
// mDNS::Browser/Announcer on Android are no-ops — the JNI layer handles everything.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include "Common/Net/MDNS.h"
#include "Common/Log.h"

namespace mDNS {

// Android uses android.net.nsd.NsdManager directly via JNI (see AndroidLANSync.cpp).
// These are pass-through stubs — discovery is managed at a higher level.

class AndroidBrowser : public Browser {
public:
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) override {
		INFO_LOG(Log::System, "mDNS: Android NsdManager browser — handled by AndroidLANSync JNI");
		return true;  // Discovery is active via JNI NsdManager, not through mDNS
	}
	void Stop() override {}
	bool IsRunning() const override { return true; }
};

class AndroidAnnouncer : public Announcer {
public:
	bool Register(const ServiceInfo &info, ErrorCallback onError) override {
		INFO_LOG(Log::System, "mDNS: Android NsdManager announcer — handled by AndroidLANSync JNI");
		return true;  // Announcement is active via JNI NsdManager
	}
	bool Update(const ServiceInfo &info) override { return true; }
	void Unregister() override {}
	bool IsRegistered() const override { return true; }
};

Browser *Browser::Create() {
	return new AndroidBrowser();
}

Announcer *Announcer::Create() {
	return new AndroidAnnouncer();
}

}  // namespace mDNS

#endif  // PPSSPP_PLATFORM(ANDROID)
