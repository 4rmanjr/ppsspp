// PPSSPP Project - LAN Save State Sync
// mDNS/Bonjour peer discovery for local network
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

namespace mDNS {

// M-DNS-SD service type for PPSSPP LAN sync discovery
constexpr const char *SERVICE_TYPE = "_ppsspp-sync._tcp.local.";

// Information about a discovered peer
struct PeerInfo {
	std::string id;
	std::string name;
	std::string device;       // "PC" or "Android"
	std::string host;         // IP address
	int port = 0;
	std::string certFingerprint;  // SHA-256 fingerprint for TOFU
	std::string serviceName;      // Full instance name: "PPSSPP-MyPC._ppsspp-sync._tcp.local."
};

// Information we advertise about ourselves
struct ServiceInfo {
	std::string id;
	std::string name;
	std::string device;       // "PC" or "Android"
	int port = 0;
	std::string certFingerprint;  // SHA-256 fingerprint for TOFU
};

// Callbacks for discovery events
using PeerFoundCallback = std::function<void(const PeerInfo &peer)>;
using PeerLostCallback = std::function<void(const std::string &id)>;
using ErrorCallback = std::function<void(const std::string &error)>;

// Browser: discovers remote PPSSPP instances on the local network
class Browser {
public:
	virtual ~Browser() = default;

	// Start browsing for _ppsspp-sync._tcp.local. services
	virtual bool Start(PeerFoundCallback onFound, PeerLostCallback onLost, ErrorCallback onError) = 0;
	// Stop browsing
	virtual void Stop() = 0;
	// Returns true if currently browsing
	virtual bool IsRunning() const = 0;

	// Factory: creates platform-appropriate browser
	static Browser *Create();
};

// Announcer: registers this PPSSPP instance on the local network
class Announcer {
public:
	virtual ~Announcer() = default;

	// Register our service on the network
	virtual bool Register(const ServiceInfo &info, ErrorCallback onError) = 0;
	// Update TXT records (e.g., if port changes)
	virtual bool Update(const ServiceInfo &info) = 0;
	// Unregister our service
	virtual void Unregister() = 0;
	// Returns true if currently announced
	virtual bool IsRegistered() const = 0;

	// Factory: creates platform-appropriate announcer
	static Announcer *Create();
};

}  // namespace mDNS
