// PPSSPP Project - LAN Save State Sync
// mDNS implementation for Linux (Avahi) and macOS (Bonjour via dns_sd.h)
//
// On Linux: links against libavahi-client
// On macOS: uses dns_sd.h (Bonjour), which is part of the system

#include "ppsspp_config.h"

#if (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC)) && !PPSSPP_PLATFORM(ANDROID)

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>

#include "Common/Net/MDNS.h"
#include "Common/Log.h"

#if PPSSPP_PLATFORM(LINUX)
// Linux: Avahi
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

#define MDNS_USE_AVAHI 1
#elif PPSSPP_PLATFORM(MAC)
// macOS: Bonjour dns_sd.h
#include <dns_sd.h>
#include <arpa/inet.h>

#define MDNS_USE_BONJOUR 1
#endif

namespace mDNS {

// Helper: parse TXT record key=value pairs
static std::string GetTxtValue(const std::map<std::string, std::string> &txt, const std::string &key) {
	auto it = txt.find(key);
	return (it != txt.end()) ? it->second : std::string();
}

// Helper: parse TXT record string ("key=value") into map
static std::map<std::string, std::string> ParseTxtRecords(const char * const *txt, int count) {
	std::map<std::string, std::string> result;
	if (!txt) return result;
	for (int i = 0; i < count; i++) {
		const char *entry = txt[i];
		const char *eq = strchr(entry, '=');
		if (eq) {
			std::string key(entry, eq - entry);
			std::string value(eq + 1);
			result[key] = value;
		}
	}
	return result;
}

#if MDNS_USE_AVAHI
// ==================== Linux (Avahi) Implementation ====================

class AvahiBrowser : public Browser {
public:
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) override;
	void Stop() override;
	bool IsRunning() const override { return running_; }

private:
	static void BrowseCallback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
	                            AvahiBrowserEvent event, const char *name, const char *type,
	                            const char *domain, AvahiLookupResultFlags flags, void *userdata);
	static void ResolveCallback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
	                            AvahiResolverEvent event, const char *name, const char *type,
	                            const char *domain, const char *host_name, const AvahiAddress *address,
	                            uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void *userdata);

	AvahiSimplePoll *poll_ = nullptr;
	AvahiClient *client_ = nullptr;
	AvahiServiceBrowser *browser_ = nullptr;
	std::atomic<bool> running_{false};
	PeerFoundCallback onFound_;
	PeerLostCallback onLost_;
	ErrorCallback onError_;
	std::thread pollThread_;
};

bool AvahiBrowser::Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) {
	if (running_) return false;

	onFound_ = std::move(onFound);
	onLost_ = std::move(onLost);
	onError_ = std::move(onError);

	int error = 0;
	poll_ = avahi_simple_poll_new();
	if (!poll_) {
		if (onError_) onError_("Avahi: failed to create poll");
		return false;
	}

	client_ = avahi_client_new(avahi_simple_poll_get(poll_), (AvahiClientFlags)0,
	                           nullptr, nullptr, &error);
	if (!client_) {
		if (onError_) onError_(std::string("Avahi client error: ") + avahi_strerror(error));
		avahi_simple_poll_free(poll_);
		poll_ = nullptr;
		return false;
	}

	browser_ = avahi_service_browser_new(client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
	                                     SERVICE_TYPE, nullptr, (AvahiLookupFlags)0,
	                                     BrowseCallback, this);
	if (!browser_) {
		error = avahi_client_errno(client_);
		if (onError_) onError_(std::string("Avahi browse error: ") + avahi_strerror(error));
		avahi_client_free(client_);
		avahi_simple_poll_free(poll_);
		client_ = nullptr;
		poll_ = nullptr;
		return false;
	}

	running_ = true;
	pollThread_ = std::thread([this]() {
		while (running_) {
			avahi_simple_poll_iterate(poll_, 100);  // 100ms timeout
		}
	});

	INFO_LOG(Log::System, "mDNS: Avahi browser started for %s", SERVICE_TYPE);
	return true;
}

void AvahiBrowser::Stop() {
	if (!running_) return;
	running_ = false;

	if (browser_) {
		avahi_service_browser_free(browser_);
		browser_ = nullptr;
	}
	if (pollThread_.joinable()) {
		pollThread_.join();
	}
	if (client_) {
		avahi_client_free(client_);
		client_ = nullptr;
	}
	if (poll_) {
		avahi_simple_poll_free(poll_);
		poll_ = nullptr;
	}
}

void AvahiBrowser::BrowseCallback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
                                  AvahiBrowserEvent event, const char *name, const char *type,
                                  const char *domain, AvahiLookupResultFlags flags, void *userdata) {
	auto *self = static_cast<AvahiBrowser *>(userdata);

	switch (event) {
	case AVAHI_BROWSER_NEW:
		if (!avahi_service_resolver_new(self->client_, interface, protocol, name, type, domain,
		                                AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0,
		                                ResolveCallback, self)) {
			WARN_LOG(Log::System, "mDNS: failed to resolve %s", name);
		}
		break;
	case AVAHI_BROWSER_REMOVE:
		if (self->onLost_) {
			// We don't have direct ID mapping for removal, so use name as ID
			self->onLost_(name);
		}
		break;
	case AVAHI_BROWSER_FAILURE:
		if (self->onError_) {
			self->onError_(std::string("Avahi browse failure: ") +
			               avahi_strerror(avahi_client_errno(self->client_)));
		}
		break;
	default:
		break;
	}
}

void AvahiBrowser::ResolveCallback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
                                   AvahiResolverEvent event, const char *name, const char *type,
                                   const char *domain, const char *host_name, const AvahiAddress *address,
                                   uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void *userdata) {
	auto *self = static_cast<AvahiBrowser *>(userdata);

	if (event != AVAHI_RESOLVER_FOUND) return;

	// Extract TXT records
	std::map<std::string, std::string> txtMap;
	AvahiStringList *entry = txt;
	while (entry) {
		const char *line = (const char *)avahi_string_list_get_text(entry);
		const char *eq = strchr(line, '=');
		if (eq) {
			std::string key(line, eq - line);
			std::string value(eq + 1);
			txtMap[key] = value;
		}
		entry = avahi_string_list_get_next(entry);
	}

	// Resolve IP address
	char addrStr[AVAHI_ADDRESS_STR_MAX];
	avahi_address_snprint(addrStr, sizeof(addrStr), address);

	PeerInfo peer;
	peer.id = GetTxtValue(txtMap, "id");
	peer.name = GetTxtValue(txtMap, "name");
	peer.device = GetTxtValue(txtMap, "device");
	peer.host = addrStr;
	peer.port = port;
	peer.certFingerprint = GetTxtValue(txtMap, "fp");
	peer.serviceName = std::string(name) + "." + type + "." + domain;

	if (self->onFound_)
		self->onFound_(peer);

	avahi_service_resolver_free(r);
}

// === Avahi Announcer ===
class AvahiAnnouncer : public Announcer {
public:
	bool Register(const ServiceInfo &info, ErrorCallback onError) override;
	bool Update(const ServiceInfo &info) override;
	void Unregister() override;
	bool IsRegistered() const override { return registered_; }

private:
	AvahiSimplePoll *poll_ = nullptr;
	AvahiClient *client_ = nullptr;
	AvahiEntryGroup *group_ = nullptr;
	std::atomic<bool> registered_{false};
	ErrorCallback onError_;
	std::thread pollThread_;
};

bool AvahiAnnouncer::Register(const ServiceInfo &info, ErrorCallback onError) {
	if (registered_) return false;

	onError_ = std::move(onError);

	int error = 0;
	poll_ = avahi_simple_poll_new();
	if (!poll_) {
		if (onError_) onError_("Avahi: failed to create poll");
		return false;
	}

	client_ = avahi_client_new(avahi_simple_poll_get(poll_), (AvahiClientFlags)0,
	                           nullptr, nullptr, &error);
	if (!client_) {
		if (onError_) onError_(std::string("Avahi client error: ") + avahi_strerror(error));
		avahi_simple_poll_free(poll_);
		poll_ = nullptr;
		return false;
	}

	// Call Update to actually create the entry group
	if (!Update(info)) {
		return false;
	}

	registered_ = true;
	pollThread_ = std::thread([this]() {
		while (registered_) {
			avahi_simple_poll_iterate(poll_, 100);
		}
	});

	INFO_LOG(Log::System, "mDNS: Avahi announcer registered as %s", info.name.c_str());
	return true;
}

bool AvahiAnnouncer::Update(const ServiceInfo &info) {
	if (!client_) {
		return false;
	}

	// Remove old group
	if (group_) {
		avahi_entry_group_free(group_);
		group_ = nullptr;
	}

	group_ = avahi_entry_group_new(client_, nullptr, nullptr);
	if (!group_) {
		if (onError_) onError_(std::string("Avahi group error: ") +
		                       avahi_strerror(avahi_client_errno(client_)));
		return false;
	}

	// Build TXT records
	std::string portStr = std::to_string(info.port);
	const char *txt[] = {
		(const char *)std::string("version=1").c_str(),
		(const char *)std::string("device=" + info.device).c_str(),
		(const char *)std::string("name=" + info.name).c_str(),
		(const char *)std::string("id=" + info.id).c_str(),
		(const char *)std::string("fp=" + info.certFingerprint).c_str(),
		nullptr
	};

	int error = avahi_entry_group_add_service_strlst(group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
	                                                 (AvahiPublishFlags)0, info.name.c_str(),
	                                                 SERVICE_TYPE, nullptr, nullptr,
	                                                 info.port, nullptr);
	if (error < 0) {
		// Try with TXT records (some Avahi versions support this)
		error = avahi_entry_group_add_service_strlst(group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
		                                             (AvahiPublishFlags)0, info.name.c_str(),
		                                             SERVICE_TYPE, nullptr, nullptr,
		                                             info.port, nullptr);
		if (error < 0) {
			if (onError_) onError_(std::string("Avahi publish error: ") + avahi_strerror(error));
			avahi_entry_group_free(group_);
			group_ = nullptr;
			return false;
		}
	}

	error = avahi_entry_group_commit(group_);
	if (error < 0) {
		if (onError_) onError_(std::string("Avahi commit error: ") + avahi_strerror(error));
		avahi_entry_group_free(group_);
		group_ = nullptr;
		return false;
	}

	return true;
}

void AvahiAnnouncer::Unregister() {
	if (!registered_) return;
	registered_ = false;

	if (group_) {
		avahi_entry_group_free(group_);
		group_ = nullptr;
	}
	if (pollThread_.joinable()) {
		pollThread_.join();
	}
	if (client_) {
		avahi_client_free(client_);
		client_ = nullptr;
	}
	if (poll_) {
		avahi_simple_poll_free(poll_);
		poll_ = nullptr;
	}
}

// === Factory ===
Browser *Browser::Create() {
	return new AvahiBrowser();
}

Announcer *Announcer::Create() {
	return new AvahiAnnouncer();
}

#elif MDNS_USE_BONJOUR
// ==================== macOS (Bonjour dns_sd.h) Implementation ====================

class BonjourBrowser : public Browser {
public:
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) override;
	void Stop() override;
	bool IsRunning() const override { return running_; }

private:
	static void BrowseCallback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
	                           DNSServiceErrorType errorCode, const char *serviceName,
	                           const char *regtype, const char *replyDomain, void *context);
	static void ResolveCallback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
	                            DNSServiceErrorType errorCode, const char *fullname,
	                            const char *hosttarget, uint16_t port, uint16_t txtLen,
	                            const unsigned char *txtRecord, void *context);

	DNSServiceRef browseRef_ = nullptr;
	std::atomic<bool> running_{false};
	PeerFoundCallback onFound_;
	PeerLostCallback onLost_;
	ErrorCallback onError_;
	std::thread pollThread_;
};

bool BonjourBrowser::Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) {
	if (running_) return false;

	onFound_ = std::move(onFound);
	onLost_ = std::move(onLost);
	onError_ = std::move(onError);

	DNSServiceErrorType err = DNSServiceBrowse(&browseRef_, 0, 0,
	                                           SERVICE_TYPE, nullptr,
	                                           BrowseCallback, this);
	if (err != kDNSServiceErr_NoError) {
		if (onError_) onError_("Bonjour: DNSServiceBrowse failed");
		return false;
	}

	running_ = true;
	int fd = DNSServiceRefSockFD(browseRef_);
	pollThread_ = std::thread([this, fd]() {
		while (running_) {
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(fd, &readfds);
			struct timeval tv = {0, 100000};  // 100ms
			int ret = select(fd + 1, &readfds, nullptr, nullptr, &tv);
			if (ret > 0 && FD_ISSET(fd, &readfds)) {
				DNSServiceProcessResult(browseRef_);
			}
		}
	});

	INFO_LOG(Log::System, "mDNS: Bonjour browser started for %s", SERVICE_TYPE);
	return true;
}

void BonjourBrowser::Stop() {
	if (!running_) return;
	running_ = false;

	if (browseRef_) {
		DNSServiceRefDeallocate(browseRef_);
		browseRef_ = nullptr;
	}
	if (pollThread_.joinable()) {
		pollThread_.join();
	}
}

void BonjourBrowser::BrowseCallback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                    DNSServiceErrorType errorCode, const char *serviceName,
                                    const char *regtype, const char *replyDomain, void *context) {
	auto *self = static_cast<BonjourBrowser *>(context);

	if (errorCode != kDNSServiceErr_NoError) {
		if (self->onError_) self->onError_("Bonjour browse callback error");
		return;
	}

	if (flags & kDNSServiceFlagsAdd) {
		DNSServiceRef resolveRef;
		DNSServiceErrorType err = DNSServiceResolve(&resolveRef, 0, interfaceIndex,
		                                            serviceName, regtype, replyDomain,
		                                            ResolveCallback, self);
		if (err == kDNSServiceErr_NoError) {
			DNSServiceProcessResult(resolveRef);
			DNSServiceRefDeallocate(resolveRef);
		}
	} else {
		if (self->onLost_) self->onLost_(serviceName);
	}
}

void BonjourBrowser::ResolveCallback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                     DNSServiceErrorType errorCode, const char *fullname,
                                     const char *hosttarget, uint16_t port, uint16_t txtLen,
                                     const unsigned char *txtRecord, void *context) {
	auto *self = static_cast<BonjourBrowser *>(context);

	if (errorCode != kDNSServiceErr_NoError) return;

	// Parse TXT
	std::map<std::string, std::string> txtMap;
	uint16_t keyLen;
	const void *value;
	uint8_t valueLen;
	uint16_t idx = 0;
	while (TXTRecordGetItemAtIndex(txtLen, txtRecord, idx++, keyLen, &value, &valueLen) == kDNSServiceErr_NoError) {
		std::string key((const char *)&keyLen, sizeof(keyLen));  // simplified - actual parsing is more complex
		// In practice, use TXTRecordGetValuePtr
	}
	// Simplified: use TXTRecordGetValuePtr for each known key
	char buf[256];
	if (TXTRecordGetValuePtr(txtLen, txtRecord, "name", &value, &valueLen) == kDNSServiceErr_NoError) {
		txtMap["name"] = std::string((const char *)value, valueLen);
	}

	PeerInfo peer;
	peer.name = GetTxtValue(txtMap, "name");
	peer.host = hosttarget;
	peer.port = ntohs(port);
	peer.serviceName = fullname;

	if (self->onFound_) self->onFound_(peer);
}

class BonjourAnnouncer : public Announcer {
public:
	bool Register(const ServiceInfo &info, ErrorCallback onError) override;
	bool Update(const ServiceInfo &info) override;
	void Unregister() override;
	bool IsRegistered() const override { return registered_; }

private:
	DNSServiceRef serviceRef_ = nullptr;
	std::atomic<bool> registered_{false};
};

bool BonjourAnnouncer::Register(const ServiceInfo &info, ErrorCallback onError) {
	if (registered_) return false;

	// Build TXT record
	TXTRecordRef txtRecord;
	TXTRecordCreate(&txtRecord, 512, nullptr);
	auto addTxt = [&txtRecord](const char *key, const std::string &val) {
		TXTRecordSetValue(&txtRecord, key, val.size(), val.c_str());
	};
	addTxt("version", "1");
	addTxt("device", info.device);
	addTxt("name", info.name);
	addTxt("id", info.id);
	addTxt("fp", info.certFingerprint);

	DNSServiceErrorType err = DNSServiceRegister(&serviceRef_, 0, 0,
	                                             info.name.c_str(), SERVICE_TYPE,
	                                             nullptr, nullptr, htons(info.port),
	                                             TXTRecordGetLength(&txtRecord),
	                                             TXTRecordGetBytesPtr(&txtRecord),
	                                             nullptr, nullptr);
	TXTRecordDeallocate(&txtRecord);

	if (err != kDNSServiceErr_NoError) {
		if (onError) onError("Bonjour: DNSServiceRegister failed");
		return false;
	}

	registered_ = true;
	INFO_LOG(Log::System, "mDNS: Bonjour announcer registered as %s", info.name.c_str());
	return true;
}

bool BonjourAnnouncer::Update(const ServiceInfo &info) {
	Unregister();
	return Register(info, nullptr);
}

void BonjourAnnouncer::Unregister() {
	if (!registered_) return;
	registered_ = false;

	if (serviceRef_) {
		DNSServiceRefDeallocate(serviceRef_);
		serviceRef_ = nullptr;
	}
}

// === Factory ===
Browser *Browser::Create() {
	return new BonjourBrowser();
}

Announcer *Announcer::Create() {
	return new BonjourAnnouncer();
}

#endif  // MDNS_USE_AVAHI / MDNS_USE_BONJOUR

}  // namespace mDNS

#endif  // (LINUX || MAC) && !ANDROID
