#include "LANSync/MDNS.h"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/strlst.h>

#include <thread>
#include <atomic>
#include <string>
#include <cstring>
#include <map>

#include "Common/Log.h"

namespace LANSync {

// ==================== MDNSAnnouncerLinux ====================

class MDNSAnnouncerLinux : public MDNSAnnouncer {
public:
	MDNSAnnouncerLinux() = default;
	~MDNSAnnouncerLinux() override { Stop(); }

	bool Start(const std::string &serviceType, int port, const std::string &deviceName) override;
	void Stop() override;

private:
	void RunLoop();

	static void ClientCallback(AvahiClient *c, AvahiClientState state, void *userdata);
	static void EntryGroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata);

	AvahiSimplePoll *simplePoll_ = nullptr;
	AvahiClient *client_ = nullptr;
	AvahiEntryGroup *group_ = nullptr;
	std::string serviceType_;
	int port_ = 0;
	std::string deviceName_;
	std::atomic<bool> running_{false};
	std::thread thread_;
};

void MDNSAnnouncerLinux::ClientCallback(AvahiClient *c, AvahiClientState state, void *userdata) {
	auto *self = static_cast<MDNSAnnouncerLinux *>(userdata);
	if (state == AVAHI_CLIENT_FAILURE) {
		WARN_LOG(Log::System, "mDNS: Avahi client failure: %s", avahi_strerror(avahi_client_errno(c)));
		avahi_simple_poll_quit(self->simplePoll_);
	}
}

void MDNSAnnouncerLinux::EntryGroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
	if (state == AVAHI_ENTRY_GROUP_FAILURE) {
		auto *self = static_cast<MDNSAnnouncerLinux *>(userdata);
		WARN_LOG(Log::System, "mDNS: Avahi entry group failure: %s",
		         avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
		avahi_simple_poll_quit(self->simplePoll_);
	}
}

bool MDNSAnnouncerLinux::Start(const std::string &serviceType, int port, const std::string &deviceName) {
	if (running_) return false;

	serviceType_ = serviceType;
	port_ = port;
	deviceName_ = deviceName;

	simplePoll_ = avahi_simple_poll_new();
	if (!simplePoll_) {
		ERROR_LOG(Log::System, "mDNS: failed to create avahi simple poll");
		return false;
	}

	int error = 0;
	client_ = avahi_client_new(avahi_simple_poll_get(simplePoll_), (AvahiClientFlags)0,
	                           ClientCallback, this, &error);
	if (!client_) {
		ERROR_LOG(Log::System, "mDNS: avahi client error: %s", avahi_strerror(error));
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	group_ = avahi_entry_group_new(client_, EntryGroupCallback, this);
	if (!group_) {
		ERROR_LOG(Log::System, "mDNS: avahi entry group error: %s",
		          avahi_strerror(avahi_client_errno(client_)));
		avahi_client_free(client_);
		client_ = nullptr;
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	// Build TXT records
	AvahiStringList *txt = nullptr;
	txt = avahi_string_list_add_pair(txt, "device", deviceName.c_str());

	int ret = avahi_entry_group_add_service_strlst(group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
	                                               (AvahiPublishFlags)0, deviceName.c_str(),
	                                               serviceType.c_str(), nullptr, nullptr,
	                                               port_, txt);
	avahi_string_list_free(txt);

	if (ret < 0) {
		ERROR_LOG(Log::System, "mDNS: avahi add service error: %s", avahi_strerror(ret));
		avahi_entry_group_free(group_);
		group_ = nullptr;
		avahi_client_free(client_);
		client_ = nullptr;
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	ret = avahi_entry_group_commit(group_);
	if (ret < 0) {
		ERROR_LOG(Log::System, "mDNS: avahi commit error: %s", avahi_strerror(ret));
		avahi_entry_group_free(group_);
		group_ = nullptr;
		avahi_client_free(client_);
		client_ = nullptr;
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	running_ = true;
	thread_ = std::thread(&MDNSAnnouncerLinux::RunLoop, this);
	INFO_LOG(Log::System, "mDNS: announcer started for %s on port %d", serviceType.c_str(), port_);
	return true;
}

void MDNSAnnouncerLinux::Stop() {
	if (!running_) return;
	running_ = false;

	if (simplePoll_)
		avahi_simple_poll_quit(simplePoll_);

	if (thread_.joinable())
		thread_.join();

	if (group_) {
		avahi_entry_group_free(group_);
		group_ = nullptr;
	}
	if (client_) {
		avahi_client_free(client_);
		client_ = nullptr;
	}
	if (simplePoll_) {
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
	}
}

void MDNSAnnouncerLinux::RunLoop() {
	while (running_) {
		avahi_simple_poll_iterate(simplePoll_, 100);
	}
}

// ==================== MDNSBrowserLinux ====================

class MDNSBrowserLinux : public MDNSBrowser {
public:
	MDNSBrowserLinux() = default;
	~MDNSBrowserLinux() override { Stop(); }

	bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) override;
	void Stop() override;

private:
	void RunLoop();

	static void BrowseCallback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
	                           AvahiBrowserEvent event, const char *name, const char *type,
	                           const char *domain, AvahiLookupResultFlags flags, void *userdata);
	static void ResolveCallback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
	                            AvahiResolverEvent event, const char *name, const char *type,
	                            const char *domain, const char *host_name, const AvahiAddress *address,
	                            uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags,
	                            void *userdata);

	AvahiSimplePoll *simplePoll_ = nullptr;
	AvahiClient *client_ = nullptr;
	AvahiServiceBrowser *browser_ = nullptr;
	std::string serviceType_;
	std::atomic<bool> running_{false};
	OnPeerFound onFound_;
	OnPeerLost onLost_;
	std::thread thread_;
};

void MDNSBrowserLinux::BrowseCallback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
                                      AvahiBrowserEvent event, const char *name, const char *type,
                                      const char *domain, AvahiLookupResultFlags flags, void *userdata) {
	auto *self = static_cast<MDNSBrowserLinux *>(userdata);

	switch (event) {
	case AVAHI_BROWSER_NEW:
		if (!avahi_service_resolver_new(self->client_, interface, protocol, name, type, domain,
		                                AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0,
		                                ResolveCallback, self)) {
			WARN_LOG(Log::System, "mDNS: failed to resolve %s", name);
		}
		break;
	case AVAHI_BROWSER_REMOVE:
		if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
			break;
		if (self->onLost_) {
			DiscoveredPeer peer;
			peer.deviceName = name;
			self->onLost_(peer);
		}
		break;
	case AVAHI_BROWSER_FAILURE:
		WARN_LOG(Log::System, "mDNS: browse failure: %s",
		         avahi_strerror(avahi_client_errno(self->client_)));
		break;
	default:
		break;
	}
}

void MDNSBrowserLinux::ResolveCallback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
                                       AvahiResolverEvent event, const char *name, const char *type,
                                       const char *domain, const char *host_name, const AvahiAddress *address,
                                       uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags,
                                       void *userdata) {
	auto *self = static_cast<MDNSBrowserLinux *>(userdata);

	if (event != AVAHI_RESOLVER_FOUND) {
		avahi_service_resolver_free(r);
		return;
	}

	// Parse TXT records
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

	char addrStr[AVAHI_ADDRESS_STR_MAX];
	avahi_address_snprint(addrStr, sizeof(addrStr), address);

	DiscoveredPeer peer;
	peer.host = addrStr;
	peer.port = port;

	auto it = txtMap.find("device");
	if (it != txtMap.end())
		peer.deviceName = it->second;
	else
		peer.deviceName = name;

	if (flags & AVAHI_LOOKUP_RESULT_LOCAL) {
		avahi_service_resolver_free(r);
		return;
	}

	if (self->onFound_)
		self->onFound_(peer);

	avahi_service_resolver_free(r);
}

bool MDNSBrowserLinux::Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) {
	if (running_) return false;

	serviceType_ = serviceType;
	onFound_ = std::move(onFound);
	onLost_ = std::move(onLost);

	simplePoll_ = avahi_simple_poll_new();
	if (!simplePoll_) {
		ERROR_LOG(Log::System, "mDNS: failed to create avahi simple poll");
		return false;
	}

	int error = 0;
	client_ = avahi_client_new(avahi_simple_poll_get(simplePoll_), (AvahiClientFlags)0,
	                           nullptr, nullptr, &error);
	if (!client_) {
		ERROR_LOG(Log::System, "mDNS: avahi client error: %s", avahi_strerror(error));
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	browser_ = avahi_service_browser_new(client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
	                                     serviceType.c_str(), nullptr, (AvahiLookupFlags)0,
	                                     BrowseCallback, this);
	if (!browser_) {
		ERROR_LOG(Log::System, "mDNS: avahi browse error: %s",
		          avahi_strerror(avahi_client_errno(client_)));
		avahi_client_free(client_);
		client_ = nullptr;
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
		return false;
	}

	running_ = true;
	thread_ = std::thread(&MDNSBrowserLinux::RunLoop, this);
	INFO_LOG(Log::System, "mDNS: browser started for %s", serviceType.c_str());
	return true;
}

void MDNSBrowserLinux::Stop() {
	if (!running_) return;
	running_ = false;

	if (simplePoll_)
		avahi_simple_poll_quit(simplePoll_);

	if (thread_.joinable())
		thread_.join();

	if (browser_) {
		avahi_service_browser_free(browser_);
		browser_ = nullptr;
	}
	if (client_) {
		avahi_client_free(client_);
		client_ = nullptr;
	}
	if (simplePoll_) {
		avahi_simple_poll_free(simplePoll_);
		simplePoll_ = nullptr;
	}
}

void MDNSBrowserLinux::RunLoop() {
	while (running_) {
		avahi_simple_poll_iterate(simplePoll_, 100);
	}
}

// ==================== Factory functions ====================

MDNSAnnouncer *CreateMDNSAnnouncerLinux() {
	return new MDNSAnnouncerLinux();
}

MDNSBrowser *CreateMDNSBrowserLinux() {
	return new MDNSBrowserLinux();
}

}  // namespace LANSync
