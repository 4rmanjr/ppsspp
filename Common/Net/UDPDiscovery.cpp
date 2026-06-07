// PPSSPP Project - LAN Save State Sync
// UDP broadcast discovery implementation

#include "ppsspp_config.h"

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <vector>
#include <map>
#include <chrono>

#include "Common/Net/UDPDiscovery.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/Parsers.h"  // For JSON-like parsing

#if PPSSPP_PLATFORM(WINDOWS)
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace UDPDiscovery {

static const char *DISCOVERY_MAGIC = "ppsspp-sync";
static const int DISCOVERY_VERSION = 1;

// Simple JSON construction/parsing (no external JSON library dependency)
// We use a minimal format: key:value pairs separated by newlines

static std::string BuildPayload(const PeerInfo &info) {
	std::string payload;
	payload += "type:" + std::string(DISCOVERY_MAGIC) + "\n";
	payload += "version:" + std::to_string(DISCOVERY_VERSION) + "\n";
	payload += "device:" + info.device + "\n";
	payload += "name:" + info.name + "\n";
	payload += "port:" + std::to_string(info.port) + "\n";
	payload += "fp:" + info.certFingerprint + "\n";
	payload += "id:" + info.id + "\n";
	return payload;
}

static bool ParsePayload(const std::string &data, PeerInfo &out) {
	out = {};
	out.lastSeen = time(nullptr);

	// Simple key:value parser
	size_t pos = 0;
	while (pos < data.size()) {
		size_t nl = data.find('\n', pos);
		if (nl == std::string::npos) nl = data.size();
		std::string line = data.substr(pos, nl - pos);
		pos = nl + 1;

		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;

		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);

		if (key == "type" && value != DISCOVERY_MAGIC) return false;
		if (key == "device") out.device = value;
		if (key == "name") out.name = value;
		if (key == "port") out.port = atoi(value.c_str());
		if (key == "fp") out.certFingerprint = value;
		if (key == "id") out.id = value;
	}
	return !out.id.empty();
}

// === Announcer ===

Announcer::~Announcer() {
	Stop();
}

bool Announcer::Start(const PeerInfo &info, ErrorCallback onError) {
	if (running_) return false;

	peerId_ = info.id;
	onError_ = std::move(onError);

	// Try ports BASE_DISCOVERY_PORT through MAX_DISCOVERY_PORT
	for (int port = BASE_DISCOVERY_PORT; port <= MAX_DISCOVERY_PORT; port++) {
		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) continue;

		// Set SO_REUSEADDR so multiple listeners can coexist
		int reuse = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

		// Allow broadcasting
		int broadcast = 1;
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port);

		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			listenPort_ = port;
			// Keep socket for broadcasting
			INFO_LOG(Log::System, "UDP discovery announcer bound to port %d", port);
			break;
		}
		closesocket(sock);
	}

	if (listenPort_ == 0) {
		if (onError_) onError_("UDP discovery: all ports in use (27313-27320)");
		return false;
	}

	running_ = true;
	// Launch announce loop
	std::thread([this]() { AnnounceLoop(); }).detach();

	return true;
}

void Announcer::Stop() {
	running_ = false;
	listenPort_ = 0;
}

void Announcer::AnnounceLoop() {
	PeerInfo payloadInfo;
	payloadInfo.id = peerId_;
	// Note: selfInfo_ would be set externally in full implementation

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) return;

	int broadcast = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));

	struct sockaddr_in broadcastAddr;
	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_port = htons(BASE_DISCOVERY_PORT);

	while (running_) {
		// Build payload (could be refreshed each iteration)
		std::string payload = BuildPayload(payloadInfo);

		// Send to 255.255.255.255
		broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(sock, payload.c_str(), payload.size(), 0,
		       (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));

		// Also send to subnet broadcast (192.168.x.255)
		// This is best-effort; 255.255.255.255 covers most cases

		std::this_thread::sleep_for(std::chrono::milliseconds(ANNOUNCE_INTERVAL_MS));
	}

	closesocket(sock);
}

// === Listener ===

Listener::~Listener() {
	Stop();
}

bool Listener::Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) {
	if (running_) return false;

	onFound_ = std::move(onFound);
	onLost_ = std::move(onLost);
	onError_ = std::move(onError);
	running_ = true;

	std::thread([this]() { ListenLoop(); }).detach();

	INFO_LOG(Log::System, "UDP discovery listener started");
	return true;
}

void Listener::Stop() {
	running_ = false;
}

void Listener::ListenLoop() {
	// Listen on all discovery ports
	const int MAX_SOCKS = DISCOVERY_PORT_RANGE;
	int socks[MAX_SOCKS];
	int sockCount = 0;

	for (int i = 0; i < DISCOVERY_PORT_RANGE; i++) {
		int port = BASE_DISCOVERY_PORT + i;
		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) continue;

		int reuse = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port);

		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			socks[sockCount++] = sock;
		} else {
			closesocket(sock);
		}
	}

	if (sockCount == 0) {
		if (onError_) onError_("UDP discovery: cannot bind any listener port");
		return;
	}

	// Track known peers with expiration
	std::map<std::string, time_t> knownPeers;  // id -> lastSeen
	std::mutex peerMutex;

	while (running_) {
		// Use select to wait on all sockets with 1 second timeout
		fd_set readfds;
		FD_ZERO(&readfds);
		int maxfd = 0;
		for (int i = 0; i < sockCount; i++) {
			FD_SET(socks[i], &readfds);
			if (socks[i] > maxfd) maxfd = socks[i];
		}

		struct timeval tv = {1, 0};  // 1 second timeout
		int ret = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);

		if (ret > 0) {
			for (int i = 0; i < sockCount; i++) {
				if (!FD_ISSET(socks[i], &readfds)) continue;

				char buf[2048];
				struct sockaddr_in senderAddr;
				socklen_t senderLen = sizeof(senderAddr);
				int bytes = recvfrom(socks[i], buf, sizeof(buf) - 1, 0,
				                    (struct sockaddr *)&senderAddr, &senderLen);
				if (bytes > 0) {
					buf[bytes] = '\0';
					PeerInfo peer;
					if (ParsePayload(std::string(buf, bytes), peer)) {
						peer.address = std::string(inet_ntoa(senderAddr.sin_addr)) +
						               ":" + std::to_string(peer.port);
						peer.host = inet_ntoa(senderAddr.sin_addr);

						std::lock_guard<std::mutex> lock(peerMutex);
						auto it = knownPeers.find(peer.id);
						if (it == knownPeers.end()) {
							// New peer
							knownPeers[peer.id] = time(nullptr);
							if (onFound_) onFound_(peer);
						} else {
							// Update last seen
							it->second = time(nullptr);
						}
					}
				}
			}
		}

		// Check for expired peers
		{
			std::lock_guard<std::mutex> lock(peerMutex);
			time_t now = time(nullptr);
			auto it = knownPeers.begin();
			while (it != knownPeers.end()) {
				if (now - it->second > PEER_TIMEOUT_S) {
					if (onLost_) onLost_(it->first);
					it = knownPeers.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	// Cleanup
	for (int i = 0; i < sockCount; i++) {
		closesocket(socks[i]);
	}
}

// GetPeers is simplified here; full implementation would cache PeerInfo
std::vector<PeerInfo> Listener::GetPeers() const {
	return {};  // Full implementation tracks Peers in knownPeerMap_
}

}  // namespace UDPDiscovery
