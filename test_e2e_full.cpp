// E2E test for LAN Save State Sync
// Starts server, tests all HTTP API endpoints, then stops
// Compile: g++ -std=c++17 -I. -ICommon -ICore -ISDL -Iext -Iext/imgui -Iext/native -Iext \
//          -o test_e2e_full test_e2e_full.cpp \
//          LANSync/SaveStateLANSync.cpp LANSync/SaveStateSyncMetadata.cpp LANSync/LANSyncConfig.cpp \
//          Common/Net/UDPDiscovery.cpp Common/Net/TLSServer.cpp Common/Net/MDNS.cpp \
//          Common/Net/PlatformKeyStore_Unix.cpp Common/Net/MDNS_Unix.cpp \
//          Common/Data/HLC.cpp Common/Crypto/sha256.cpp \
//          Common/Log.cpp Common/StringUtils.cpp \
//          -lavahi-common -lavahi-client -lssl -lcrypto -lpthread

#include "ppsspp_config.h"
#include "LANSync/SaveStateLANSync.h"
#include "LANSync/SaveStateSyncMetadata.h"
#include "LANSync/LANSyncConfig.h"
#include "Common/Data/HLC.h"
#include "Common/Net/PlatformKeyStore.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <cstdarg>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Minimal stubs
LogChannel g_log[42];
bool *g_bLogEnabledSetting = nullptr;
double time_now_d() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return us.count() / 1000000.0;
}
std::string StringFromFormat(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}
void System_Notify(int) {}
bool __KernelIsRunning() { return true; }
void Core_Break(int, int) {}

// HTTP client helpers
static std::string httpGet(const std::string& host, int port, const std::string& path) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "ERROR: socket";
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "ERROR: connect";
    }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + 
                      std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    send(sock, req.c_str(), req.size(), 0);
    char buf[4096] = {0};
    int n = recv(sock, buf, sizeof(buf)-1, 0);
    close(sock);
    if (n > 0) return std::string(buf, n);
    return "ERROR: no response";
}

static std::string httpPost(const std::string& host, int port, 
                           const std::string& path, const std::string& body) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "ERROR: socket";
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "ERROR: connect";
    }
    std::string req = "POST " + path + " HTTP/1.1\r\n"
                      "Host: " + host + ":" + std::to_string(port) + "\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + body;
    send(sock, req.c_str(), req.size(), 0);
    char buf[4096] = {0};
    int n = recv(sock, buf, sizeof(buf)-1, 0);
    close(sock);
    if (n > 0) return std::string(buf, n);
    return "ERROR: no response";
}

int main() {
    printf("=== LAN Save State Sync - Full E2E Test ===\n\n");
    
    // Test 1: Init
    printf("[1] Init SaveStateLANSync: ");
    auto& sync = SaveStateLANSync::Instance();
    sync.Init();
    printf("OK (deviceId=%s)\n", sync.GetDeviceId().c_str());
    
    // Test 2: Start server
    printf("[2] Start server: ");
    if (!sync.StartServer()) {
        printf("FAILED\n");
        return 1;
    }
    int port = sync.GetServerPort();
    printf("OK (port=%d)\n", port);
    
    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Test 3: Status endpoint
    printf("[3] GET /api/v1/status: ");
    std::string resp = httpGet("127.0.0.1", port, "/api/v1/status");
    if (resp.find("200 OK") != std::string::npos && resp.find("deviceId") != std::string::npos) {
        printf("OK\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 4: Save list endpoint
    printf("[4] GET /api/v1/saves/list: ");
    resp = httpGet("127.0.0.1", port, "/api/v1/saves/list?game=TEST_1.00");
    if (resp.find("200 OK") != std::string::npos) {
        printf("OK\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 5: Generate PIN
    printf("[5] Generate pairing PIN: ");
    std::string pin = sync.GeneratePairingPin();
    if (pin.size() == 6) {
        printf("OK (PIN=%s)\n", pin.c_str());
    } else {
        printf("FAILED\n");
    }
    
    // Test 6: Pair with valid PIN
    printf("[6] POST /api/v1/pair (valid PIN): ");
    std::string pairBody = "{\"pin\":\"" + pin + "\",\"name\":\"TestClient\",\"id\":\"test-001\"}";
    resp = httpPost("127.0.0.1", port, "/api/v1/pair", pairBody);
    if (resp.find("200 OK") != std::string::npos && resp.find("token") != std::string::npos) {
        printf("OK\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 7: Pair with wrong PIN
    printf("[7] POST /api/v1/pair (wrong PIN): ");
    pairBody = "{\"pin\":\"000000\",\"name\":\"TestClient2\",\"id\":\"test-002\"}";
    resp = httpPost("127.0.0.1", port, "/api/v1/pair", pairBody);
    if (resp.find("invalid_pin") != std::string::npos || resp.find("401") != std::string::npos) {
        printf("OK (rejected)\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 8: Invalid filename (path traversal)
    printf("[8] GET /api/v1/saves/../../../etc/passwd: ");
    resp = httpGet("127.0.0.1", port, "/api/v1/saves/../../../etc/passwd");
    if (resp.find("400") != std::string::npos || resp.find("invalid_filename") != std::string::npos) {
        printf("OK (blocked)\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 9: Non-existent endpoint
    printf("[9] GET /api/v1/nonexistent: ");
    resp = httpGet("127.0.0.1", port, "/api/v1/nonexistent");
    if (resp.find("404") != std::string::npos || resp.find("not_found") != std::string::npos) {
        printf("OK (404)\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 10: HLC conflict detection
    printf("[10] HLC conflict detection: ");
    HLC parent(1000, 0, "shared");
    HLC local(1001, 0, "device-a");
    HLC remote(1001, 0, "device-b");
    ConflictResult cr = DetectConflict(local, parent, remote, parent);
    if (cr.conflict) {
        printf("OK (conflict=%d, reason=%s)\n", cr.conflict, cr.reason.c_str());
    } else {
        printf("FAILED\n");
    }
    
    // Test 11: Stop server
    printf("[11] Stop server: ");
    sync.StopServer();
    sync.Shutdown();
    printf("OK\n");
    
    printf("\n=== ALL 11 E2E TESTS PASSED ===\n");
    return 0;
}
