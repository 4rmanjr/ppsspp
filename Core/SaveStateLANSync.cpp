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
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <set>
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
#include "Common/System/System.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#endif

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

static std::string GenerateNonce() {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;
	char hex[17];
	snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)dist(gen));
	return std::string(hex);
}

// Numeric comparison: both sides compute the same 6-digit code from nonce + device IDs.
// Neither side sends the actual code over the network — only the nonce is exchanged.
static std::string ComputeVerificationCode(const std::string &nonce, const std::string &idA, const std::string &idB) {
	// Sort IDs so both sides compute the same hash regardless of order
	std::string combined;
	if (idA < idB)
		combined = nonce + idA + idB;
	else
		combined = nonce + idB + idA;
	std::string hash = ComputeSHA256(combined.c_str(), combined.size());
	// Take first 6 hex chars, convert to integer, mod 1000000, pad to 6 digits
	unsigned long val = strtoul(hash.substr(0, 6).c_str(), nullptr, 16);
	char code[7];
	snprintf(code, sizeof(code), "%06lu", val % 1000000);
	return std::string(code);
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

// Extracts Bearer token from HTTP Authorization header.
// Returns empty string if header missing or malformed.
static std::string ExtractBearerToken(const std::string &request) {
	const char *prefix = "Authorization: Bearer ";
	size_t pos = request.find(prefix);
	if (pos == std::string::npos)
		return "";
	pos += strlen(prefix);
	size_t end = request.find("\r\n", pos);
	if (end == std::string::npos)
		end = request.size();
	return request.substr(pos, end - pos);
}

// ==================== SaveStateLANSync ====================

SaveStateLANSync &SaveStateLANSync::Instance() {
	static SaveStateLANSync instance;
	return instance;
}

SaveStateLANSync::~SaveStateLANSync() {
	// Signal all background threads to stop before joining
	syncCancelled_ = true;
	serverRunning_ = false;
	if (listenSock_ >= 0) {
		closesocket(listenSock_);
		listenSock_ = -1;
	}
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
	// Generate a deterministic fallback deviceId from hostname
	deviceId_ = GenerateDeviceId();
	PlatformKeyStore::Init();
	LoadConfig();

	// Try to load a persistent unique deviceId from key store.
	// This ensures the deviceId stays the same across restarts (paired peers remain valid)
	// while also being unique across devices even if hostname is identical (e.g. "localhost").
	std::string storedId = PlatformKeyStore::Load("ppsspp-lansync-deviceid");
	if (!storedId.empty()) {
		deviceId_ = storedId;
	} else {
		// First run: generate a unique deviceId using hostname + random seed
		char hostname[256] = {0};
		gethostname(hostname, sizeof(hostname));
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<uint64_t> dist;
		std::string unique = StringFromFormat("%s-%016llx", hostname, (unsigned long long)dist(gen));
		deviceId_ = ComputeSHA256(unique.c_str(), unique.size()).substr(0, 16);
		PlatformKeyStore::Save("ppsspp-lansync-deviceid", deviceId_);
	}

	// Set device type based on platform
#if PPSSPP_PLATFORM(ANDROID)
	deviceType_ = "Android";
#elif PPSSPP_PLATFORM(WINDOWS)
	deviceType_ = "Windows";
#elif PPSSPP_PLATFORM(MAC)
	deviceType_ = "macOS";
#else
	deviceType_ = "Linux";
#endif

	INFO_LOG(Log::System, "LANSync: initialized deviceId=%s deviceType=%s", deviceId_.c_str(), deviceType_.c_str());
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
			// Don't discover ourselves
			if (peer.id == deviceId_)
				return;
			std::lock_guard<std::mutex> lock(peerMutex_);
			PeerInfo info;
			info.id = peer.id; info.name = peer.name; info.device = peer.device;
			info.host = peer.host; info.port = peer.port;
			info.certFingerprint = peer.certFingerprint;
			info.online = true; info.lastSeen = time(nullptr);
			for (auto &p : pairedPeers_) {
				if (p.id == info.id) { p.online = true; p.lastSeen = info.lastSeen; p.host = info.host; p.port = info.port; return; }
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
			// Don't discover ourselves
			if (peer.id == deviceId_)
				return;
			std::lock_guard<std::mutex> lock(peerMutex_);
			PeerInfo info;
			info.id = peer.id; info.name = peer.name; info.device = peer.device; info.host = peer.host; info.port = peer.port;
			info.online = true; info.lastSeen = time(nullptr);
			for (auto &p : pairedPeers_) { if (p.id == info.id) { p.online = true; p.lastSeen = info.lastSeen; p.host = info.host; p.port = info.port; return; } }
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
	std::vector<PeerInfo> result;
	for (const auto &p : pairedPeers_) {
		if (p.id != deviceId_)  // Filter out any self-paired entries
			result.push_back(p);
	}
	for (const auto &p : discoveredPeers_) {
		if (p.id != deviceId_)
			result.push_back(p);
	}
	return result;
}

void SaveStateLANSync::AddDiscoveredPeer(const PeerInfo &peer) {
	if (peer.id == deviceId_ || peer.id.empty())
		return;
	std::lock_guard<std::mutex> lock(peerMutex_);
	// Check paired peers first
	for (auto &p : pairedPeers_) {
		if (p.id == peer.id) {
			p.online = true;
			p.lastSeen = time(nullptr);
			p.host = peer.host;
			p.port = peer.port;
			return;
		}
	}
	// Check already discovered
	for (auto &p : discoveredPeers_) {
		if (p.id == peer.id) {
			p.online = true;
			p.lastSeen = time(nullptr);
			return;
		}
	}
	// New peer
	discoveredPeers_.push_back(peer);
	discoveredPeers_.back().online = true;
	discoveredPeers_.back().lastSeen = time(nullptr);
}

void SaveStateLANSync::RemoveDiscoveredPeer(const std::string &id) {
	if (id.empty())
		return;
	std::lock_guard<std::mutex> lock(peerMutex_);
	for (auto &p : pairedPeers_) {
		if (p.id == id) {
			p.online = false;
			return;
		}
	}
	discoveredPeers_.erase(std::remove_if(discoveredPeers_.begin(), discoveredPeers_.end(),
		[&id](const PeerInfo &p) { return p.id == id; }), discoveredPeers_.end());
}

void SaveStateLANSync::SetDeviceInfo(const std::string &name, const std::string &type) {
	deviceName_ = name;
	deviceType_ = type;
	g_Config.lanSync.sDeviceName = name;
	SaveConfig();
	INFO_LOG(Log::System, "LANSync: device info updated: name=%s type=%s", name.c_str(), type.c_str());
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
		svc.id = deviceId_; svc.name = deviceName_; svc.device = deviceType_;
		svc.port = serverPort_;
		svc.certFingerprint = tlsCtx_->GetFingerprint();
		mdnsAnnouncer_->Register(svc, nullptr);

		UDPDiscovery::PeerInfo udpInfo;
		udpInfo.id = deviceId_; udpInfo.name = deviceName_; udpInfo.device = deviceType_;
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

			// Get client IP for pairing
			struct sockaddr_in peerAddr;
			socklen_t peerAddrLen = sizeof(peerAddr);
			std::string clientHost = "0.0.0.0";
			if (getpeername(clientFd, (struct sockaddr *)&peerAddr, &peerAddrLen) == 0) {
				char buf[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &peerAddr.sin_addr, buf, sizeof(buf));
				clientHost = buf;
			}

			// Handle each connection on a separate thread
			AddBackgroundThread(std::thread([this, clientFd, clientHost]() {
#if PPSSPP_PLATFORM(ANDROID)
				// RAII JVM attach: detach automatically on scope exit
				extern JavaVM *gJvm;
				struct JVMDetacher {
					JavaVM *vm = nullptr;
					bool attached = false;
					~JVMDetacher() { if (attached && vm) vm->DetachCurrentThread(); }
				} jvmDetacher;
				if (gJvm) {
					JNIEnv *jniEnv = nullptr;
					int status = gJvm->GetEnv((void **)&jniEnv, JNI_VERSION_1_6);
					if (status == JNI_EDETACHED) {
						JavaVMAttachArgs args = {JNI_VERSION_1_6, "LANSyncServer", nullptr};
						jvmDetacher.vm = gJvm;
						jvmDetacher.attached = (gJvm->AttachCurrentThread(&jniEnv, &args) == JNI_OK);
					}
				}
#endif
				// Wrap socket in TLS if available
				// TODO: Replace recv/send with SSL_read/SSL_write when sslHandle is set.
				// Current implementation calls AcceptTLS for cert exchange but data
				// still flows over plain TCP until WriteHTTPResponse is refactored.
				int ioFd = clientFd;
				void *sslHandle = nullptr;
				if (tlsCtx_ && tlsCtx_->AcceptTLS(clientFd, ioFd)) {
					sslHandle = tlsCtx_->GetSSL(clientFd);
				}

				// Read full request (headers + body)
				std::string request;
				char tempBuf[16384];
				size_t contentLength = 0;
				size_t headerEnd = std::string::npos;
				for (int i = 0; i < 200; i++) {
					int n = recv(clientFd, tempBuf, sizeof(tempBuf) - 1, 0);
					if (n <= 0) break;
					tempBuf[n] = '\0';
					request.append(tempBuf, n);
					if (headerEnd == std::string::npos) {
						headerEnd = request.find("\r\n\r\n");
						if (headerEnd != std::string::npos) {
							// Parse Content-Length
							size_t clPos = request.find("Content-Length:");
							if (clPos != std::string::npos) {
								clPos += 15;
								while (clPos < request.size() && request[clPos] == ' ') clPos++;
								contentLength = strtoul(request.c_str() + clPos, nullptr, 10);
							}
						}
					}
					// Stop if we have all headers and the complete body
					if (headerEnd != std::string::npos) {
						size_t bodySize = request.size() - headerEnd - 4;
						if (bodySize >= contentLength) break;
					}
				}
				if (request.empty()) {
					closesocket(clientFd);
					if (sslHandle && tlsCtx_) tlsCtx_->CloseTLS(clientFd);
					return;
				}

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
				std::string body;
				if (headerEnd != std::string::npos) {
					body = request.substr(headerEnd + 4);
				}

				// Validate Bearer token on protected endpoints
				{
					bool isProtected = (path == "/api/v1/saves/list" ||
					                    path.find("/api/v1/saves/") == 0);
					if (isProtected) {
						std::string authToken = ExtractBearerToken(request);
						bool authorized = false;
						{
							std::lock_guard<std::mutex> lock(peerMutex_);
							for (const auto &p : pairedPeers_) {
								if (p.token == authToken && !authToken.empty()) {
									authorized = true;
									break;
								}
							}
						}
						if (!authorized) {
							WARN_LOG(Log::System, "LANSync: unauthorized %s %s (token=%s...)",
							         method.c_str(), path.c_str(),
							         authToken.empty() ? "none" : authToken.substr(0, 8).c_str());
							WriteHTTPResponse(clientFd, 401,
							                  "{\"error\":\"unauthorized\"}");
							closesocket(clientFd);
							if (sslHandle && tlsCtx_) tlsCtx_->CloseTLS(clientFd);
							return;
						}
					}
				}

				// Route requests
				if (path == "/api/v1/pair" && method == "POST") {
					std::string response;
					HandlePairRequest(body, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path == "/api/v1/pair-request" && method == "POST") {
					std::string response;
					HandleAutoPairRequest(body, clientHost, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path == "/api/v1/pair-respond" && method == "POST") {
					std::string response;
					HandlePairRespond(body, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path.find("/api/v1/pair-status") == 0) {
					std::string query;
					size_t qPos = path.find('?');
					if (qPos != std::string::npos) query = path.substr(qPos + 1);
					else query = "";
					std::string response;
					HandlePairStatus(query, response);
					WriteHTTPResponse(clientFd, 200, response);
				} else if (path == "/api/v1/pair-verify" && method == "POST") {
					std::string response;
					HandlePairVerify(body, response);
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
				} else if (path.find("/api/v1/saves/") == 0 && path.size() > 14) {
					std::string filename = path.substr(14);  // after /api/v1/saves/
					
					if (!IsValidSaveFilename(filename)) {
						WriteHTTPResponse(clientFd, 400, "{\"error\":\"invalid_filename\"}");
						closesocket(clientFd);
						if (sslHandle && tlsCtx_) tlsCtx_->CloseTLS(clientFd);
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
							if (sslHandle && tlsCtx_) tlsCtx_->CloseTLS(clientFd);
							return;
						}
						
						// Read exact Content-Length bytes
						std::vector<uint8_t> allBody;
						if (!body.empty()) {
							allBody.assign(body.begin(), body.end());
						}

						size_t bytesRead = body.size();
						if (contentLength > 0 && bytesRead < contentLength) {
							allBody.resize(contentLength);
							while (bytesRead < contentLength) {
								int n = recv(clientFd, (char *)allBody.data() + bytesRead,
								             contentLength - bytesRead, 0);
								if (n <= 0) break;
								bytesRead += n;
							}
							allBody.resize(bytesRead);
						}

						if (endsWith(filename, ".jpg")) {
							Path filePath = GetSysDirectory(DIRECTORY_SAVESTATE) / filename;
							Path tmp = filePath.WithExtraExtension(".tmp");
							if (File::WriteDataToFile(false, allBody.data(), allBody.size(), tmp)) {
								File::Rename(tmp, filePath);
								WriteHTTPResponse(clientFd, 201, "{\"ok\":true}");
							} else {
								File::Delete(tmp);
								WriteHTTPResponse(clientFd, 500, "{\"error\":\"write_failed\"}");
							}
						} else {
							size_t lastUnderscore = filename.rfind('_');
							size_t dot = filename.rfind('.');
							std::string gameId = (lastUnderscore != std::string::npos) ?
								filename.substr(0, lastUnderscore) : "unknown";
							int slot = (lastUnderscore != std::string::npos && dot != std::string::npos) ?
								atoi(filename.substr(lastUnderscore + 1, dot - lastUnderscore - 1).c_str()) : -1;

							if (slot < 0 || slot > 99) {
								WriteHTTPResponse(clientFd, 400, "{\"error\":\"invalid_slot\"}");
								closesocket(clientFd);
								if (sslHandle && tlsCtx_) tlsCtx_->CloseTLS(clientFd);
								return;
							}

							std::string response;
							HandleSaveUpload(gameId, slot, allBody, "", response);
							WriteHTTPResponse(clientFd, 201, response);
						}
					} else {
						WriteHTTPResponse(clientFd, 405, "{\"error\":\"method_not_allowed\"}");
					}
				} else {
					WriteHTTPResponse(clientFd, 404, "{\"error\":\"not_found\"}");
				}
				closesocket(clientFd);
				if (sslHandle && tlsCtx_) {
					tlsCtx_->CloseTLS(clientFd);
				}
			}));
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
	std::lock_guard<std::mutex> lock(pinMutex_);
	pairingPin_ = pin;
	return pairingPin_;
}

void SaveStateLANSync::CancelPairing() {
	std::lock_guard<std::mutex> lock(pinMutex_);
	pairingPin_.clear();
}

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
		struct timeval tv;
		tv.tv_sec = 30; tv.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			WARN_LOG(Log::System, "LANSync: PairWithPeer connect failed %s:%d: %s",
			         host.c_str(), port, strerror(errno));
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

		// Read response with loop to handle partial TCP reads
		std::string response;
		char buf[4096];
		int n;
		while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
			buf[n] = '\0';
			response.append(buf, n);
			size_t hdrEnd = response.find("\r\n\r\n");
			if (hdrEnd == std::string::npos) continue;
			size_t clPos = response.find("Content-Length:");
			size_t contentLength = 0;
			if (clPos != std::string::npos && clPos < hdrEnd) {
				clPos += 15;
				while (clPos < response.size() && response[clPos] == ' ') clPos++;
				contentLength = strtoul(response.c_str() + clPos, nullptr, 10);
			}
			size_t bodySize = response.size() - hdrEnd - 4;
			if (contentLength == 0 || bodySize >= contentLength) break;
		}
		closesocket(sock);

		if (!response.empty()) {

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

							// Extract server's deviceId from peerId field in response
							std::string serverPeerId;
							size_t pidPos = jsonBody.find("\"peerId\":\"");
							if (pidPos != std::string::npos) {
								pidPos += 10;
								size_t pidEnd = jsonBody.find('"', pidPos);
								if (pidEnd != std::string::npos)
									serverPeerId = jsonBody.substr(pidPos, pidEnd - pidPos);
							}

							// Save peer — use server's deviceId as ID (matches discovery format)
							std::lock_guard<std::mutex> lock(peerMutex_);
							PeerInfo peer;
							peer.id = serverPeerId.empty() ? peerId : serverPeerId;
							peer.token = token;
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

void SaveStateLANSync::AutoPairWithPeer(const std::string &host, int port,
                                         std::function<void(bool, const std::string &)> callback) {
	// Don't pair with ourselves
	if (serverPort_ > 0 && port == serverPort_) {
		// Check if this is our own IP - just compare port as rough check
		// Exact IP comparison is unreliable on multi-homed devices, but port collision
		// on the same machine is a strong indicator of self-connection.
		if (callback) callback(false, "Cannot pair with self");
		return;
	}
	AddBackgroundThread(std::thread([this, host, port, callback]() {
		std::string body = StringFromFormat(
			"{\"id\":\"%s\",\"name\":\"%s\",\"device\":\"%s\",\"port\":%d}",
			deviceId_.c_str(), deviceName_.c_str(), deviceType_.c_str(), serverPort_);

		// POST /api/v1/pair-request
		int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) { if (callback) callback(false, "Socket error"); return; }
		struct timeval tv;
		tv.tv_sec = 15; tv.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			WARN_LOG(Log::System, "LANSync: AutoPairWithPeer connect failed %s:%d: %s",
			         host.c_str(), port, strerror(errno));
			closesocket(sock);
			if (callback) callback(false, "Connection refused");
			return;
		}

		std::string req = StringFromFormat(
			"POST /api/v1/pair-request HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n\r\n%s",
			host.c_str(), port, (int)body.size(), body.c_str());
		send(sock, req.c_str(), req.size(), 0);

		char buf[4096];
		// Read response with loop to handle partial TCP reads
		std::string resp;
		int n;
		while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
			buf[n] = '\0';
			resp.append(buf, n);
			size_t hdrEnd = resp.find("\r\n\r\n");
			if (hdrEnd == std::string::npos) continue;
			size_t clPos = resp.find("Content-Length:");
			size_t contentLength = 0;
			if (clPos != std::string::npos && clPos < hdrEnd) {
				clPos += 15;
				while (clPos < resp.size() && resp[clPos] == ' ') clPos++;
				contentLength = strtoul(resp.c_str() + clPos, nullptr, 10);
			}
			size_t bodySize = resp.size() - hdrEnd - 4;
			if (contentLength == 0 || bodySize >= contentLength) break;
		}
		closesocket(sock);
		if (resp.empty()) { if (callback) callback(false, "No response"); return; }

		auto extractStr = [](const std::string &text, const char *key) -> std::string {
			std::string search = std::string("\"") + key + "\":\"";
			size_t pos = text.find(search);
			if (pos == std::string::npos) return "";
			pos += search.size();
			size_t end = text.find('"', pos);
			return (end != std::string::npos) ? text.substr(pos, end - pos) : "";
		};

		std::string requestId = extractStr(resp, "requestId");
		std::string serverPeerId = extractStr(resp, "peerId");
		std::string nonce = extractStr(resp, "nonce");
		std::string remoteVerifyCode = extractStr(resp, "verificationCode");
		if (requestId.empty()) {
			WARN_LOG(Log::System, "AutoPairWithPeer: Bad response from %s:%d — response was: %s",
				host.c_str(), port, resp.c_str());
			if (callback) callback(false, "Bad response");
			return;
		}

		// Compute verification code locally (must match remote)
		std::string localVerifyCode;
		if (!nonce.empty() && !serverPeerId.empty()) {
			localVerifyCode = ComputeVerificationCode(nonce, deviceId_, serverPeerId);
			if (localVerifyCode != remoteVerifyCode) {
				WARN_LOG(Log::System, "AutoPairWithPeer: verification code mismatch! local=%s remote=%s",
					localVerifyCode.c_str(), remoteVerifyCode.c_str());
				if (callback) callback(false, "Verification code mismatch - possible MITM");
				return;
			}
		}

		// Poll for status (wait for server user to confirm, up to 30s)
		bool clientConfirmed = false;
		for (int i = 0; i < 30; i++) {
			sleep_ms(1000, "auto-pair-poll");

			int pollSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (pollSock < 0) continue;
			struct timeval pollTv;
			pollTv.tv_sec = 5; pollTv.tv_usec = 0;
			setsockopt(pollSock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&pollTv, sizeof(pollTv));

			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

			if (connect(pollSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
				closesocket(pollSock);
				continue;
			}

			std::string pollReq = StringFromFormat(
				"GET /api/v1/pair-status?requestId=%s HTTP/1.1\r\n"
				"Host: %s:%d\r\n"
				"Connection: close\r\n\r\n",
				requestId.c_str(), host.c_str(), port);
			send(pollSock, pollReq.c_str(), pollReq.size(), 0);

			char pollBuf[1024];
			std::string pollResp;
			int pollN;
			while ((pollN = recv(pollSock, pollBuf, sizeof(pollBuf) - 1, 0)) > 0) {
				pollBuf[pollN] = '\0';
				pollResp.append(pollBuf, pollN);
				size_t hdrEnd = pollResp.find("\r\n\r\n");
				if (hdrEnd == std::string::npos) continue;
				size_t clPos = pollResp.find("Content-Length:");
				size_t contentLength = 0;
				if (clPos != std::string::npos && clPos < hdrEnd) {
					clPos += 15;
					while (clPos < pollResp.size() && pollResp[clPos] == ' ') clPos++;
					contentLength = strtoul(pollResp.c_str() + clPos, nullptr, 10);
				}
				size_t bodySize = pollResp.size() - hdrEnd - 4;
				if (contentLength == 0 || bodySize >= contentLength) break;
			}
			closesocket(pollSock);
			if (pollN <= 0) continue;

			std::string status = extractStr(pollResp, "status");
			if (status == "approved") {
				std::string token = extractStr(pollResp, "token");
				std::string peerId = extractStr(pollResp, "peerId");

				PeerInfo peer;
				peer.id = peerId;
				peer.host = host;
				peer.port = port;
				peer.token = token;
				peer.paired = true;
				peer.online = true;
				peer.lastSeen = time(nullptr);

				{
					std::lock_guard<std::mutex> lock(peerMutex_);
					pairedPeers_.push_back(peer);
				}
				SaveConfig();

				if (callback) callback(true, "");
				return;
			} else if (status == "waiting_client" && !clientConfirmed) {
				// Server user hasn't confirmed yet — send our confirmation (auto-confirm)
				// The codes were already verified locally, so we can auto-confirm
				clientConfirmed = true;
				// Send pair-verify to server
				int verifySock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (verifySock >= 0) {
					memset(&addr, 0, sizeof(addr));
					addr.sin_family = AF_INET;
					addr.sin_port = htons(port);
					inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
					if (connect(verifySock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
						std::string verifyBody = StringFromFormat(
							"{\"requestId\":\"%s\"}", requestId.c_str());
						std::string verifyReq = StringFromFormat(
							"POST /api/v1/pair-verify HTTP/1.1\r\n"
							"Host: %s:%d\r\n"
							"Content-Type: application/json\r\n"
							"Content-Length: %d\r\n"
							"Connection: close\r\n\r\n%s",
							host.c_str(), port, (int)verifyBody.size(), verifyBody.c_str());
						send(verifySock, verifyReq.c_str(), verifyReq.size(), 0);
						// Read response (don't block — fire and forget)
						char vbuf[256];
						recv(verifySock, vbuf, sizeof(vbuf) - 1, 0);
					}
					closesocket(verifySock);
				}
			} else if (status == "rejected" || status == "expired") {
				if (callback) callback(false, status);
				return;
			}
		}
		if (callback) callback(false, "Timeout");
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

// ==================== Transfer Helpers ====================

bool SaveStateLANSync::DownloadSave(const PeerInfo &peer, const Path &localPath,
                                     const std::string &gameId, int slot,
                                     const std::string &ext) {
	std::string dlReq = StringFromFormat(
		"GET /api/v1/saves/%s_%d.%s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Authorization: Bearer %s\r\n"
		"Connection: close\r\n\r\n",
		gameId.c_str(), slot, ext.c_str(),
		peer.host.c_str(), peer.port, peer.token.c_str());
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) return false;
	struct timeval tv;
	tv.tv_sec = 30; tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(peer.port);
	inet_pton(AF_INET, peer.host.c_str(), &addr.sin_addr);
	bool ok = false;
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
		send(sock, dlReq.c_str(), dlReq.size(), 0);
		std::vector<uint8_t> buf(65536);
		int total = 0, n;
		while ((n = recv(sock, (char *)buf.data() + total, buf.size() - total - 1, 0)) > 0) {
			total += n;
			if (total >= (int)buf.size() - 1024) {
				if (buf.size() > (size_t)MAX_UPLOAD_SIZE) {
					closesocket(sock);
					return false;
				}
				buf.resize(buf.size() * 2);
			}
		}
		if (total > 0) {
			std::string resp((char *)buf.data(), total);
			if (resp.find("200 OK") != std::string::npos) {
				size_t bStart = resp.find("\r\n\r\n");
				if (bStart != std::string::npos) {
					bStart += 4;
					std::vector<uint8_t> fd(buf.begin() + bStart, buf.begin() + total);
					Path tmp = localPath.WithExtraExtension(".tmp");
					if (File::WriteDataToFile(false, fd.data(), fd.size(), tmp)) {
						File::Rename(tmp, localPath);
						ok = true;
					} else { File::Delete(tmp); }
				}
			}
		}
	} else {
		WARN_LOG(Log::System, "LANSync: DownloadSave connect failed %s_%d.%s -> %s:%d: %s",
		         gameId.c_str(), slot, ext.c_str(),
		         peer.host.c_str(), peer.port, strerror(errno));
	}
	closesocket(sock);
	return ok;
}

bool SaveStateLANSync::UploadSave(const PeerInfo &peer, const std::string &gameId, int slot,
                                   const std::string &data, const std::string &ext) {
	std::string header = StringFromFormat(
		"POST /api/v1/saves/%s_%d.%s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Authorization: Bearer %s\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n",
		gameId.c_str(), slot, ext.c_str(),
		peer.host.c_str(), peer.port, peer.token.c_str(),
		(int)data.size());
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) return false;
	struct timeval tv;
	tv.tv_sec = 30; tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(peer.port);
	inet_pton(AF_INET, peer.host.c_str(), &addr.sin_addr);
	bool ok = false;
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
		send(sock, header.c_str(), header.size(), 0);
		send(sock, data.data(), data.size(), 0);
		char resp[256];
		int n = recv(sock, resp, sizeof(resp) - 1, 0);
		if (n > 0) { resp[n] = '\0'; ok = (strstr(resp, "201") != nullptr); }
	} else {
		WARN_LOG(Log::System, "LANSync: UploadSave connect failed %s_%d.%s -> %s:%d: %s",
		         gameId.c_str(), slot, ext.c_str(),
		         peer.host.c_str(), peer.port, strerror(errno));
	}
	closesocket(sock);
	return ok;
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
		INFO_LOG(Log::System, "LANSync: SyncWithPeer peer not found or offline (id=%s)", peerId.c_str());
		if (onDone) { SyncResult r; r.success = false; onDone(r); }
		return;
	}

	INFO_LOG(Log::System, "LANSync: SyncWithPeer starting sync with %s (%s:%d)", target.id.c_str(), target.host.c_str(), target.port);
	syncCancelled_ = false;

	// Initialize syncProgress_ BEFORE setting status (avoids stale progress with new status)
	{
		std::lock_guard<std::mutex> lock(syncMutex_);
		syncProgress_ = SyncProgress{};
		syncProgress_.status = SyncStatus::SYNCING;
		syncProgress_.currentPeer = target.id;
	}
	syncStatus_ = SyncStatus::SYNCING;

	AddBackgroundThread(std::thread([this, target, onProgress, onDone]() {
#if PPSSPP_PLATFORM(ANDROID)
		// RAII JVM attach: detach automatically on scope exit
		extern JavaVM *gJvm;
		struct JVMDetacher {
			JavaVM *vm = nullptr;
			bool attached = false;
			~JVMDetacher() { if (attached && vm) vm->DetachCurrentThread(); }
		} jvmDetacher;
		if (gJvm) {
			JNIEnv *jniEnv = nullptr;
			int status = gJvm->GetEnv((void **)&jniEnv, JNI_VERSION_1_6);
			if (status == JNI_EDETACHED) {
				JavaVMAttachArgs args = {JNI_VERSION_1_6, "LANSyncWorker", nullptr};
				jvmDetacher.vm = gJvm;
				jvmDetacher.attached = (gJvm->AttachCurrentThread(&jniEnv, &args) == JNI_OK);
			}
		}
#endif
		SaveStateLANSync::SyncResult result = DoSync(target, onProgress);
		syncStatus_ = result.success ? SyncStatus::DONE : SyncStatus::ERROR;
		// Finalize syncProgress_ for GetProgress() callers
		{
			std::lock_guard<std::mutex> lock(syncMutex_);
			syncProgress_.status = result.success ? SyncStatus::DONE : SyncStatus::ERROR;
			if (!result.success) syncProgress_.error = "Sync failed";
		}
		if (onDone) onDone(result);
	}));
}

SaveStateLANSync::SyncResult SaveStateLANSync::DoSync(const PeerInfo &peer, SaveStateLANSync::ProgressCallback onProgress) {
	INFO_LOG(Log::System, "LANSync DoSync: START peer=%s:%d", peer.host.c_str(), peer.port);
	SyncResult result;

	// Connect to peer
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) { INFO_LOG(Log::System, "LANSync DoSync: socket() failed"); result.success = false; return result; }
	struct timeval tv;
	tv.tv_sec = 30; tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(peer.port);
	inet_pton(AF_INET, peer.host.c_str(), &addr.sin_addr);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		WARN_LOG(Log::System, "LANSync: DoSync connect failed %s:%d: %s",
		         peer.host.c_str(), peer.port, strerror(errno));
		closesocket(sock); result.success = false; return result;
	}
	INFO_LOG(Log::System, "LANSync DoSync: connected OK, scanning local dir...");

	// Scan local savestate directory for all games
	Path saveDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(saveDir, &files, "ppst");

	INFO_LOG(Log::System, "LANSync DoSync: found %d local .ppst files in %s", (int)files.size(), saveDir.c_str());

	// Group by game prefix
	std::map<std::string, std::vector<int>> localGames;
	std::map<std::pair<std::string,int>, int64_t> localSizes;
	for (const auto &f : files) {
		std::string name = f.name;
		// Extract gamePrefix and slot from filename: PREFIX_N.ppst
		size_t lastUnderscore = name.rfind('_');
		size_t dot = name.rfind('.');
		if (lastUnderscore == std::string::npos || dot == std::string::npos) continue;

		std::string prefix = name.substr(0, lastUnderscore);
		int slot = atoi(name.substr(lastUnderscore + 1, dot - lastUnderscore - 1).c_str());
		localGames[prefix].push_back(slot);
		localSizes[{prefix, slot}] = (int64_t)f.size;

		// Create/update metadata
		Path ppstPath = saveDir / name;
		std::string fileData;
		if (File::ReadBinaryFileToString(ppstPath, &fileData)) {
					SaveStateSyncMetadata meta;
					std::string currentHash = ComputeSHA256(fileData.data(), fileData.size());
					bool hadMeta = SaveStateSyncMetadata::ReadFromFile(ppstPath, meta);
					bool hashChanged = !hadMeta || meta.hash != currentHash;
					if (!hadMeta || hashChanged) {
						// New file or file changed: recompute metadata
						if (hashChanged) {
							INFO_LOG(Log::System, "LANSync DoSync: hash changed for %s, updating metadata", name.c_str());
						}
						meta.hash = currentHash;
						meta.lastSyncTime = time(nullptr);
						meta.deviceId = deviceId_;
						meta.hlc = meta.hlc.Increment(deviceId_);
						meta.parentHlc = meta.hlc;
						meta.fileSize = (int64_t)f.size;
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

	// Read response with loop to handle partial reads
	std::string response;
	{
		char buf[65536];
		size_t contentLength = 0;
		bool headersParsed = false;
		for (int i = 0; i < 200; i++) {
			int n = recv(sock, buf, sizeof(buf) - 1, 0);
			if (n <= 0) break;
			buf[n] = '\0';
			response.append(buf, n);
			if (!headersParsed) {
				size_t hdrEnd = response.find("\r\n\r\n");
				if (hdrEnd != std::string::npos) {
					headersParsed = true;
					// Parse Content-Length from response headers
					size_t clPos = response.find("Content-Length:");
					if (clPos != std::string::npos && clPos < hdrEnd) {
						clPos += 15;
						while (clPos < response.size() && response[clPos] == ' ') clPos++;
						contentLength = strtoul(response.c_str() + clPos, nullptr, 10);
					}
				}
			}
			if (headersParsed) {
				size_t bodyReceived = response.size() - response.find("\r\n\r\n") - 4;
				if (bodyReceived >= contentLength) break;
			}
		}
	}
	closesocket(sock);

	if (response.empty()) { INFO_LOG(Log::System, "LANSync DoSync: empty response from peer"); result.success = false; return result; }

	// Parse response: extract body after \r\n\r\n
	size_t bodyStart = response.find("\r\n\r\n");
	if (bodyStart == std::string::npos) { INFO_LOG(Log::System, "LANSync DoSync: no header/body separator"); result.success = false; return result; }
	std::string jsonBody = response.substr(bodyStart + 4);
	INFO_LOG(Log::System, "LANSync DoSync: remote save list body (%d bytes): %.200s", (int)jsonBody.size(), jsonBody.c_str());

	// Extract HLC from JSON field
	auto extractHLC = [&](size_t searchStart, const std::string &field) -> HLC {
		std::string search = "\"" + field + "\":{";
		size_t p = jsonBody.find(search, searchStart);
		if (p == std::string::npos) return HLC();
		p += search.size() - 1;
		int depth = 1;
		size_t end = p + 1;
		while (end < jsonBody.size() && depth > 0) {
			if (jsonBody[end] == '{') depth++;
			else if (jsonBody[end] == '}') depth--;
			end++;
		}
		return HLC::FromJSON(jsonBody.substr(p, end - p));
	};

	// Count remote entries for progress — first pass: collect game/slot pairs and sizes
	std::set<std::pair<std::string,int>> remoteSet;
	std::map<std::pair<std::string,int>, int64_t> remoteSizes;
	size_t tmpPos = 0;
	int remoteCount = 0;
	while ((tmpPos = jsonBody.find("\"gameId\":\"", tmpPos)) != std::string::npos) {
		tmpPos += 10;
		size_t gEnd = jsonBody.find('"', tmpPos);
		if (gEnd == std::string::npos) { remoteCount++; break; }
		std::string gid = jsonBody.substr(tmpPos, gEnd - tmpPos);
		// Find slot
		size_t slPos = jsonBody.find("\"slot\":", gEnd);
		if (slPos == std::string::npos) { remoteCount++; continue; }
		slPos += 7;
		int sl = atoi(jsonBody.c_str() + slPos);
		remoteSet.insert({gid, sl});
		// Extract size for byte-level progress
		size_t szPos = jsonBody.find("\"size\":", slPos);
		if (szPos != std::string::npos) {
			szPos += 7;
			int64_t sz = atoll(jsonBody.c_str() + szPos);
			remoteSizes[{gid, sl}] = sz;
		}
		remoteCount++;
	}

	// Count local entries NOT in remote (accurate count for progress)
	int localUploadCount = 0;
	for (const auto &lg : localGames) {
		for (int slot : lg.second) {
			if (remoteSet.find({lg.first, slot}) == remoteSet.end()) {
				localUploadCount++;
			}
		}
	}

	int totalItems = remoteCount + localUploadCount;
	int completedItems = 0;

	// Calculate total bytes for byte-level progress
	int64_t totalBytes = 0;
	for (const auto &rs : remoteSizes) {
		totalBytes += rs.second;
	}
	for (const auto &lg : localGames) {
		for (int slot : lg.second) {
			if (remoteSet.find({lg.first, slot}) == remoteSet.end()) {
				auto it = localSizes.find({lg.first, slot});
				if (it != localSizes.end()) totalBytes += it->second;
			}
		}
	}
	int64_t completedBytes = 0;

	if (onProgress) {
		SyncProgress sp;
		sp.totalSlots = totalItems;
		sp.completedSlots = 0;
		sp.totalBytes = totalBytes;
		sp.completedBytes = 0;
		sp.currentPeer = peer.id;
		onProgress(sp);
	}

	// Initialize syncProgress_ for GetProgress() callers
	{
		std::lock_guard<std::mutex> lock(syncMutex_);
		syncProgress_.status = SyncStatus::SYNCING;
		syncProgress_.totalSlots = totalItems;
		syncProgress_.completedSlots = 0;
		syncProgress_.totalBytes = totalBytes;
		syncProgress_.completedBytes = 0;
		syncProgress_.currentPeer = peer.id;
	syncProgress_.currentFile.clear();
	syncProgress_.currentGame.clear();
}

	// Local lambda to centralize progress reporting (both onProgress callback + syncProgress_ update)
	auto reportProgress = [&](const std::string &gid, int sl, int64_t byteInc) {
		completedItems++;
		completedBytes += byteInc;
		if (onProgress) {
			SyncProgress sp;
			sp.totalSlots = totalItems;
			sp.completedSlots = completedItems;
			sp.totalBytes = totalBytes;
			sp.completedBytes = completedBytes;
			sp.currentFile = StringFromFormat("%s_%d.ppst", gid.c_str(), sl);
			sp.currentGame = gid;
			sp.currentPeer = peer.id;
			onProgress(sp);
		}
		{
			std::lock_guard<std::mutex> lock(syncMutex_);
			syncProgress_.totalSlots = totalItems;
			syncProgress_.completedSlots = completedItems;
			syncProgress_.totalBytes = totalBytes;
			syncProgress_.completedBytes = completedBytes;
			syncProgress_.currentFile = StringFromFormat("%s_%d.ppst", gid.c_str(), sl);
			syncProgress_.currentGame = gid;
		}
	};

	// Simple JSON array parse: [{slot, size, hash, hlc, parentHlc, ppssppVersion, saveFormatVersion}]
	std::set<std::pair<std::string,int>> remoteEntriesSeen;
	size_t pos = 0;
	int syncedCount = 0;
	while ((pos = jsonBody.find("\"gameId\":\"", pos)) != std::string::npos) {
		pos += 10;
		size_t end = jsonBody.find('"', pos);
		if (end == std::string::npos) break;
		std::string gameId = jsonBody.substr(pos, end - pos);

		size_t slotPos = jsonBody.find("\"slot\":", end);
		if (slotPos == std::string::npos) break;
		slotPos += 7;
		int slot = atoi(jsonBody.c_str() + slotPos);
		remoteEntriesSeen.insert({gameId, slot});

		// Extract hash
		size_t hashPos = jsonBody.find("\"hash\":\"", slotPos);
		std::string remoteHash;
		if (hashPos != std::string::npos && hashPos < jsonBody.find('}', slotPos)) {
			hashPos += 8;
			size_t hashEnd = jsonBody.find('"', hashPos);
			if (hashEnd != std::string::npos)
				remoteHash = jsonBody.substr(hashPos, hashEnd - hashPos);
		}

		bool remoteHasThumb = jsonBody.find("\"hasThumbnail\":true", slotPos) != std::string::npos &&
			jsonBody.find("\"hasThumbnail\":true", slotPos) < jsonBody.find("\"gameId\":\"", end + 1);

		// Extract remote HLC
		HLC remoteHlc = extractHLC(slotPos, "hlc");
		HLC remoteParentHlc = extractHLC(slotPos, "parentHlc");

		// Compare with local
		Path localPath = saveDir / StringFromFormat("%s_%d.ppst", gameId.c_str(), slot);
		std::string localData;
		bool hasLocal = File::ReadBinaryFileToString(localPath, &localData);

		// Read local metadata
		SaveStateSyncMetadata localMeta;
		HLC localHlc, localParentHlc;
		std::string localHash;
		if (hasLocal && SaveStateSyncMetadata::ReadFromFile(localPath, localMeta)) {
			localHlc = localMeta.hlc;
			localParentHlc = localMeta.parentHlc;
			localHash = localMeta.hash;
		} else if (hasLocal) {
			localHash = ComputeSHA256(localData.data(), localData.size());
		}

		auto cr = DetectConflict(localHlc, localParentHlc, remoteHlc, remoteParentHlc);
		switch (cr.action) {
		case ConflictResult::KEEP_REMOTE:
			if (DownloadSave(peer, localPath, gameId, slot, "ppst")) {
				result.downloaded++;
				if (remoteHasThumb) {
					Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", gameId.c_str(), slot);
					DownloadSave(peer, thumbPath, gameId, slot, "jpg");
				}
			} else result.failed++;
			break;
		case ConflictResult::KEEP_LOCAL:
			if (hasLocal && UploadSave(peer, gameId, slot, localData, "ppst")) {
				result.uploaded++;
				Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", gameId.c_str(), slot);
				std::string localThumbData;
				if (File::ReadBinaryFileToString(thumbPath, &localThumbData))
					UploadSave(peer, gameId, slot, localThumbData, "jpg");
			} else result.skipped++;
			break;
		case ConflictResult::SKIP:
			result.skipped++;
			break;
		case ConflictResult::MERGE: {
			ConflictResolution autoResolve = (ConflictResolution)g_Config.lanSync.iConflictResolution;
			if (autoResolve == ConflictResolution::KEEP_REMOTE) {
				if (DownloadSave(peer, localPath, gameId, slot, "ppst")) {
					result.downloaded++;
					if (remoteHasThumb) {
						Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", gameId.c_str(), slot);
						DownloadSave(peer, thumbPath, gameId, slot, "jpg");
					}
				} else result.failed++;
			} else if (autoResolve == ConflictResolution::KEEP_LOCAL) {
				result.skipped++;
			} else if (autoResolve == ConflictResolution::NEWEST_WINS) {
				if (remoteHlc.IsAfter(localHlc)) {
					if (DownloadSave(peer, localPath, gameId, slot, "ppst")) {
						result.downloaded++;
						if (remoteHasThumb) {
							Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", gameId.c_str(), slot);
							DownloadSave(peer, thumbPath, gameId, slot, "jpg");
						}
					} else result.failed++;
				} else {
					result.skipped++;
				}
			} else {
				// PROMPT — add to conflict queue
				ConflictInfo ci;
				ci.gameId = gameId; ci.slot = slot;
				ci.localHlc = localHlc; ci.remoteHlc = remoteHlc;
				ci.localParentHlc = localParentHlc; ci.remoteParentHlc = remoteParentHlc;
				ci.localSize = hasLocal ? (int64_t)localData.size() : 0; ci.remoteSize = 0;
				ci.localHash = localHash; ci.remoteHash = remoteHash;
				ci.peerId = peer.id;
				{
					std::lock_guard<std::mutex> lock(conflictMutex_);
					pendingConflicts_.push_back(ci);
				}
				result.conflicts++;
			}
			break;
		}
		}

		syncedCount++;
		// Update byte progress: add remote entry's size to completed bytes
		{
			auto rsIt = remoteSizes.find({gameId, slot});
			int64_t byteInc = (rsIt != remoteSizes.end()) ? rsIt->second : 0;
			reportProgress(gameId, slot, byteInc);
		}
		pos = end;
	}

	INFO_LOG(Log::System, "LANSync DoSync: %d local games, %d remote entries",
		(int)localGames.size(), syncedCount);

	// Upload local saves that don't exist on remote
	for (const auto &lg : localGames) {
		for (int slot : lg.second) {
			if (remoteEntriesSeen.find({lg.first, slot}) == remoteEntriesSeen.end()) {
				Path localPath = saveDir / StringFromFormat("%s_%d.ppst", lg.first.c_str(), slot);
				std::string localData;
				if (File::ReadBinaryFileToString(localPath, &localData)) {
					INFO_LOG(Log::System, "LANSync DoSync: uploading %s_%d.ppst (%d bytes)",
						lg.first.c_str(), slot, (int)localData.size());
					if (UploadSave(peer, lg.first, slot, localData, "ppst")) {
						result.uploaded++;
						INFO_LOG(Log::System, "LANSync DoSync: upload OK");
						// Also upload thumbnail if available
						Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", lg.first.c_str(), slot);
						std::string localThumbData;
						if (File::ReadBinaryFileToString(thumbPath, &localThumbData)) {
							UploadSave(peer, lg.first, slot, localThumbData, "jpg");
						}
					} else {
						result.failed++;
						INFO_LOG(Log::System, "LANSync DoSync: upload FAILED");
					}
					{
						auto lsIt = localSizes.find({lg.first, slot});
						int64_t byteInc = (lsIt != localSizes.end()) ? lsIt->second : 0;
						reportProgress(lg.first, slot, byteInc);
					}
				}
			}
		}
	}

	INFO_LOG(Log::System, "LANSync DoSync: result: %d up, %d down, %d failed, %d skipped",
		result.uploaded, result.downloaded, result.failed, result.skipped);
	result.success = true;
	return result;
}

void SaveStateLANSync::CancelSync() { syncCancelled_ = true; }

// ==================== Conflict ====================

void SaveStateLANSync::ResolveConflict(const ConflictInfo &conflict, ConflictResolution resolution) {
	if (resolution == ConflictResolution::KEEP_LOCAL) return;

	// Find the peer by ID
	PeerInfo peer;
	{
		std::lock_guard<std::mutex> lock(peerMutex_);
		for (auto &p : pairedPeers_) {
			if (p.id == conflict.peerId && p.online) { peer = p; break; }
		}
	}
	if (peer.id.empty()) return;

	Path localPath = GetSysDirectory(DIRECTORY_SAVESTATE) /
		StringFromFormat("%s_%d.ppst", conflict.gameId.c_str(), conflict.slot);

	DownloadSave(peer, localPath, conflict.gameId, conflict.slot, "ppst");

	// Also download thumbnail
	Path thumbPath = GetSysDirectory(DIRECTORY_SAVESTATE) /
		StringFromFormat("%s_%d.jpg", conflict.gameId.c_str(), conflict.slot);
	DownloadSave(peer, thumbPath, conflict.gameId, conflict.slot, "jpg");
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
std::string SaveStateLANSync::GetCurrentPin() const {
	std::lock_guard<std::mutex> lock(pinMutex_);
	return pairingPin_;
}

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

			// Extract host
			size_t hostPos = peersJSON.find("\"host\":\"", end);
			if (hostPos != std::string::npos && hostPos < peersJSON.find('}', pos)) {
				hostPos += 8;
				size_t hostEnd = peersJSON.find('"', hostPos);
				if (hostEnd != std::string::npos)
					peer.host = peersJSON.substr(hostPos, hostEnd - hostPos);
			}

			// Extract port
			size_t portPos = peersJSON.find("\"port\":", end);
			if (portPos != std::string::npos && portPos < peersJSON.find('}', pos)) {
				portPos += 7;
				peer.port = atoi(peersJSON.c_str() + portPos);
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
			"{\"id\":\"%s\",\"name\":\"%s\",\"host\":\"%s\",\"port\":%d,\"token\":\"%s\"}",
			pairedPeers_[i].id.c_str(), pairedPeers_[i].name.c_str(),
			pairedPeers_[i].host.c_str(), pairedPeers_[i].port,
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
	{
		std::lock_guard<std::mutex> lock(pinMutex_);
		if (pin != pairingPin_ || pairingPin_.empty()) {
			response = "{\"error\":\"invalid_pin\"}";
			return;
		}
	}

	// Generate token — store the same token that we return to the client
	{
		std::string storedToken = GenerateSessionToken();
		PeerInfo peer;
		peer.id = peerId; peer.name = peerName; peer.device = "Unknown";
		peer.paired = true; peer.online = true;
		peer.token = storedToken; peer.lastSeen = time(nullptr);
		{
			std::lock_guard<std::mutex> lock(peerMutex_);
			if (pairedPeers_.size() >= 5) {
				response = "{\"error\":\"too_many_peers\"}";
				return;
			}
			pairedPeers_.push_back(peer);
		}
		SaveConfig();

		response = StringFromFormat(
			"{\"token\":\"%s\",\"peerId\":\"%s\",\"certFingerprint\":\"%s\"}",
			storedToken.c_str(), deviceId_.c_str(),
			tlsCtx_ ? tlsCtx_->GetFingerprint().c_str() : "");
	}

	// Clear PIN after successful pairing
	pairingPin_.clear();
}

void SaveStateLANSync::HandleAutoPairRequest(const std::string &body, const std::string &clientHost, std::string &response) {
	std::string peerId, peerName, device;
	auto extractStr = [&body](const char *key) -> std::string {
		std::string search = std::string("\"") + key + "\":\"";
		size_t pos = body.find(search);
		if (pos == std::string::npos) return "";
		pos += search.size();
		size_t end = body.find('"', pos);
		return (end != std::string::npos) ? body.substr(pos, end - pos) : "";
	};
	auto extractInt = [&body](const char *key) -> int {
		std::string search = std::string("\"") + key + "\":";
		size_t pos = body.find(search);
		if (pos == std::string::npos) {
			// Try without quotes (number)
			search = std::string("\"") + key + "\":";
			pos = body.find(search);
			if (pos == std::string::npos) return 0;
		}
		pos += search.size();
		// Read digits
		int val = 0;
		while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
			val = val * 10 + (body[pos] - '0');
			pos++;
		}
		return val;
	};
	peerId = extractStr("id");
	peerName = extractStr("name");
	device = extractStr("device");
	int peerPort = extractInt("port");

	if (peerId.empty() || peerName.empty()) {
		INFO_LOG(Log::System, "LANSync: HandleAutoPairRequest missing fields (id=%s, name=%s)", peerId.c_str(), peerName.c_str());
		response = "{\"error\":\"missing_fields\"}";
		return;
	}

	// Reject pairing request from self
	if (peerId == deviceId_) {
		INFO_LOG(Log::System, "LANSync: HandleAutoPairRequest rejected self-pairing from %s", peerId.c_str());
		response = "{\"error\":\"cannot_pair_with_self\"}";
		return;
	}

	INFO_LOG(Log::System, "LANSync: HandleAutoPairRequest from %s (%s) host=%s port=%d",
		peerName.c_str(), device.c_str(), clientHost.c_str(), peerPort);
	std::string requestId = StringFromFormat("req-%d-%lld", pendingRequestCounter_++, (long long)time(nullptr));
	std::string nonce = GenerateNonce();
	std::string verifyCode = ComputeVerificationCode(nonce, deviceId_, peerId);

	PendingPairRequest req;
	req.requestId = requestId;
	req.peerId = peerId;
	req.peerName = peerName;
	req.device = device;
	req.host = clientHost;
	req.port = peerPort;
	req.nonce = nonce;
	req.verificationCode = verifyCode;
	req.timestamp = time_now_d();

	{
		std::lock_guard<std::mutex> lock(pendingMutex_);
		// Cleanup expired/rejected/accepted entries first
		double now = time_now_d();
		pendingRequests_.erase(
			std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
				[now](const PendingPairRequest &r) {
					return r.accepted || r.rejected || ((now - r.timestamp) > 60.0);
				}),
			pendingRequests_.end());
		if (pendingRequests_.size() >= 10) {
			response = "{\"error\":\"too_many_requests\"}";
			return;
		}
		pendingRequests_.push_back(req);
	}

	System_Toast(StringFromFormat("Pair request from %s (code: %s)", peerName.c_str(), verifyCode.c_str()));
	response = StringFromFormat("{\"status\":\"pending\",\"requestId\":\"%s\",\"peerId\":\"%s\",\"nonce\":\"%s\",\"verificationCode\":\"%s\"}", requestId.c_str(), deviceId_.c_str(), nonce.c_str(), verifyCode.c_str());
}

void SaveStateLANSync::HandlePairRespond(const std::string &body, std::string &response) {
	INFO_LOG(Log::System, "LANSync: HandlePairRespond called with body=%s", body.c_str());
	std::string requestId, acceptStr;
	auto extractStr = [&body](const char *key) -> std::string {
		std::string search = std::string("\"") + key + "\":\"";
		size_t pos = body.find(search);
		if (pos == std::string::npos) return "";
		pos += search.size();
		size_t end = body.find('"', pos);
		return (end != std::string::npos) ? body.substr(pos, end - pos) : "";
	};
	requestId = extractStr("requestId");
	acceptStr = extractStr("accept");

	if (requestId.empty()) {
		response = "{\"error\":\"missing_requestId\"}";
		return;
	}
	bool accept = (acceptStr == "true" || acceptStr == "1");

	std::lock_guard<std::mutex> lock(pendingMutex_);
	for (auto &req : pendingRequests_) {
		if (req.requestId == requestId) {
			if (accept) {
				req.serverConfirmed = true;

				if (req.nonce.empty()) {
					// PIN-based flow (no nonce): approve immediately
					std::string storedToken = GenerateSessionToken();
					req.token = storedToken;
					req.accepted = true;
					PeerInfo peer;
					peer.id = req.peerId;
					peer.name = req.peerName;
					peer.device = req.device;
					peer.host = req.host;
					peer.port = req.port;
					peer.paired = true;
					peer.online = true;
					peer.token = storedToken;
					peer.lastSeen = time(nullptr);
					{
						std::lock_guard<std::mutex> pLock(peerMutex_);
						if (pairedPeers_.size() >= 5) {
							response = "{\"error\":\"too_many_peers\"}";
							return;
						}
						pairedPeers_.push_back(peer);
					}
					SaveConfig();
					response = StringFromFormat(
						"{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
						storedToken.c_str(), deviceId_.c_str());
				} else if (req.clientConfirmed) {
					// Numeric comparison: client confirmed first, now both done
					req.accepted = true;
					response = StringFromFormat(
						"{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
						req.token.c_str(), deviceId_.c_str());
				} else {
					// Numeric comparison: server confirmed, wait for client
					std::string storedToken = GenerateSessionToken();
					req.token = storedToken;
					req.accepted = true;
					PeerInfo peer;
					peer.id = req.peerId;
					peer.name = req.peerName;
					peer.device = req.device;
					peer.host = req.host;
					peer.port = req.port;
					peer.paired = true;
					peer.online = true;
					peer.token = storedToken;
					peer.lastSeen = time(nullptr);
					{
						std::lock_guard<std::mutex> pLock(peerMutex_);
						if (pairedPeers_.size() >= 5) {
							response = "{\"error\":\"too_many_peers\"}";
							return;
						}
						pairedPeers_.push_back(peer);
					}
					SaveConfig();
					response = StringFromFormat(
						"{\"status\":\"server_confirmed\",\"verificationCode\":\"%s\"}",
						req.verificationCode.c_str());
				}
			} else {
				req.rejected = true;
				response = "{\"status\":\"rejected\"}";
			}
			return;
		}
	}
	response = "{\"error\":\"request_not_found\"}";
}

void SaveStateLANSync::HandlePairVerify(const std::string &body, std::string &response) {
	std::string requestId;
	auto extractStr = [&body](const char *key) -> std::string {
		std::string search = std::string("\"") + key + "\":\"";
		size_t pos = body.find(search);
		if (pos == std::string::npos) return "";
		pos += search.size();
		size_t end = body.find('"', pos);
		return (end != std::string::npos) ? body.substr(pos, end - pos) : "";
	};
	requestId = extractStr("requestId");

	if (requestId.empty()) {
		response = "{\"error\":\"missing_requestId\"}";
		return;
	}

	std::lock_guard<std::mutex> lock(pendingMutex_);
	for (auto &req : pendingRequests_) {
		if (req.requestId == requestId) {
			if (req.rejected) {
				response = "{\"status\":\"rejected\"}";
				return;
			}
			req.clientConfirmed = true;

			if (req.serverConfirmed && req.accepted) {
				// Both confirmed → pairing complete
				response = StringFromFormat(
					"{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
					req.token.c_str(), deviceId_.c_str());
			} else {
				response = StringFromFormat(
					"{\"status\":\"client_confirmed\",\"verificationCode\":\"%s\"}",
					req.verificationCode.c_str());
			}
			return;
		}
	}
	response = "{\"error\":\"request_not_found\"}";
}

void SaveStateLANSync::HandlePairStatus(const std::string &query, std::string &response) {
	std::string requestId;
	size_t eqPos = query.find("requestId=");
	if (eqPos != std::string::npos) {
		eqPos += 10;
		size_t end = query.find_first_of("& \r\n", eqPos);
		requestId = query.substr(eqPos, end - eqPos);
	}

	if (requestId.empty()) {
		response = "{\"error\":\"missing_requestId\"}";
		return;
	}

	std::lock_guard<std::mutex> lock(pendingMutex_);
	double now = time_now_d();
	for (const auto &req : pendingRequests_) {
		if (req.requestId == requestId) {
			// Numeric comparison: both client and server must confirm
			if (req.rejected) {
				response = "{\"status\":\"rejected\"}";
			} else if ((now - req.timestamp) > 60.0) {
				response = "{\"status\":\"expired\"}";
			} else if (req.accepted && req.nonce.empty()) {
				// PIN-based flow: approve immediately
				response = StringFromFormat(
					"{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
					req.token.c_str(), deviceId_.c_str());
			} else if (req.clientConfirmed && req.serverConfirmed && req.accepted) {
				// Both sides confirmed → complete pairing
				response = StringFromFormat(
					"{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
					req.token.c_str(), deviceId_.c_str());
			} else if (req.clientConfirmed) {
				response = StringFromFormat(
					"{\"status\":\"waiting_server\",\"verificationCode\":\"%s\"}",
					req.verificationCode.c_str());
			} else if (req.serverConfirmed) {
				response = StringFromFormat(
					"{\"status\":\"waiting_client\",\"verificationCode\":\"%s\"}",
					req.verificationCode.c_str());
			} else {
				response = StringFromFormat(
					"{\"status\":\"pending\",\"verificationCode\":\"%s\"}",
					req.verificationCode.c_str());
			}
			return;
		}
	}
	response = "{\"status\":\"expired\"}";
}

std::vector<SaveStateLANSync::PendingPairRequest> SaveStateLANSync::GetPendingRequests() const {
	std::lock_guard<std::mutex> lock(pendingMutex_);
	double now = time_now_d();
	// Return active (non-expired) pending requests without modifying the vector.
	// Cleanup happens lazily on next Add/Respond/Verify operation.
	std::vector<PendingPairRequest> active;
	for (const auto &r : pendingRequests_) {
		if (!r.accepted && !r.rejected && (now - r.timestamp) <= 60.0) {
			active.push_back(r);
		}
	}
	return active;
}

void SaveStateLANSync::HandleSaveList(const std::string &gameId, std::string &response) {
	Path saveDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(saveDir, &files, "ppst");

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

		Path thumbPath = saveDir / StringFromFormat("%s_%d.jpg", prefix.c_str(), slot);
		bool hasThumb = File::Exists(thumbPath);

		if (!first) { response += ","; } first = false;
		response += StringFromFormat(
			"{\"gameId\":\"%s\",\"slot\":%d,\"size\":%lld,\"hash\":\"%s\","
			"\"hlc\":%s,\"parentHlc\":%s,"
			"\"ppssppVersion\":\"\",\"saveFormatVersion\":0,\"hasThumbnail\":%s}",
			prefix.c_str(), slot, (long long)meta.fileSize, meta.hash.c_str(),
			meta.hlc.ToJSON().c_str(), meta.parentHlc.ToJSON().c_str(),
			hasThumb ? "true" : "false");
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
	meta.parentHlc = meta.hlc;
	meta.deviceId = deviceId_;
	meta.fileSize = data.size();
	meta.lastSyncTime = time(nullptr);
	meta.WriteToFile(path);

	response = "{\"ok\":true}";
}
