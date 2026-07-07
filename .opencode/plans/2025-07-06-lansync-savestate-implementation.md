# LAN Save State Sync — Implementation Plan

> **For agentic workers:** Implementation steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add LAN-based save state synchronization between PPSSPP devices (PC ↔ Android) allowing users to transfer save states with identical progress across devices.

**Architecture:** HTTP/1.1 REST API over TLS 1.3 with mDNS-SD device discovery. A `SaveStateLANSync` singleton orchestrates discovery → pairing → sync lifecycle. Sync uses last-write-wins by file modification time. All new code guarded by `#ifdef PPSSPP_LANSYNC`, zero modifications to upstream logic, additive-only changes to existing files.

**Tech Stack:** C++17, OpenSSL (TLS), Avahi (Linux mDNS), Android NsdManager (Android mDNS), existing `Common/Net/` HTTP infra

---

## File Map

### New Files (all in `LANSync/` directory)

| File | Responsibility |
|------|---------------|
| `LANSync/HLC.h` | Hybrid Logical Clock — causal timestamp |
| `LANSync/LANSyncProtocol.h` | API types, structs, enums, HTTP paths |
| `LANSync/LANSyncConfig.h + .cpp` | Config block (device name, auto-sync, port, pairing list) |
| `LANSync/LANSyncMetadata.h + .cpp` | Sidecar `.sync.json` read/write per save file |
| `LANSync/PlatformKeyStore.h` | Interface: save/load/remove peer cert fingerprint |
| `LANSync/PlatformKeyStore_Linux.cpp` | Linux: file-based keystore (JSON per peer) |
| `LANSync/PlatformKeyStore_Android.cpp` | Android: `KeyStore` via JNI |
| `LANSync/TLSTransport.h + .cpp` | TLS context: self-signed ECDSA P-256 cert gen, OpenSSL init |
| `LANSync/LANSyncServer.h + .cpp` | TLS HTTP server (port 27314), REST API handlers |
| `LANSync/LANSyncClient.h + .cpp` | TLS HTTP client for peer requests |
| `LANSync/MDNS.h + MDNS.cpp` | mDNS abstraction interface (announce, browse) |
| `LANSync/MDNS_Linux.cpp` | Linux: Avahi client backend |
| `LANSync/MDNS_Android.cpp` | Android: `NsdManager` via JNI |
| `LANSync/LANSyncDiscovery.h + .cpp` | Discovery orchestration: mDNS + manual IP |
| `LANSync/LANSyncPairing.h + .cpp` | 6-digit PIN protocol + TOFU trust handshake |
| `LANSync/SaveStateLANSync.h + .cpp` | Singleton orchestrator: init, shutdown, startSync, callbacks |
| `LANSync/LANSyncScreen.h + .cpp` | UI screen: peer list, pairing prompt, sync progress |

### Modified Files (additive, `#ifdef PPSSPP_LANSYNC` only)

| File | Change |
|------|--------|
| `Core/Config.h` | Add `LANSyncConfig` member fields in `Config` class (after networking section) |
| `Core/Config.cpp` | Add `lansyncSettings[]` array + `g_sectionMeta` entry for `[LANSync]` section |
| `UI/MainScreen.cpp` | Add "LAN Sync" button (right column) + `OnLANSync` handler |
| `UI/MainScreen.h` | Add `OnLANSync` member declaration |
| `UI/GameSettingsScreen.cpp` | Add LAN sync section at end of Networking tab |
| `UI/PauseScreen.cpp` | Add "Sync Saves Now" option |
| `UI/PauseScreen.h` | Add `OnLANSync` member declaration |
| `UI/NativeApp.cpp` | Init/shutdown `SaveStateLANSync` on boot |
| `CMakeLists.txt` | Add `LANSync/` sources to `Core` library, `find_package(OpenSSL)`, `target_compile_definitions(PPSSPP_LANSYNC)` |
| `android/jni/Android.mk` | Add `LANSync/` sources to build |
| `android/jni/Locals.mk` | Add `-DPPSSPP_LANSYNC` to CFLAGS |

---

## Task 1: Build System + Config Foundation

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `Core/Config.h`
- Modify: `Core/Config.cpp`
- Modify: `android/jni/Android.mk`
- Modify: `android/jni/Locals.mk`

- [ ] **Step 1: Add `PPSSPP_LANSYNC` option and OpenSSL detection to CMakeLists.txt**

At line ~359 (after existing `find_package` calls like `find_package(ZLIB)`), add:

```cmake
# [PPSSPP-FORK] LANSync: save state sync over LAN
option(PPSSPP_LANSYNC "Enable LAN Save State Sync" OFF)
if(PPSSPP_LANSYNC)
    find_package(OpenSSL REQUIRED)
    if(OPENSSL_FOUND)
        message(STATUS "LANSync: OpenSSL ${OPENSSL_VERSION} found")
    endif()
endif()
```

Near the Core library target_compile_definitions (line ~2652), add:

```cmake
if(PPSSPP_LANSYNC)
    target_compile_definitions(${CoreLibName} PRIVATE PPSSPP_LANSYNC)
    target_include_directories(${CoreLibName} PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(${CoreLibName} OpenSSL::SSL OpenSSL::Crypto)
endif()
```

- [ ] **Step 2: Add LANSync/ source files to Core library build**

In the Core library `add_library()` block (line ~2142), after the platform-specific `CoreExtra`:

```cmake
if(PPSSPP_LANSYNC)
    list(APPEND CoreExtra
        LANSync/HLC.h
        LANSync/LANSyncProtocol.h
        LANSync/LANSyncConfig.h
        LANSync/LANSyncConfig.cpp
        LANSync/LANSyncMetadata.h
        LANSync/LANSyncMetadata.cpp
        LANSync/PlatformKeyStore.h
        LANSync/TLSTransport.h
        LANSync/TLSTransport.cpp
        LANSync/LANSyncServer.h
        LANSync/LANSyncServer.cpp
        LANSync/LANSyncClient.h
        LANSync/LANSyncClient.cpp
        LANSync/MDNS.h
        LANSync/MDNS.cpp
        LANSync/LANSyncDiscovery.h
        LANSync/LANSyncDiscovery.cpp
        LANSync/LANSyncPairing.h
        LANSync/LANSyncPairing.cpp
        LANSync/SaveStateLANSync.h
        LANSync/SaveStateLANSync.cpp
        LANSync/LANSyncScreen.h
        LANSync/LANSyncScreen.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND CoreExtra
            LANSync/PlatformKeyStore_Linux.cpp
            LANSync/MDNS_Linux.cpp
        )
    elseif(ANDROID)
        list(APPEND CoreExtra
            LANSync/PlatformKeyStore_Android.cpp
            LANSync/MDNS_Android.cpp
        )
    endif()
endif()
```

Also add the include dir:

```cmake
if(PPSSPP_LANSYNC)
    target_include_directories(${CoreLibName} PRIVATE ${CMAKE_SOURCE_DIR}/LANSync)
endif()
```

- [ ] **Step 3: Android build support**

In `android/jni/Locals.mk`, append `-DPPSSPP_LANSYNC` to `LOCAL_CFLAGS`.

In `android/jni/Android.mk`, add the LANSync/ .cpp files (same list as CMake) under the `if(PPSSPP_LANSYNC)` guard equivalent.

- [ ] **Step 4: Add LANSyncConfig fields to Core/Config.h**

After networking section (after line ~599), add:

```cpp
#ifdef PPSSPP_LANSYNC
    // [PPSSPP-FORK] LANSync
    bool bLANSyncEnabled = false;
    bool bLANSyncAutoSync = false;
    std::string sLANSyncDeviceName;
    int iLANSyncPort = 27314;
    std::vector<std::string> vLANSyncPairedPeers;
#endif
```

- [ ] **Step 5: Add LANSync settings to Core/Config.cpp**

Before `g_sectionMeta[]` (line ~1175), add:

```cpp
#ifdef PPSSPP_LANSYNC
static const ConfigSetting lansyncSettings[] = {
    ConfigSetting("LANSyncEnabled", SETTING(g_Config, bLANSyncEnabled), false, CfgFlag::DEFAULT),
    ConfigSetting("LANSyncAutoSync", SETTING(g_Config, bLANSyncAutoSync), false, CfgFlag::DEFAULT),
    ConfigSetting("LANSyncDeviceName", SETTING(g_Config, sLANSyncDeviceName), "", CfgFlag::DEFAULT),
    ConfigSetting("LANSyncPort", SETTING(g_Config, iLANSyncPort), 27314, CfgFlag::DEFAULT),
    ConfigSetting("LANSyncPairedPeers", SETTING(g_Config, vLANSyncPairedPeers), "", CfgFlag::DEFAULT),
};
#endif
```

In `g_sectionMeta[]`, add after VR entry:

```cpp
#ifdef PPSSPP_LANSYNC
    { &g_Config, lansyncSettings, ARRAY_SIZE(lansyncSettings), "LANSync" },
#endif
```

---

## Task 2: HLC + LANSyncProtocol Types

**Files:**
- Create: `LANSync/HLC.h`
- Create: `LANSync/LANSyncProtocol.h`

- [ ] **Step 1: Create LANSync/HLC.h**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <tuple>

struct HLC {
    uint64_t physical = 0;
    uint32_t logical = 0;

    void Tick(uint64_t now) {
        if (now > physical) {
            physical = now;
            logical = 0;
        } else {
            logical++;
        }
    }

    void Merge(const HLC &other) {
        if (other.physical > physical) {
            physical = other.physical;
            logical = other.logical + 1;
        } else if (other.physical == physical) {
            logical = std::max(logical, other.logical) + 1;
        }
    }

    bool operator<(const HLC &o) const {
        return std::tie(physical, logical) < std::tie(o.physical, o.logical);
    }
    bool operator==(const HLC &o) const {
        return physical == o.physical && logical == o.logical;
    }
    bool ConflictsWith(const HLC &other) const {
        return !(*this < other) && !(other < *this) && !(*this == other);
    }

    std::string ToString() const {
        return StringFromFormat("%llu:%u", (unsigned long long)physical, logical);
    }

    static HLC FromString(const std::string &s) {
        HLC h;
        sscanf(s.c_str(), "%llu:%u", (unsigned long long *)&h.physical, &h.logical);
        return h;
    }
};
```

- [ ] **Step 2: Create LANSync/LANSyncProtocol.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include "Common/File/Path.h"

namespace LANSync {

constexpr const char *kServiceType = "_ppsspp-sync._tcp";
constexpr int kDefaultPort = 27314;

struct PeerInfo {
    std::string deviceName;
    std::string version;
    std::string peerId;
    int protocolVersion = 1;
};

struct SaveFileEntry {
    std::string gameId;
    int slot = 0;
    std::string checksum;
    uint64_t mtime = 0;
    int64_t size = 0;
};

struct SyncResponse {
    std::vector<SaveFileEntry> files;
};

struct PairBeginResponse {
    std::string nonce;
    std::string certFingerprint;
};

struct PairVerifyRequest {
    std::string nonce;
    std::string pin;
    std::string peerId;
};

struct PairVerifyResponse {
    bool success = false;
    std::string peerId;
};

struct SyncProgress {
    int totalFiles = 0;
    int completedFiles = 0;
    std::string currentFile;
    enum Status { IDLE, DISCOVERING, PAIRING, SYNCING, COMPLETED, ERROR };
    Status status = IDLE;
    std::string errorMessage;
};

struct DiscoveredPeer {
    std::string host;
    int port = kDefaultPort;
    std::string deviceName;
    std::string peerId;
};

}  // namespace LANSync
```

---

## Task 3: LANSyncConfig + Metadata

**Files:**
- Create: `LANSync/LANSyncConfig.h`
- Create: `LANSync/LANSyncConfig.cpp`
- Create: `LANSync/LANSyncMetadata.h`
- Create: `LANSync/LANSyncMetadata.cpp`

- [ ] **Step 1: Create LANSync/LANSyncConfig.h**

```cpp
#pragma once
#include <string>
#include <vector>

namespace LANSync {

struct LANSyncConfig {
    bool bEnabled = false;
    bool bAutoSync = false;
    std::string sDeviceName;
    int iPort = 27314;
    std::vector<std::string> vPairedPeers;

    void Load();
    void Save();
    std::string GetDeviceName() const;
};

}  // namespace LANSync
```

Implementation accesses `g_Config.bLANSyncEnabled` etc.

- [ ] **Step 2: Create LANSync/LANSyncMetadata.h**

```cpp
#pragma once
#include <string>
#include "Common/File/Path.h"
#include "LANSync/HLC.h"

class LANSyncMetadata {
public:
    static bool Load(const Path &ppstPath, HLC &hlc, uint64_t &originalMtime, std::string &peerId);
    static bool Save(const Path &ppstPath, const HLC &hlc, uint64_t originalMtime, const std::string &peerId);
    static Path SidecarPath(const Path &ppstPath);
    static void Delete(const Path &ppstPath);
    static std::string ComputeChecksum(const Path &path);
};
```

- [ ] **Step 3: Create LANSync/LANSyncMetadata.cpp**

Implementation:
- `SidecarPath()`: return `Path(ppstPath.ToString() + ".sync.json")`
- `Load()`: read JSON via `IniFile`-style parsing or simple `json[field]` lookup
- `Save()`: write `{"hlc":"...","originalMtime":...,"peerId":"..."}`
- `ComputeChecksum()`: OpenSSL `EVP_Digest` with SHA-256, return hex string

---

## Task 4: PlatformKeyStore

**Files:**
- Create: `LANSync/PlatformKeyStore.h`
- Create: `LANSync/PlatformKeyStore_Linux.cpp`
- Create: `LANSync/PlatformKeyStore_Android.cpp`

- [ ] **Step 1: Create LANSync/PlatformKeyStore.h**

```cpp
#pragma once
#include <string>
#include <vector>

namespace LANSync {

struct TrustedPeer {
    std::string peerId;
    std::string deviceName;
    std::string certPEM;
    std::string lastIP;
    uint64_t pairedAt = 0;
};

class PlatformKeyStore {
public:
    static bool SavePeer(const TrustedPeer &peer);
    static std::vector<TrustedPeer> LoadPeers();
    static bool IsTrusted(const std::string &fingerprint);
    static TrustedPeer *FindPeer(const std::string &peerId);
    static bool RemovePeer(const std::string &peerId);
    static Path StorageDir();
};

}  // namespace LANSync
```

- [ ] **Step 2: Create Linux implementation**

Store peers as individual JSON files in:
```
<memstick>/PSP/PPSSPP_STATE/sync_peers/<peerId>.json
```

- `SavePeer()`: Write JSON to `StorageDir() / peerId + ".json"`
- `LoadPeers()`: List `*.json` in dir, parse each, return vector
- `IsTrusted()`: Check if file exists for given fingerprint

- [ ] **Step 3: Create Android implementation**

Same file-based approach for simplicity (Android Keystore is optional enhancement). The JNI bridge can be added later.

---

## Task 5: TLS Transport

**Files:**
- Create: `LANSync/TLSTransport.h`
- Create: `LANSync/TLSTransport.cpp`

- [ ] **Step 1: Create LANSync/TLSTransport.h**

```cpp
#pragma once
#include <string>
#include "Common/File/Path.h"

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

namespace LANSync {

class TLSContext {
public:
    TLSContext();
    ~TLSContext();
    bool InitServer();
    bool InitClient();
    bool HasCert() const { return certInitialized_; }
    SSL_CTX *GetSSLContext() const { return ctx_; }
    std::string GetCertFingerprint() const { return fingerprint_; }
    std::string GetCertPEM() const { return certPEM_; }
private:
    bool GenerateSelfSignedCert();
    bool LoadOrCreateCert();
    SSL_CTX *ctx_ = nullptr;
    bool certInitialized_ = false;
    std::string fingerprint_;
    std::string certPEM_;
    Path certDir_;
};

class TLSConnection {
public:
    TLSConnection(SSL *ssl, int fd);
    ~TLSConnection();
    SSL *GetSSL() const { return ssl_; }
    int Read(void *buf, int num);
    int Write(const void *buf, int num);
    bool Handshake();
    void Close();
private:
    SSL *ssl_ = nullptr;
    int fd_ = -1;
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/TLSTransport.cpp**

Key implementations:
- `LoadOrCreateCert()`: Check `<memstick>/PSP/PPSSPP_STATE/sync_cert.pem` + `sync_key.pem`. Generate if missing.
- `GenerateSelfSignedCert()`: Use `EVP_EC_gen("P-256")`, `X509_new()`, set validity 10yr, sign, write PEM.
- `InitServer()`: `SSL_CTX_new(TLS_server_method())`, load cert+key, set verify mode to request client cert.
- `InitClient()`: `SSL_CTX_new(TLS_client_method())`, set custom verify callback to check against PlatformKeyStore.
- `TLSConnection::Handshake()`: `SSL_accept()` (server) or `SSL_connect()` (client).

---

## Task 6: LANSync HTTP Server

**Files:**
- Create: `LANSync/LANSyncServer.h`
- Create: `LANSync/LANSyncServer.cpp`

- [ ] **Step 1: Create LANSync/LANSyncServer.h**

```cpp
#pragma once
#include <functional>
#include <map>
#include <thread>
#include <atomic>
#include "Common/File/Path.h"
#include "LANSync/TLSTransport.h"

namespace LANSync {

class SyncEngine;

class LANSyncServer {
public:
    using RequestHandler = std::function<std::string(const std::string &method, const std::string &path, const std::string &body)>;

    LANSyncServer();
    ~LANSyncServer();
    bool Start(int port, TLSContext *tlsCtx);
    void Stop();
    bool IsRunning() const { return running_; }
    int Port() const { return port_; }
    void RegisterHandler(const std::string &pathPrefix, RequestHandler handler);
    void SetSyncEngine(SyncEngine *engine) { syncEngine_ = engine; }
private:
    void AcceptLoop();
    void HandleConnection(int fd, SSL *ssl);
    void ParseAndDispatch(int fd, SSL *ssl, const std::string &request);
    int listenerFd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    TLSContext *tlsCtx_ = nullptr;
    SyncEngine *syncEngine_ = nullptr;
    std::map<std::string, RequestHandler, std::less<>> handlers_;
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/LANSyncServer.cpp**

Implementation:
- `Start()`: `socket()`, `bind()` to `0.0.0.0:port`, `listen()`, spawn accept thread.
- `AcceptLoop()`: loop `accept()` → `SSL_new()` → `SSL_set_fd()` → `SSL_accept()` → read HTTP request → dispatch → write response → cleanup.
- HTTP/1.1 parsing: read until `\r\n\r\n`, parse method + path + headers, read body by `Content-Length`.
- Response: `HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: N\r\n\r\n{body}`.
- Error: 400 (bad request), 404 (not found), 500 (internal error).

---

## Task 7: LANSync HTTP Client

**Files:**
- Create: `LANSync/LANSyncClient.h`
- Create: `LANSync/LANSyncClient.cpp`

- [ ] **Step 1: Create LANSync/LANSyncClient.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include "Common/File/Path.h"
#include "LANSync/TLSTransport.h"

namespace LANSync {

struct HTTPResponse {
    int statusCode = 0;
    std::string body;
    std::vector<std::string> headers;
};

class LANSyncClient {
public:
    explicit LANSyncClient(TLSContext *tlsCtx);
    ~LANSyncClient();
    bool Connect(const std::string &host, int port);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    HTTPResponse Get(const std::string &path);
    HTTPResponse Post(const std::string &path, const std::string &contentType, const std::string &body);
    bool DownloadFile(const std::string &path, const Path &outputPath);
private:
    HTTPResponse SendRequest(const std::string &method, const std::string &path, const std::string &contentType, const std::string &body);
    TLSContext *tlsCtx_ = nullptr;
    TLSConnection *conn_ = nullptr;
    std::string host_;
    int port_ = 0;
    bool connected_ = false;
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/LANSyncClient.cpp**

Implementation:
- `Connect()`: `net::Connect()` to host:port, then `SSL_new(ctx)`, `SSL_set_fd()`, `SSL_connect()`.
- `SendRequest()`: Format `GET /path HTTP/1.1\r\nHost: host\r\n\r\n`, write to TLS, read response.
- `DownloadFile()`: GET request, write response body to temp file, atomic rename to final path.
- `Post()`: Format `POST /path HTTP/1.1\r\nContent-Type: ...\r\nContent-Length: N\r\n\r\n{body}`.

---

## Task 8: mDNS Discovery

**Files:**
- Create: `LANSync/MDNS.h`
- Create: `LANSync/MDNS.cpp`
- Create: `LANSync/MDNS_Linux.cpp`
- Create: `LANSync/MDNS_Android.cpp`

- [ ] **Step 1: Create LANSync/MDNS.h**

```cpp
#pragma once
#include <functional>
#include <string>
#include "LANSync/LANSyncProtocol.h"

namespace LANSync {

class MDNSAnnouncer {
public:
    virtual ~MDNSAnnouncer() = default;
    virtual bool Start(const std::string &serviceType, int port, const std::string &deviceName) = 0;
    virtual void Stop() = 0;
};

class MDNSBrowser {
public:
    using OnPeerFound = std::function<void(const DiscoveredPeer &peer)>;
    using OnPeerLost = std::function<void(const DiscoveredPeer &peer)>;
    virtual ~MDNSBrowser() = default;
    virtual bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) = 0;
    virtual void Stop() = 0;
};

// Platform-specific factory functions
MDNSAnnouncer *CreateMDNSAnnouncer();
MDNSBrowser *CreateMDNSBrowser();

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/MDNS.cpp (platform dispatch)**

```cpp
#include "LANSync/MDNS.h"

namespace LANSync {

MDNSAnnouncer *CreateMDNSAnnouncer() {
#if defined(__linux__) && !defined(ANDROID)
    return new MDNSAnnouncerAvahi();
#elif defined(ANDROID)
    return new MDNSAnnouncerAndroid();
#else
    return nullptr;  // mDNS not available on this platform
#endif
}

MDNSBrowser *CreateMDNSBrowser() {
#if defined(__linux__) && !defined(ANDROID)
    return new MDNSBrowserAvahi();
#elif defined(ANDROID)
    return new MDNSBrowserAndroid();
#else
    return nullptr;
#endif
}

}  // namespace LANSync
```

- [ ] **Step 3: Create LANSync/MDNS_Linux.cpp (Avahi backend)**

Implementation using `avahi-client` library:
- `MDNSAnnouncerAvahi`: Create `AvahiClient` + `AvahiEntryGroup`, register `_ppsspp-sync._tcp` service with TXT record `name=<deviceName>`.
- `MDNSBrowserAvahi`: Create `AvahiServiceBrowser` for `_ppsspp-sync._tcp`, resolve services via `AvahiServiceResolver`, call `OnPeerFound` with host/port/name.

- [ ] **Step 4: Create LANSync/MDNS_Android.cpp (NsdManager via JNI)**

Implementation uses JNI to call Android APIs:
- Get `NsdManager` from context.
- `registerService()` with `NsdServiceInfo` for announcing.
- `discoverServices()` with `DiscoveryListener` for browsing.
- Callbacks invoke C++ function pointers.

---

## Task 9: Discovery Orchestration

**Files:**
- Create: `LANSync/LANSyncDiscovery.h`
- Create: `LANSync/LANSyncDiscovery.cpp`

- [ ] **Step 1: Create LANSync/LANSyncDiscovery.h**

```cpp
#pragma once
#include <functional>
#include <vector>
#include <atomic>
#include "LANSync/MDNS.h"
#include "LANSync/LANSyncProtocol.h"

namespace LANSync {

class SaveStateLANSync;

class LANSyncDiscovery {
public:
    explicit LANSyncDiscovery(SaveStateLANSync *owner);
    ~LANSyncDiscovery();
    bool Start(int port, const std::string &deviceName);
    void Stop();
    bool IsRunning() const { return running_; }
    void AddManualPeer(const std::string &host, int port);
    std::vector<DiscoveredPeer> GetPeers() const { return discoveredPeers_; }
private:
    void OnPeerFound(const DiscoveredPeer &peer);
    void OnPeerLost(const DiscoveredPeer &peer);
    SaveStateLANSync *owner_ = nullptr;
    MDNSAnnouncer *announcer_ = nullptr;
    MDNSBrowser *browser_ = nullptr;
    std::atomic<bool> running_{false};
    std::vector<DiscoveredPeer> discoveredPeers_;
    mutable std::mutex peersMutex_;
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/LANSyncDiscovery.cpp**

Implementation:
- `Start()`: Create announcer + browser via platform factory, start both. Filter out own IP.
- `OnPeerFound()`: Add to `discoveredPeers_`, notify owner.
- `OnPeerLost()`: Remove from list, notify owner.
- `AddManualPeer()`: Allow manual IP:port entry.

---

## Task 10: Pairing Protocol

**Files:**
- Create: `LANSync/LANSyncPairing.h`
- Create: `LANSync/LANSyncPairing.cpp`

- [ ] **Step 1: Create LANSync/LANSyncPairing.h**

```cpp
#pragma once
#include <string>
#include "LANSync/LANSyncProtocol.h"
#include "LANSync/PlatformKeyStore.h"

namespace LANSync {

class LANSyncPairing {
public:
    static std::string GeneratePIN();
    static std::string ComputePIN(const std::string &nonce, const std::string &secret);
    static PairBeginResponse BeginPairing(const std::string &certFingerprint);
    static PairVerifyResponse VerifyPairing(const std::string &nonce, const std::string &pin,
                                             const std::string &peerId, const std::string &certPEM,
                                             const std::string &deviceName);
    static bool CompletePairing(const std::string &serverHost, int port,
                                 const std::string &pin, const std::string &peerId);
    static bool IsPeerTrusted(const std::string &peerId);
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/LANSyncPairing.cpp**

Flow:
1. Server calls `BeginPairing()`: generate 32-byte random nonce, return `PairBeginResponse{nonce, fingerprint}`.
2. Client shows nonce to user as PIN (truncated HMAC). User enters it on server.
3. Server calls `VerifyPairing()`: compute expected pin = `HMAC-SHA256(nonce, certFingerprint)` hex, compare first 6 chars. If match, create TrustedPeer entry via PlatformKeyStore.
4. Client calls `CompletePairing()`: POST to `/api/v1/pair/verify`, store server's TrustedPeer on success.

---

## Task 11: SaveStateLANSync Orchestrator

**Files:**
- Create: `LANSync/SaveStateLANSync.h`
- Create: `LANSync/SaveStateLANSync.cpp`

- [ ] **Step 1: Create LANSync/SaveStateLANSync.h**

```cpp
#pragma once
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include "Common/File/Path.h"
#include "LANSync/LANSyncProtocol.h"
#include "LANSync/LANSyncDiscovery.h"
#include "LANSync/LANSyncServer.h"
#include "LANSync/LANSyncClient.h"
#include "LANSync/LANSyncPairing.h"
#include "LANSync/TLSTransport.h"

namespace LANSync {

class SaveStateLANSync {
public:
    using StatusCallback = std::function<void(const SyncProgress &progress)>;

    static SaveStateLANSync &Instance();

    bool Init();
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    void StartDiscovery();
    void StopDiscovery();
    void StartSync(const std::string &peerHost, int peerPort);
    void CancelSync();
    void StartPairing(const std::string &peerHost, int peerPort);

    SyncProgress GetProgress() const { return progress_; }
    std::vector<DiscoveredPeer> GetDiscoveredPeers() const;
    PeerInfo GetOwnInfo() const;

    void SetStatusCallback(StatusCallback cb) { callback_ = cb; }

    // Server handlers (called by LANSyncServer)
    std::string HandleInfo();
    std::string HandleSavesList();
    std::string HandleDownload(const std::string &gameId, int slot, const std::string &ext);
    std::string HandlePull(const std::string &body);
    std::string HandlePush(const std::string &body);

private:
    SaveStateLANSync() = default;
    void RunSync(const std::string &peerHost, int peerPort);
    std::vector<SaveFileEntry> ScanLocalSaves();
    SaveFileEntry MakeEntry(const Path &ppstPath);
    void SyncFiles(const std::vector<SaveFileEntry> &local, const std::vector<SaveFileEntry> &remote,
                   LANSyncClient &client, const Path &savesDir);

    bool initialized_ = false;
    std::atomic<bool> syncActive_{false};
    std::thread syncThread_;
    mutable std::mutex progressMutex_;

    TLSContext tlsCtx_;
    LANSyncServer server_;
    std::unique_ptr<LANSyncDiscovery> discovery_;
    StatusCallback callback_;
    SyncProgress progress_;
};

}  // namespace LANSync
```

- [ ] **Step 2: Create LANSync/SaveStateLANSync.cpp (core orchestrator)**

Implementation sections:

**`Init()`:**
```
1. Get save state directory: GetSysDirectory(DIRECTORY_SAVESTATE)
2. tlsCtx_.InitServer() → load or create self-signed cert
3. server_.Start(g_Config.iLANSyncPort, &tlsCtx_)
4. Register server handlers:
   server_.RegisterHandler("/api/v1/info", [this]{ return HandleInfo(); });
   server_.RegisterHandler("/api/v1/saves", [this]{ return HandleSavesList(); });
   server_.RegisterHandler("/api/v1/pull", [this](auto m, auto p, auto b){ return HandlePull(b); });
   server_.RegisterHandler("/api/v1/push", [this](auto m, auto p, auto b){ return HandlePush(b); });
   server_.RegisterHandler("/api/v1/pair/begin", [this]{ return HandlePairBegin(); });
   server_.RegisterHandler("/api/v1/pair/verify", [this](auto m, auto p, auto b){ return HandlePairVerify(b); });
5. If g_Config.bLANSyncAutoSync: discovery_->Start(port, deviceName)
6. initialized_ = true
```

**`Shutdown()`:**
```
1. Stop discovery
2. Cancel sync
3. Stop server
4. initialized_ = false
```

**`ScanLocalSaves()`:**
```
1. savesDir = GetSysDirectory(DIRECTORY_SAVESTATE)
2. GetFilesInDir(savesDir, &files, "ppst:")
3. For each .ppst file:
   a. Parse filename: gameId_slot.ppst (e.g. "ULUS12345_1.00_2.ppst")
   b. Compute checksum via LANSyncMetadata::ComputeChecksum()
   c. Get mtime + size
   d. Create SaveFileEntry
4. Return vector
```

**`RunSync(peerHost, peerPort)`:**
```
1. LANSyncClient client(&tlsCtx_)
2. client.Connect(peerHost, peerPort)
3. If not trusted: trigger pairing, return
4. HTTPResponse listResp = client.Get("/api/v1/saves")
5. Parse JSON response → vector<SaveFileEntry> remoteFiles
6. vector<SaveFileEntry> localFiles = ScanLocalSaves()
7. SyncFiles(localFiles, remoteFiles, client, savesDir)
8. client.Disconnect()
```

**`SyncFiles()`:**
```
For each remoteFile:
    Find matching localFile (same gameId + slot)
    If not found locally:
        Download remoteFile from server
    Elif checksums differ:
        If remoteFile.mtime > localFile.mtime:
            Rename local to .conflict.ppst
            Download remoteFile
        Else:
            Upload localFile to server
For each localFile not in remote list:
    Upload localFile to server
```

**Server handlers:**

`HandleInfo()`: Return JSON of `PeerInfo` (device name from config, version from PPSSPP git version, peerId from cert fingerprint).

`HandleSavesList()`: Call `ScanLocalSaves()`, serialize to JSON array.

`HandleDownload(gameId, slot, ext)`:
```
path = savesDir / gameId + "_" + slot + "." + ext (ext = "ppst" or "jpg")
If file exists: return file bytes
Else: return 404
```

`HandlePull(body)`:
```
Parse SyncRequest from body (sinceMtime)
ScanLocalSaves()
Filter: return only entries with mtime > sinceMtime
Serialize to JSON
```

`HandlePush(body)`:
```
Parse SaveFileEntry from body
Write file content to savesDir/filename.ppst
Update sidecar metadata
Return success JSON
```

---

## Task 12: LANSync UI Screen

**Files:**
- Create: `LANSync/LANSyncScreen.h`
- Create: `LANSync/LANSyncScreen.cpp`

- [ ] **Step 1: Create LANSync/LANSyncScreen.h**

```cpp
#pragma once
#include "UI/BaseScreens.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "LANSync/LANSyncProtocol.h"

class LANSyncScreen : public UIBaseScreen {
public:
    LANSyncScreen();
    const char *tag() const override { return "LANSyncScreen"; }
protected:
    void CreateViews() override;
    void update() override;
    void OnPeerSelected(const LANSync::DiscoveredPeer &peer);
    void OnScan();
    void OnManualIP();
    void OnStartSync();
    void OnSyncProgress(const LANSync::SyncProgress &progress);

    UI::ListView *peerList_ = nullptr;
    UI::TextView *statusText_ = nullptr;
    UI::ProgressBar *progressBar_ = nullptr;
    UI::Button *syncButton_ = nullptr;
    std::vector<LANSync::DiscoveredPeer> currentPeers_;
    int selectedPeer_ = -1;
};
```

- [ ] **Step 2: Create LANSync/LANSyncScreen.cpp**

UI layout:
```
Title: "LAN Save State Sync"
Status: [IDLE icon] text
─────────────────────────
[Device: "My Phone"]     ← device name display
[Scan Again] [Manual IP] ← action buttons
─────────────────────────
Peer List:
  ☐ Office PC (192.168.1.5) [Paired]
  ☐ Laptop (192.168.1.8) [Not Paired]
─────────────────────────
[Sync Selected]           ← bottom button
[Back]                    ← back navigation
─────────────────────────
Progress: ████████░░ 80%
```

Implementation:
- `CreateViews()`: Build layout with ListView for peers, buttons, progress bar (initially hidden).
- `update()`: Poll `SaveStateLANSync::Instance().GetDiscoveredPeers()` each frame. Set callback for progress.
- `OnStartSync()`: Call `SaveStateLANSync::Instance().StartSync(host, port)`.
- `OnManualIP()`: Show popup text input for IP:port.

---

## Task 13: MainScreen Button

**Files:**
- Modify: `UI/MainScreen.cpp`
- Modify: `UI/MainScreen.h`

- [ ] **Step 1: Add include to MainScreen.cpp**

```cpp
#ifdef PPSSPP_LANSYNC
#include "LANSync/LANSyncScreen.h"
#include "LANSync/SaveStateLANSync.h"
#endif
```

- [ ] **Step 2: Add button in CreateMainButtons()**

After the "Settings" button:

```cpp
#ifdef PPSSPP_LANSYNC
    if (g_Config.bLANSyncEnabled) {
        parent->Add(new Choice(mm->T("LAN Sync")))
            ->OnClick.Handle(this, &MainScreen::OnLANSync);
    }
#endif
```

- [ ] **Step 3: Add handler + declaration**

In MainScreen.cpp:
```cpp
#ifdef PPSSPP_LANSYNC
UI::EventReturn MainScreen::OnLANSync(UI::EventParams &e) {
    screenManager()->push(new LANSyncScreen());
    return UI::EVENT_DONE;
}
#endif
```

In MainScreen.h:
```cpp
#ifdef PPSSPP_LANSYNC
    UI::EventReturn OnLANSync(UI::EventParams &e);
#endif
```

---

## Task 14: GameSettingsScreen Integration

**Files:**
- Modify: `UI/GameSettingsScreen.cpp`

- [ ] **Step 1: Add include**

```cpp
#ifdef PPSSPP_LANSYNC
#include "LANSync/LANSyncScreen.h"
#include "LANSync/SaveStateLANSync.h"
#endif
```

- [ ] **Step 2: Add LAN sync section in CreateNetworkingSettings()**

At the end of `CreateNetworkingSettings()`:

```cpp
#ifdef PPSSPP_LANSYNC
    networkingSettings->Add(new ItemHeader(ms->T("LAN Save State Sync")));
    networkingSettings->Add(new CheckBox(&g_Config.bLANSyncEnabled, n->T("Enable LAN Sync")));
    networkingSettings->Add(new CheckBox(&g_Config.bLANSyncAutoSync, n->T("Auto-sync on startup")));
    
    LinearLayout *nameRow = networkingSettings->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
    nameRow->SetSpacing(0.0f);
    nameRow->Add(new ChoiceWithValueDisplay(&g_Config.sLANSyncDeviceName, n->T("Device Name"), I18NCat::NONE, new LinearLayoutParams(1.0f)));
    nameRow->Add(new Choice(ImageID("I_EDIT_TEXT"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([this](UI::EventParams &) {
        // Show text input for device name
        System_InputBoxGetString(GetRequesterToken(), "Device Name", g_Config.sLANSyncDeviceName, [](bool ok, const std::string &val) {
            if (ok) g_Config.sLANSyncDeviceName = val;
        });
        return UI::EVENT_DONE;
    });
    
    networkingSettings->Add(new Choice(ms->T("Open LAN Sync")))->OnClick.Add([this](UI::EventParams &) {
        screenManager()->push(new LANSyncScreen());
        return UI::EVENT_DONE;
    });
#endif
```

---

## Task 15: PauseScreen Integration

**Files:**
- Modify: `UI/PauseScreen.cpp`
- Modify: `UI/PauseScreen.h`

- [ ] **Step 1: Add include**

```cpp
#ifdef PPSSPP_LANSYNC
#include "LANSync/LANSyncScreen.h"
#include "LANSync/SaveStateLANSync.h"
#endif
```

- [ ] **Step 2: Add "Sync Saves" button in pause menu**

In the save state controls section (after the save slot area), add:

```cpp
#ifdef PPSSPP_LANSYNC
    if (g_Config.bLANSyncEnabled) {
        pauseItems->Add(new Choice(di->T("Sync Saves")))
            ->OnClick.Handle(this, &GamePauseScreen::OnLANSync);
    }
#endif
```

- [ ] **Step 3: Add handler + declaration**

In PauseScreen.cpp:
```cpp
#ifdef PPSSPP_LANSYNC
UI::EventReturn GamePauseScreen::OnLANSync(UI::EventParams &e) {
    screenManager()->push(new LANSyncScreen());
    return UI::EVENT_DONE;
}
#endif
```

In PauseScreen.h:
```cpp
#ifdef PPSSPP_LANSYNC
    UI::EventReturn OnLANSync(UI::EventParams &e);
#endif
```

---

## Task 16: Native Init/Shutdown

**Files:**
- Modify: `UI/NativeApp.cpp`

- [ ] **Step 1: Add include**

```cpp
#ifdef PPSSPP_LANSYNC
#include "LANSync/SaveStateLANSync.h"
#endif
```

- [ ] **Step 2: Init in NativeInit()**

After line ~410 (`g_recentFiles.EnsureThread()`):

```cpp
#ifdef PPSSPP_LANSYNC
    if (g_Config.bLANSyncEnabled) {
        SaveStateLANSync::Instance().Init();
    }
#endif
```

- [ ] **Step 3: Shutdown in NativeShutdown()**

After `__UPnPShutdown()` (line ~1717):

```cpp
#ifdef PPSSPP_LANSYNC
    SaveStateLANSync::Instance().Shutdown();
#endif
```

---

## Task 17: Build Verification

- [ ] **Step 1: Verify PPSSPP_LANSYNC=OFF builds**

```bash
mkdir -p build_off && cd build_off
cmake .. -DPPSSPP_LANSYNC=OFF
make -j$(nproc) Core
```
Expected: Success, no LANSync symbols.

- [ ] **Step 2: Verify PPSSPP_LANSYNC=ON builds on Linux**

```bash
mkdir -p build_on && cd build_on
cmake .. -DPPSSPP_LANSYNC=ON
make -j$(nproc) Core
```
Expected: Success, OpenSSL linked, LANSync compiled.

- [ ] **Step 3: Verify Android build**

```bash
cd android
./gradlew assembleGold
```
Expected: Success with `-DPPSSPP_LANSYNC` in compiler flags.
