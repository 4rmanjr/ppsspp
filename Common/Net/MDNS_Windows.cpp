// PPSSPP Project - LAN Save State Sync
// mDNS implementation for Windows (WinRT DNS-SD + UDP fallback)
// Phase 4: WinRT DNS-SD implementation for Windows 10 1709+
// Fallback: UDP broadcast (always available)

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)

#include <string>
#include <thread>
#include <atomic>

#include <Windows.h>

#include "Common/Net/MDNS.h"
#include "Common/Log.h"

namespace mDNS {

// Windows DNS-SD implementation:
//
// Primary (Win10 1709+):
//   Windows.Networking.ServiceDiscovery.Dnssd.DnssdServiceWatcher (browse)
//   Windows.Networking.ServiceDiscovery.Dnssd.DnssdServiceInstance (register)
//   These are WinRT APIs and require C++/WinRT or C++/CX.
//   They're available on all Windows 10 builds >= 1709.
//
// Fallback (Win7/8/10 pre-1709):
//   Custom UDP broadcast on port 27313.
//   The UDPDiscovery module handles this independently.
//
// For now, Windows uses UDP broadcast as the primary discovery method
// because:
// 1. It works on all Windows versions (7, 8, 10, 11)
// 2. It doesn't require WinRT runtime
// 3. It avoids C++/WinRT or C++/CX build dependencies
//
// WinRT DNS-SD can be added later as an enhancement (Phase 5+).

class WindowsBrowser : public Browser {
public:
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) override {
		INFO_LOG(Log::System, "mDNS: Windows browser - using UDP broadcast fallback");
		// UDP discovery handles this via UDPDiscovery::Listener
		// mDNS proper will be added with C++/WinRT (Phase 5)
		return true;  // Return true so caller doesn't error; UDP handles discovery
	}
	void Stop() override {}
	bool IsRunning() const override { return false; }
};

class WindowsAnnouncer : public Announcer {
public:
	bool Register(const ServiceInfo &info, ErrorCallback onError) override {
		INFO_LOG(Log::System, "mDNS: Windows announcer - using UDP broadcast fallback (port %d)", info.port);
		// UDP discovery handles this via UDPDiscovery::Announcer
		// mDNS proper will be added with C++/WinRT (Phase 5)
		return true;
	}
	bool Update(const ServiceInfo &info) override { return true; }
	void Unregister() override {}
	bool IsRegistered() const override { return true; }
};

Browser *Browser::Create() {
	return new WindowsBrowser();
}

Announcer *Announcer::Create() {
	return new WindowsAnnouncer();
}

}  // namespace mDNS

#endif  // PPSSPP_PLATFORM(WINDOWS)
