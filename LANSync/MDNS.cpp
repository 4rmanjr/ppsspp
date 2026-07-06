#include "LANSync/MDNS.h"

namespace LANSync {

MDNSAnnouncer *CreateMDNSAnnouncer() {
#if defined(__linux__) && !defined(ANDROID)
	extern MDNSAnnouncer *CreateMDNSAnnouncerLinux();
	return CreateMDNSAnnouncerLinux();
#elif defined(ANDROID)
	return nullptr;
#else
	return nullptr;
#endif
}

MDNSBrowser *CreateMDNSBrowser() {
#if defined(__linux__) && !defined(ANDROID)
	extern MDNSBrowser *CreateMDNSBrowserLinux();
	return CreateMDNSBrowserLinux();
#elif defined(ANDROID)
	return nullptr;
#else
	return nullptr;
#endif
}

}  // namespace LANSync
