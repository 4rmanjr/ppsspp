// PPSSPP Project - LAN Save State Sync
// UDP broadcast-based peer discovery (fallback when mDNS is blocked)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <ctime>

namespace UDPDiscovery {

constexpr int ANNOUNCE_INTERVAL_MS = 10000;  // 10 seconds
constexpr int PEER_TIMEOUT_S = 30;           // Remove peer after 30s silence
constexpr int BASE_DISCOVERY_PORT = 27313;
constexpr int MAX_DISCOVERY_PORT = 27320;
constexpr int DISCOVERY_PORT_RANGE = MAX_DISCOVERY_PORT - BASE_DISCOVERY_PORT + 1;

// Same PeerInfo structure used by mDNS
struct PeerInfo {
	std::string id;
	std::string name;
	std::string device;       // "PC" or "Android"
	std::string address;      // IP:port
	std::string host;         // IP
	int port = 0;
	std::string certFingerprint;
	time_t lastSeen = 0;
};

using PeerFoundCallback = std::function<void(const PeerInfo &peer)>;
using PeerLostCallback = std::function<void(const std::string &id)>;
using ErrorCallback = std::function<void(const std::string &error)>;

// Advertises this PPSSPP instance via UDP broadcast
class Announcer {
public:
	~Announcer();

	// Start broadcasting our presence. Returns the port we're listening on.
	bool Start(const PeerInfo &info, ErrorCallback onError);

	// Stop broadcasting
	void Stop();

	bool IsRunning() const { return listenPort_ > 0; }

	// Returns the port used for listening (auto-assigned if 0 was given)
	int Port() const { return listenPort_; }

private:
	void AnnounceLoop();

	std::string peerId_;
	std::string peerName_;
	std::string peerDevice_;
	std::string peerFingerprint_;
	int peerPort_ = 0;
	int listenPort_ = 0;
	bool running_ = false;
	ErrorCallback onError_;
	// Thread handle managed internally
};

// Listens for UDP broadcast announcements from other PPSSPP instances
class Listener {
public:
	~Listener();

	// Start listening for broadcasts
	bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError);

	// Stop listening
	void Stop();

	bool IsRunning() const { return running_; }

	// Get currently discovered peers
	std::vector<PeerInfo> GetPeers() const;

private:
	void ListenLoop();

	PeerFoundCallback onFound_;
	PeerLostCallback onLost_;
	ErrorCallback onError_;
	bool running_ = false;
	// Listens on all ports in DISCOVERY_PORT_RANGE simultaneously
};

}  // namespace UDPDiscovery
