// E2E test for LAN Save State Sync - HTTP API test
// Tests the sync protocol without needing internal state
// Compile: g++ -std=c++17 -I. -ICommon -Iext -o test_e2e_lansync test_e2e_lansync.cpp
// Run: ./test_e2e_lansync <port>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

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
        return "ERROR: connect refused";
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
        return "ERROR: connect refused";
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

int main(int argc, char** argv) {
    printf("=== LAN Save State Sync - E2E HTTP API Test ===\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <port>\n", argv[0]);
        printf("Start PPSSPPSDL first, enable LAN sync, then run this test.\n");
        return 1;
    }
    
    int port = atoi(argv[1]);
    std::string host = "127.0.0.1";
    
    // Test 1: Status endpoint
    printf("[1] GET /api/v1/status: ");
    std::string resp = httpGet(host, port, "/api/v1/status");
    if (resp.find("200 OK") != std::string::npos) {
        printf("OK\n");
    } else if (resp.find("ERROR") != std::string::npos) {
        printf("FAILED: %s\n", resp.c_str());
        return 1;
    } else {
        printf("UNEXPECTED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 2: Save list endpoint
    printf("[2] GET /api/v1/saves/list: ");
    resp = httpGet(host, port, "/api/v1/saves/list?game=TESTGAME_1.00");
    if (resp.find("200 OK") != std::string::npos) {
        printf("OK\n");
    } else {
        printf("FAILED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 3: Pair with invalid PIN
    printf("[3] POST /api/v1/pair (invalid PIN): ");
    std::string pairBody = "{\"pin\":\"000000\",\"name\":\"TestClient\",\"id\":\"test-001\"}";
    resp = httpPost(host, port, "/api/v1/pair", pairBody);
    if (resp.find("invalid_pin") != std::string::npos || resp.find("401") != std::string::npos) {
        printf("OK (rejected)\n");
    } else if (resp.find("ERROR") != std::string::npos) {
        printf("FAILED: %s\n", resp.c_str());
    } else {
        printf("UNEXPECTED: %s\n", resp.substr(0, 100).c_str());
    }
    
    // Test 4: Non-existent endpoint
    printf("[4] GET /api/v1/nonexistent: ");
    resp = httpGet(host, port, "/api/v1/nonexistent");
    if (resp.find("404") != std::string::npos || resp.find("not_found") != std::string::npos) {
        printf("OK (404)\n");
    } else {
        printf("UNEXPECTED: %s\n", resp.substr(0, 100).c_str());
    }
    
    printf("\n=== E2E HTTP API TEST COMPLETE ===\n");
    printf("If all tests passed, the sync protocol is working.\n");
    printf("To test full sync: enable LAN sync in PPSSPPSDL, pair with another instance.\n");
    return 0;
}
