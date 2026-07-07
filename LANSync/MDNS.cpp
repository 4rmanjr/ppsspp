#include "LANSync/MDNS.h"

namespace LANSync {

MDNSAnnouncer *CreateMDNSAnnouncer() {
#if defined(__linux__) && !defined(ANDROID)
	extern MDNSAnnouncer *CreateMDNSAnnouncerLinux();
	return CreateMDNSAnnouncerLinux();
#elif defined(ANDROID)
	extern MDNSAnnouncer *CreateMDNSAnnouncerAndroid();
	return CreateMDNSAnnouncerAndroid();
#else
	return nullptr;
#endif
}

MDNSBrowser *CreateMDNSBrowser() {
#if defined(__linux__) && !defined(ANDROID)
	extern MDNSBrowser *CreateMDNSBrowserLinux();
	return CreateMDNSBrowserLinux();
#elif defined(ANDROID)
	extern MDNSBrowser *CreateMDNSBrowserAndroid();
	return CreateMDNSBrowserAndroid();
#else
	return nullptr;
#endif
}

}  // namespace LANSync
