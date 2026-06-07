// PPSSPP Project - LAN Save State Sync
// Save State LAN Sync Manager - full implementation

#include "ppsspp_config.h"

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "Core/SaveStateLANSync.h"
#include "Core/SaveStateSyncMetadata.h"
#include "Core/LANSyncConfig.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"

#include "Common/Data/HLC.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DirListing.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/Crypto/sha256.h"

#include "Common/Net/MDNS.h"
#include "Common/Net/UDPDiscovery.h"
#include "Common/Net/TLSServer.h"
#include "Common/Net/PlatformKeyStore.h"
#include "Common/Net/HTTPServer.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/SocketCompat.h"

// ==================== Utility ====================

static std::string ComputeSHA256(const void *data, size_t len) {
	sha256_context ctx;
	uint8_t hash[32];
	sha256_starts(&ctx);
	sha256_update(&ctx, (const uint8_t *)data, len);
	sha256_finish(&ctx, hash);
	std::ostringstream oss;
	for (int i = 0; i < 32; i++)
		oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
	return oss.str();
}

static std::string ComputeSHA256(const std::vector<uint8_t> &data) {
	return ComputeSHA256(data.data(), data.size());
}

static std::string GenerateDeviceId() {
	char hostname[256] = {0};
	gethostname(hostname, sizeof(hostname));
	return ComputeSHA256(hostname, strlen(hostname)).substr(0, 16);
}

static std::string GenerateSessionToken() {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;
	char hex[33];
	snprintf(hex, sizeof(hex), "%016llx%016llx",
	         (unsigned long long)dist(gen), (unsigned long long)dist(gen));
	return std::string(hex);
}

static constexpr size_t MAX_UPLOAD_SIZE = 100 * 1024 * 1024;  // 100MB

static const char *HttpStatusText(int status) {
	switch (status) {
	case 200: return "OK";
	case 201: return "Created";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 413: return "Payload Too Large";
	case 500: return "Internal Server Error";
	default: return "Unknown";
	}
}

static bool IsValidSaveFilename(const std::string &name) {
	if (name.empty() || name.size() > 255) return false;
	if (name.find("..") != std::string::npos) return false;
	if (name.find('/') != std::string::npos) return false;
	if (name.find('\\') != std::string::npos) return false;
	if (!endsWith(name, ".ppst") && !endsWith(name, ".jpg")) return false;
	return true;
}

static void WriteHTTPResponse(int fd, int status, const std::string &body,
                               const char *contentType = "application/json") {
	std::string header = StringFromFormat(
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n", status, HttpStatusText(status), contentType, (int)body.size());
	int sent = send(fd, header.c_str(), header.size(), 0);
	if (sent <= 0) return;  // Client disconnected
	sent = send(fd, body.c_str(), body.size(), 0);
	(void)sent;  // Best effort
}

// ==================== SaveStateLANSync ====================

SaveStateLANSync &SaveStateLANSync::Instance() {
	static SaveStateLANSync instance;
	return instance;
}

SaveStateLANSync::~SaveStateLANSync() { 
	Shutdown();
	JoinAllThreads();
}

void SaveStateLANSync::AddBackgroundThread(std::thread t) {
	std::lock_guard<std::mutex> lock(threadMutex_);
	backgroundThreads_.push_back(std::move(t));
}

void SaveStateLANSync::JoinAllThreads() {
	std::lock_guard<std::mutex> lock(threadMutex_);
	for (auto &t : backgroundThreads_) {
		if (t.joinable()) t.join();
	}
	backgroundThreads_.clear();
}

void SaveStateLANSync::Init() {
	deviceId_ = GenerateDeviceId();
	PlatformKeyStore::Init();
	LoadConfig();
	INFO_LOG(Log::System, "LANSync: initialized deviceId=%s", deviceId_.c_str());
}

void SaveStateLANSync::Shutdown() {
	StopDiscovery();
	StopServer();
	CancelSync();
	SaveConfig();
	PlatformKeyStore::Shutdown();
}

// ==================== Discovery ====================

void SaveStateLANSync::StartDiscovery() {
	mdnsBrowser_.reset(mDNS::Browser::Create());
	mdnsBrowser_->Start(
		[this](const mDNS::PeerInfo &peer) {
			std::lock_guard<std::mutex> lock(peerMutex_);
			PeerInfo info;
			info.id = peer.id; info.name = peer.name; info.device = peer.device;
			info.host = peer.host; info.port = peer.port;
			info.certFingerprint = peer.certFingerprint;
			info.online = true; info.lastSeen = time(nullptr);
			for (auto &p : pairedPeers_) {
				if (p.id == info.id) { p.online = true; p.lastSeen = info.lastSeen; return; }
			}
			discoveredPeers_.push_back(info);
		},
		[this](const std::string &id) {
			std::lock_guard<std::mutex> lock(peerMutex_);
			for (auto &p : pairedPeers_) { if (p.id == id) p.online = false; }
			discoveredPeers_.erase(std::remove_if(discoveredPeers_.begin(), discoveredPeers_.end(),
				[&id](const PeerInfo &p) { return p.id == id; }), discoveredPeers_.end());
		},
		nullptr);

	udpListener_.reset(new UDPDiscovery::Listener());
	udpListener_->Start(
		[this](const UDPDiscovery::PeerInfo &peer) {
			std::lock_guard<std::mutex> lock(peerMutex_);
			PeerInfo info;
			info.id = peer.id; info.name = peer.name; info.host = peer.host; info.port = peer.port;
			info.online = true; info.lastSeen = time(nullptr);
			for (auto &p : pairedPeers_) { if (p.id == info.id) { p.online = true; return; } }
			for (auto &p : discoveredPeers_) { if (p.id == info.id) return; }
			discoveredPeers_.push_back(info);
		}, nullptr, nullptr);
}

void SaveStateLANSync::StopDiscovery() {
	if (mdnsBrowser_) { mdnsBrowser_->Stop(); mdnsBrowser_.reset(); }
	if (mdnsAnnouncer_) { mdnsAnnouncer_->Unregister(); mdnsAnnouncer_.reset(); }
	if (udpAnnouncer_) { udpAnnouncer_->Stop(); udpAnnouncer_.reset(); }
	if (udpListener_) { udpListener_->Stop(); udpListener_.reset(); }
}

std::vector<SaveStateLANSync::PeerInfo> SaveStateLANSync::GetDiscoveredPeers() const {
	std::lock_guard<std::mutex> lock(peerMutex_);
	std::vector<PeerInfo> result = pairedPeers_;
	result.insert(result.end(), discoveredPeers_.begin(), discoveredPeers_.end());
	return result;
}

// ==================== Server ====================

bool SaveStateLANSync::StartServer() {
	// Start TLS context
	tlsCtx_.reset(new tls::TLSServerContext());
	if (!tlsCtx_->LoadFromKeystore()) {
		tlsCtx_->GenerateCertificate();
		tlsCtx_->SaveToKeystore();
	}

	// Start HTTP server FIRST to get port
	serverRunning_ = true;
	AddBackgroundThread(std::thread([this]() {
		listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSock_ < 0) {
			serverRunning_ = false;
			return;
		}

		int reuse = 1;
		setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = 0;  // Auto-assign

		if (bind(listenSock_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			closesocket(listenSock_);
			listenSock_ = -1;
			serverRunning_ = false;
			return;
		}

		// Get assigned port
		socklen_t addrLen = sizeof(addr);
		getsockname(listenSock_, (struct sockaddr *)&addr, &addrLen);
		{
			std::lock_guard<std::mutex> lock(serverMutex_);
			serverPort_ = ntohs(addr.sin_port);
		}

		// Set recv timeout (10 seconds) for bug #2
		struct timeval tv;
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		setsockopt(listenSock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		listen(listenSock_, 5);

		// NOW announce via mDNS + UDP (port is known)
		mdnsAnnouncer_.reset(mDNS::Announcer::Create());
		mDNS::ServiceInfo svc;
		svc.id = deviceId_; svc.name = deviceName_;
		svc.port = serverPort_;
		svc.certFingerprint = tlsCtx_->GetFingerprint();
		mdnsAnnouncer_->Register(svc, nullptr);

		UDPDiscovery::PeerInfo udpInfo;
		udpInfo.id = deviceId_; udpInfo.name = deviceName_;
		udpInfo.port = serverPort_;
		udpInfo.certFingerprint = tlsCtx_->GetFingerprint();
		udpAnnouncer_.reset(new UDPDiscovery::Announcer());
		udpAnnouncer_->Start(udpInfo, nullptr);

		INFO_LOG(Log::System, "LANSync: server listening on port %d", serverPort_);

		while (serverRunning_) {
			struct sockaddr_in clientAddr;
			socklen_t clientLen = sizeof(clientAddr);
			int clientFd = accept(listenSock_, (struct sockaddr *)&clientAddr, &clientLen);
			if (clientFd < 0) {
				if (!serverRunning_) break;
				continue;
			}

			// Set recv timeout on client socket too
			setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

			// Handle each connection on a separate thread
			std::thread([this, clientFd]() {
				char buf[4096];
				int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
				if (n <= 0) { closesocket(clientFd); return; }
				buf[n] = '\0';
				std::string request(buf, n);

				// Parse HTTP request line
				std::string method, path;
				size_t space1 = request.find(' ');
				if (space1 != std::string::npos) {
					method = request.substr(0, space1);
					size_t space2 = request.find(' ', space1 + 1);
					if (space2 != std::string::npos) {
						path = request.substr(space1 + 1, space2 - space1 - 1);
					}
				}

				// Extract body (after \r\n\r\n)
				size_t bodyStart = request.find("\r\n\r\n");
				std::string body;
				if (bodyStart != std::string::npos) {
					body = request.substr(bodyStart + 4);
				}

				// Route requests
				if (path == "/api/v1/pair" && method == "POST") {
					std::string response;
					HandlePairRequest(body, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path == "/api/v1/saves/list") {
					std::string game;
					size_t gamePos = request.find("game=");
					if (gamePos != std::string::npos) {
						gamePos += 5;
						size_t gameEnd = request.find_first_of(" &\r\n", gamePos);
						game = request.substr(gamePos, gameEnd - gamePos);
					}
					std::string response;
					HandleSaveList(game, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path == "/api/v1/status") {
					std::string response = StringFromFormat(
						"{\"deviceId\":\"%s\",\"name\":\"%s\",\"version\":\"1.0\",\"port\":%d}",
						deviceId_.c_str(), deviceName_.c_str(), serverPort_);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path.find("/api/v1/saves/") == 0 && path.size() > 15) {
					std::string filename = path.substr(15);  // after /api/v1/saves/
					
					if (!IsValidSaveFilename(filename)) {
						WriteHTTPResponse(clientFd, 400, "{\"error\":\"invalid_filename\"}");
						closesocket(clientFd);
						return;
					}

					if (method == "GET") {
						std::string fileData;
						Path filePath = GetSysDirectory(DIRECTORY_SAVESTATE) / filename;
						if (File::ReadBinaryFileToString(filePath, &fileData)) {
							std::string header = StringFromFormat(
								"HTTP/1.1 200 OK\r\n"
								"Content-Type: application/octet-stream\r\n"
								"Content-Length: %d\r\n"
								"Connection: close\r\n"
								"\r\n", (int)fileData.size());
							int sent = send(clientFd, header.c_str(), header.size(), 0);
							if (sent > 0) {
								send(clientFd, fileData.c_str(), fileData.size(), 0);
							}
						} else {
							WriteHTTPResponse(clientFd, 404, "{\"error\":\"not_found\"}");
						}
					} else if (method == "POST") {
						// Parse Content-Length header
						size_t contentLength = 0;
						size_t clPos = request.find("Content-Length:");
						if (clPos != std::string::npos) {
							clPos += 15;
							while (clPos < request.size() && request[clPos] == ' ') clPos++;
							contentLength = strtoul(request.c_str() + clPos, nullptr, 10);
						}
						
						if (contentLength > MAX_UPLOAD_SIZE) {
							WriteHTTPResponse(clientFd, 413, "{\"error\":\"payload_too_large\"}");
							closesocket(clientFd);
							return;
						}
						
						// Read exact Content-Length bytes
						std::vector<uint8_t> allBody;
						if (!body.empty()) {
							allBody.assign(body.begin(), body.end());
						}
						
						if (contentLength > 0 && allBody.size() < contentLength) {
							allBody.resize(contentLength);
							size_t bytesRead = allBody.size();
							while (bytesRead < contentLength) {
								int n = recv(clientFd, (char *)allBody.data() + bytesRead,
								             contentLength - bytesRead, 0);
								if (n <= 0) break;
								bytesRead += n;
							}
							allBody.resize(bytesRead);
						}

						size_t lastUnderscore = filename.rfind('_');
						size_t dot = filename.rfind('.');
						std::string gameId = (lastUnderscore != std::string::npos) ?
							filename.substr(0, lastUnderscore) : "unknown";
						int slot = (lastUnderscore != std::string::npos && dot != std::string::npos) ?
							atoi(filename.substr(lastUnderscore + 1, dot - lastUnderscore - 1).c_str()) : -1;

						std::string response;
						HandleSaveUpload(gameId, slot, allBody, "", response);
						WriteHTTPResponse(clientFd, 201, response);
					} else {
						WriteHTTPResponse(clientFd, 405, "{\"error\":\"method_not_allowed\"}");
					}
				} else {
					WriteHTTPResponse(clientFd, 404, "{\"error\":\"not_found\"}");
				}
				closesocket(clientFd);
			}).detach();
		}
		closesocket(listenSock_);
		listenSock_ = -1;
	}));

	return true;
}

void SaveStateLANSync::StopServer() {
	serverRunning_ = false;
	if (listenSock_ >= 0) {
		closesocket(listenSock_);  // Wake up accept()
		listenSock_ = -1;
	}
	StopDiscovery();
}

int SaveStateLANSync::GetServerPort() const {
	std::lock_guard<std::mutex> lock(serverMutex_);
	return serverPort_;
}

// ==================== Pairing ====================

std::string SaveStateLANSync::GeneratePairingPin() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dist(0, 9);
	char pin[7];
	for (int i = 0; i < 6; i++) pin[i] = '0' + dist(gen);
	pin[6] = '\0';
	pairingPin_ = pin;
	return pairingPin_;
}

void SaveStateLANSync::CancelPairing() { pairingPin_.clear(); }

void SaveStateLANSync::PairWithPeer(const std::string &peerId, const std::string &pin,
                                     std::function<void(bool, const std::string &)> callback) {
	// Parse peerId = "host:port"
	size_t colon = peerId.rfind(':');
	if (colon == std::string::npos) {
		if (callback) callback(false, "Invalid peer ID");
		return;
	}
	std::string host = peerId.substr(0, colon);
	int port = atoi(peerId.substr(colon + 1).c_str());

	// Run on background thread
	AddBackgroundThread(std::thread([this, host, port, pin, callback, peerId]() {
		// Build pair request JSON
		std::string body = StringFromFormat(
			"{\"pin\":\"%s\",\"name\":\"%s\",\"id\":\"%s\"}",
			pin.c_str(), deviceName_.c_str(), deviceId_.c_str());

		// TCP connect + send HTTP request
		int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) {
			if (callback) callback(false, "Socket error");
			return;
		}

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			closesocket(sock);
			if (callback) callback(false, "Connection refused");
			return;
		}

		std::string request = StringFromFormat(
			"POST /api/v1/pair HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n%s", host.c_str(), port, (int)body.size(), body.c_str());

		send(sock, request.c_str(), request.size(), 0);

		// Read response
		char buf[4096];
		int n = recv(sock, buf, sizeof(buf) - 1, 0);
		closesocket(sock);

		if (n > 0) {
			buf[n] = '\0';
			std::string response(buf, n);

			// Look for 200 status
			if (response.find("200 OK") != std::string::npos) {
				// Extract token from JSON body
				size_t bodyStart = response.find("\r\n\r\n");
				if (bodyStart != std::string::npos) {
					std::string jsonBody = response.substr(bodyStart + 4);
					// Simple JSON parse: {"token":"...","peerId":"..."}
					size_t tokenPos = jsonBody.find("\"token\":\"");
					if (tokenPos != std::string::npos) {
						tokenPos += 9;
						size_t tokenEnd = jsonBody.find('"', tokenPos);
						if (tokenEnd != std::string::npos) {
							std::string token = jsonBody.substr(tokenPos, tokenEnd - tokenPos);

							// Save peer
							std::lock_guard<std::mutex> lock(peerMutex_);
							PeerInfo peer;
							peer.id = peerId; peer.token = token;
							peer.paired = true; peer.online = true;
							peer.host = host; peer.port = port;
							peer.lastSeen = time(nullptr);
							pairedPeers_.push_back(peer);
							SaveConfig();

							if (callback) callback(true, "");
							return;
						}
					}
				}
			}
		}
		if (callback) callback(false, "Pairing failed");
	}));
}

void SaveStateLANSync::AcceptPairing(const std::string &peerId, const std::string &peerName,
                                      std::function<void(bool)> callback) {
	PeerInfo peer;
	peer.id = peerId; peer.name = peerName; peer.device = "Unknown";
	peer.paired = true; peer.online = true;
	peer.token = GenerateSessionToken(); peer.lastSeen = time(nullptr);
	{
		std::lock_guard<std::mutex> lock(peerMutex_);
		if (pairedPeers_.size() >= 5) { if (callback) callback(false); return; }
		pairedPeers_.push_back(peer);
	}
	SaveConfig();
	if (callback) callback(true);
}

void SaveStateLANSync::UnpairPeer(const std::string &peerId) {
	std::lock_guard<std::mutex> lock(peerMutex_);
	pairedPeers_.erase(std::remove_if(pairedPeers_.begin(), pairedPeers_.end(),
		[&](const PeerInfo &p) { return p.id == peerId; }), pairedPeers_.end());
	SaveConfig();
}

// ==================== Sync ====================

void SaveStateLANSync::SyncWithPeer(const std::string &peerId, SyncDirection direction,
                                     ProgressCallback onProgress, DoneCallback onDone) {
	PeerInfo target;
	{
		std::lock_guard<std::mutex> lock(peerMutex_);
		for (auto &p : pairedPeers_) { if (p.id == peerId && p.online) { target = p; break; } }
	}
	if (target.id.empty()) {
		if (onDone) { SyncResult r; r.success = false; onDone(r); }
		return;
	}

	syncStatus_ = SyncStatus::SYNCING;
	syncCancelled_ = false;

	AddBackgroundThread(std::thread([this, target, onProgress, onDone]() {
		SaveStateLANSync::SyncResult result = DoSync(target, onProgress);
		syncStatus_ = result.success ? SyncStatus::DONE : SyncStatus::ERROR;
		if (onDone) onDone(result);
	}));
}

SaveStateLANSync::SyncResult SaveStateLANSync::DoSync(const PeerInfo &peer, SaveStateLANSync::ProgressCallback onProgress) {
	SyncResult result;

	// Connect to peer
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) { result.success = false; return result; }

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(peer.port);
	inet_pton(AF_INET, peer.host.c_str(), &addr.sin_addr);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		closesocket(sock); result.success = false; return result;
	}

	// Scan local savestate directory for all games
	Path saveDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(saveDir, &files, ".ppst");

	// Group by game prefix
	std::map<std::string, std::vector<int>> localGames;
	for (const auto &f : files) {
		std::string name = f.name;
		// Extract gamePrefix and slot from filename: PREFIX_N.ppst
		size_t lastUnderscore = name.rfind('_');
		size_t dot = name.rfind('.');
		if (lastUnderscore == std::string::npos || dot == std::string::npos) continue;

		std::string prefix = name.substr(0, lastUnderscore);
		int slot = atoi(name.substr(lastUnderscore + 1, dot - lastUnderscore - 1).c_str());
		localGames[prefix].push_back(slot);

		// Create/update metadata
		Path ppstPath = saveDir / name;
		std::string fileData;
		if (File::ReadBinaryFileToString(ppstPath, &fileData)) {
			SaveStateSyncMetadata meta;
			if (!SaveStateSyncMetadata::ReadFromFile(ppstPath, meta)) {
				meta.hash = ComputeSHA256(fileData.data(), fileData.size());
				meta.lastSyncTime = 0;
				meta.deviceId = deviceId_;
			}
			meta.WriteToFile(ppstPath);
		}
	}

	// Get remote save list
	std::string request = StringFromFormat(
		"GET /api/v1/saves/list HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Authorization: Bearer %s\r\n"
		"Connection: close\r\n\r\n",
		peer.host.c_str(), peer.port, peer.token.c_str());
	send(sock, request.c_str(), request.size(), 0);

	char buf[65536];
	int n = recv(sock, buf, sizeof(buf) - 1, 0);
	closesocket(sock);

	if (n <= 0) { result.success = false; return result; }
	buf[n] = '\0';

	// Parse response: extract body after \r\n\r\n
	std::string response(buf, n);
	size_t bodyStart = response.find("\r\n\r\n");
	if (bodyStart == std::string::npos) { result.success = false; return result; }
	std::string jsonBody = response.substr(bodyStart + 4);

	// Simple JSON array parse: [{slot, size, hash, hlc, parentHlc, ppssppVersion, saveFormatVersion}]
	size_t pos = 0;
	int syncedCount = 0;
	while ((pos = jsonBody.find("\"slot\":", pos)) != std::string::npos) {
		pos += 7;
		int slot = atoi(jsonBody.c_str() + pos);

		// Extract hash
		size_t hashPos = jsonBody.find("\"hash\":\"", pos);
		std::string remoteHash;
		if (hashPos != std::string::npos && hashPos < jsonBody.find('}', pos)) {
			hashPos += 8;
			size_t hashEnd = jsonBody.find('"', hashPos);
			if (hashEnd != std::string::npos)
				remoteHash = jsonBody.substr(hashPos, hashEnd - hashPos);
		}

		// Compare with local, download if remote is newer or missing locally
		Path localPath = saveDir / StringFromFormat("%s_%d.ppst", "current_game", slot);
		std::string localData;
		bool hasLocal = File::ReadBinaryFileToString(localPath, &localData);
		
		if (!hasLocal || !remoteHash.empty()) {
			// Download from remote
			std::string dlRequest = StringFromFormat(
				"GET /api/v1/saves/%s_%d.ppst HTTP/1.1\r\n"
				"Host: %s:%d\r\n"
				"Authorization: Bearer %s\r\n"
				"Connection: close\r\n\r\n",
				"current_game", slot, peer.host.c_str(), peer.port, peer.token.c_str());

			int dlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (dlSock >= 0) {
				// Set timeout for download socket
				struct timeval dlTv;
				dlTv.tv_sec = 30;  // 30 second timeout for downloads
				dlTv.tv_usec = 0;
				setsockopt(dlSock, SOL_SOCKET, SO_RCVTIMEO, &dlTv, sizeof(dlTv));

				struct sockaddr_in dlAddr;
				memset(&dlAddr, 0, sizeof(dlAddr));
				dlAddr.sin_family = AF_INET;
				dlAddr.sin_port = htons(peer.port);
				inet_pton(AF_INET, peer.host.c_str(), &dlAddr.sin_addr);

				if (connect(dlSock, (struct sockaddr *)&dlAddr, sizeof(dlAddr)) >= 0) {
					int sent = send(dlSock, dlRequest.c_str(), dlRequest.size(), 0);
					if (sent <= 0) {
						WARN_LOG(Log::System, "LANSync: send failed for download request");
						closesocket(dlSock);
						continue;
					}

					// Read response (header + binary body)
					std::vector<uint8_t> dlBuf(65536);
					int total = 0, n;
					while ((n = recv(dlSock, (char *)dlBuf.data() + total, 
					                 dlBuf.size() - total - 1, 0)) > 0) {
						total += n;
						if (total >= (int)dlBuf.size() - 1024)
							dlBuf.resize(dlBuf.size() * 2);
					}

					if (total > 0) {
						// Find body after \r\n\r\n
						std::string responseStr((char *)dlBuf.data(), total);
						size_t bodyStart = responseStr.find("\r\n\r\n");
						if (bodyStart != std::string::npos) {
							bodyStart += 4;
							std::vector<uint8_t> fileData(dlBuf.begin() + bodyStart, 
							                               dlBuf.begin() + total);
							// Write to local
							Path tmpPath = localPath.WithExtraExtension(".tmp");
							if (File::WriteDataToFile(false, fileData.data(), fileData.size(), tmpPath)) {
								File::Rename(tmpPath, localPath);
								result.downloaded++;
							} else {
								WARN_LOG(Log::System, "LANSync: failed to write downloaded file");
								File::Delete(tmpPath);
							}
						}
					}
				}
				closesocket(dlSock);
			}
		}

		syncedCount++;
	}
	result.success = true;
	return result;
}

void SaveStateLANSync::CancelSync() { syncCancelled_ = true; }

// ==================== Conflict ====================

void SaveStateLANSync::ResolveConflict(const ConflictInfo &conflict, ConflictResolution resolution) {
	switch (resolution) {
	case ConflictResolution::KEEP_LOCAL: break;
	case ConflictResolution::KEEP_REMOTE: {
		// Re-download from peer (handled in DoSync)
		break;
	}
	default: break;
	}
}

void SaveStateLANSync::ResolveAllConflicts(ConflictResolution resolution) {
	std::lock_guard<std::mutex> lock(conflictMutex_);
	for (auto &c : pendingConflicts_) ResolveConflict(c, resolution);
	pendingConflicts_.clear();
}

// ==================== Hooks ====================

void SaveStateLANSync::OnSaveStateSaved(const std::string &gamePrefix, int slot) {
	Path savePath = SaveState::GenerateSaveSlotPath(gamePrefix, slot, "ppst");
	std::string fileData;
	if (!File::ReadBinaryFileToString(savePath, &fileData)) return;

	SaveStateSyncMetadata meta;
	meta.hash = ComputeSHA256(fileData.data(), fileData.size());
	meta.hlc = meta.hlc.Increment(deviceId_);
	meta.parentHlc = meta.hlc;
	meta.deviceId = deviceId_;
	meta.lastSyncTime = time(nullptr);
	meta.WriteToFile(savePath);

	INFO_LOG(Log::System, "LANSync: metadata created for %s slot %d", gamePrefix.c_str(), slot);
}

void SaveStateLANSync::OnSaveStateLoaded(const std::string &gamePrefix, int slot) {
	INFO_LOG(Log::System, "LANSync: save loaded %s slot %d", gamePrefix.c_str(), slot);
}

// ==================== State ====================

bool SaveStateLANSync::IsServerRunning() const { return serverPort_ > 0; }
SaveStateLANSync::SyncStatus SaveStateLANSync::GetStatus() const { return syncStatus_; }
SaveStateLANSync::SyncProgress SaveStateLANSync::GetProgress() const { std::lock_guard<std::mutex> lock(syncMutex_); return syncProgress_; }
std::string SaveStateLANSync::GetCurrentPin() const { return pairingPin_; }

std::string SaveStateLANSync::GetDeviceId() const { return deviceId_; }

// ==================== Config ====================

bool SaveStateLANSync::LoadConfig() {
	// Load from Config (persisted via INI) with PlatformKeyStore fallback
	deviceName_ = g_Config.lanSync.sDeviceName;
	if (deviceName_.empty())
		deviceName_ = PlatformKeyStore::Load("ppsspp-lansync-devicename");
	if (deviceName_.empty())
		deviceName_ = "PPSSPP";

	std::string peersJSON = g_Config.lanSync.sPairedPeers;
	if (peersJSON.empty())
		peersJSON = PlatformKeyStore::Load("ppsspp-lansync-peers");
	if (!peersJSON.empty()) {
		// Simple JSON array parse
		size_t pos = 0;
		while ((pos = peersJSON.find("\"id\":\"", pos)) != std::string::npos) {
			pos += 6;
			size_t end = peersJSON.find('"', pos);
			if (end == std::string::npos) break;
			std::string id = peersJSON.substr(pos, end - pos);

			PeerInfo peer;
			peer.id = id; peer.paired = true; peer.online = false;

			// Extract name
			size_t namePos = peersJSON.find("\"name\":\"", end);
			if (namePos != std::string::npos && namePos < peersJSON.find('}', pos)) {
				namePos += 8;
				size_t nameEnd = peersJSON.find('"', namePos);
				if (nameEnd != std::string::npos)
					peer.name = peersJSON.substr(namePos, nameEnd - namePos);
			}

			// Extract token
			size_t tokPos = peersJSON.find("\"token\":\"", end);
			if (tokPos != std::string::npos && tokPos < peersJSON.find('}', pos)) {
				tokPos += 9;
				size_t tokEnd = peersJSON.find('"', tokPos);
				if (tokEnd != std::string::npos)
					peer.token = peersJSON.substr(tokPos, tokEnd - tokPos);
			}

			pairedPeers_.push_back(peer);
			pos = end;
		}
	}
	return true;
}

bool SaveStateLANSync::SaveConfig() {
	// Save to Config (persisted via INI on Save())
	g_Config.lanSync.sDeviceName = deviceName_;

	std::string peersJSON = "[";
	for (size_t i = 0; i < pairedPeers_.size(); i++) {
		if (i > 0) peersJSON += ",";
		peersJSON += StringFromFormat(
			"{\"id\":\"%s\",\"name\":\"%s\",\"token\":\"%s\"}",
			pairedPeers_[i].id.c_str(), pairedPeers_[i].name.c_str(),
			pairedPeers_[i].token.c_str());
	}
	peersJSON += "]";
	g_Config.lanSync.sPairedPeers = peersJSON;

	// Also save to PlatformKeyStore for runtime fallback
	PlatformKeyStore::Save("ppsspp-lansync-devicename", deviceName_);
	return PlatformKeyStore::Save("ppsspp-lansync-peers", peersJSON);
}

// ==================== API Handlers ====================

void SaveStateLANSync::HandlePairRequest(const std::string &body, std::string &response) {
	// Parse {pin:"123456", name:"Pixel7", id:"abc..."}
	std::string pin, peerName, peerId;

	auto extractStr = [&body](const char *key) -> std::string {
		std::string search = std::string("\"") + key + "\":\"";
		size_t pos = body.find(search);
		if (pos == std::string::npos) return "";
		pos += search.size();
		size_t end = body.find('"', pos);
		return (end != std::string::npos) ? body.substr(pos, end - pos) : "";
	};

	pin = extractStr("pin");
	peerName = extractStr("name");
	peerId = extractStr("id");

	// Validate PIN
	if (pin != pairingPin_ || pairingPin_.empty()) {
		response = "{\"error\":\"invalid_pin\"}";
		return;
	}

	// Generate token and accept pairing
	std::string token = GenerateSessionToken();
	AcceptPairing(peerId, peerName, nullptr);

	response = StringFromFormat(
		"{\"token\":\"%s\",\"peerId\":\"%s\",\"certFingerprint\":\"%s\"}",
		token.c_str(), deviceId_.c_str(),
		tlsCtx_ ? tlsCtx_->GetFingerprint().c_str() : "");

	// Clear PIN after successful pairing
	pairingPin_.clear();
}

void SaveStateLANSync::HandleSaveList(const std::string &gameId, std::string &response) {
	Path saveDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(saveDir, &files, ".ppst");

	response = "[";
	bool first = true;
	for (const auto &f : files) {
		std::string name = f.name;
		size_t lastUnderscore = name.rfind('_');
		size_t dot = name.rfind('.');
		if (lastUnderscore == std::string::npos || dot == std::string::npos) continue;

		std::string prefix = name.substr(0, lastUnderscore);
		if (!gameId.empty() && prefix != gameId) continue;

		int slot = atoi(name.substr(lastUnderscore + 1, dot - lastUnderscore - 1).c_str());

		Path ppstPath = saveDir / name;
		SaveStateSyncMetadata meta;
		SaveStateSyncMetadata::ReadFromFile(ppstPath, meta);

		if (!first) response += ","; first = false;
		response += StringFromFormat(
			"{\"slot\":%d,\"size\":%lld,\"hash\":\"%s\","
			"\"hlc\":%s,\"parentHlc\":%s,"
			"\"ppssppVersion\":\"\",\"saveFormatVersion\":0,\"hasThumbnail\":false}",
			slot, (long long)meta.fileSize, meta.hash.c_str(),
			meta.hlc.ToJSON().c_str(), meta.parentHlc.ToJSON().c_str());
	}
	response += "]";
}

void SaveStateLANSync::HandleSaveDownload(const std::string &gameId, int slot,
                                           std::vector<uint8_t> &data) {
	Path path = SaveState::GenerateSaveSlotPath(gameId, slot, "ppst");
	std::string str;
	if (File::ReadBinaryFileToString(path, &str)) {
		data.assign(str.begin(), str.end());
	}
}

void SaveStateLANSync::HandleSaveUpload(const std::string &gameId, int slot,
                                         const std::vector<uint8_t> &data,
                                         const std::string &hash, std::string &response) {
	Path path = SaveState::GenerateSaveSlotPath(gameId, slot, "ppst");

	// Verify hash
	std::string actualHash = ComputeSHA256(data);
	if (!hash.empty() && actualHash != hash) {
		response = "{\"error\":\"hash_mismatch\"}";
		return;
	}

	// Write file (atomic: .tmp then rename)
	Path tmpPath = path.WithExtraExtension(".tmp");
	File::WriteDataToFile(false, data.data(), data.size(), tmpPath);
	File::Rename(tmpPath, path);

	// Create metadata
	SaveStateSyncMetadata meta;
	meta.hash = actualHash;
	meta.hlc = meta.hlc.Increment(deviceId_);
	meta.deviceId = deviceId_;
	meta.fileSize = data.size();
	meta.lastSyncTime = time(nullptr);
	meta.WriteToFile(path);

	response = "{\"ok\":true}";
}
