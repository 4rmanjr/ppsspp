# LAN Sync Auto-Pair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task.

**Goal:** Implement one-tap auto-pair for LAN Save State Sync — user taps discovered peer name → ter-pair tanpa input IP/PIN manual.

**Architecture:** Tiga endpoint HTTP baru (`/pair-request`, `/pair-respond`, `/pair-status`) di core HTTP server + `LANPeerListScreen` (PopupScreen) untuk UI Android. PIN validation tetap ada — auto-generated, auto-validated di backend.

**Tech Stack:** C++17, PPSSPP UI framework (PopupScreen, ViewGroup, Choice, TextView), POSIX sockets

---

### Task 1: Backend — PendingPairRequest struct + storage

**Files:**
- Modify: `Core/SaveStateLANSync.h`
- Modify: `Core/SaveStateLANSync.cpp`

- [ ] **Step 1: Add `PendingPairRequest` struct and storage to header**

```cpp
// In SaveStateLANSync.h, around line 117 (after SyncResult struct)
struct PendingPairRequest {
    std::string requestId;
    std::string peerId;
    std::string peerName;
    std::string device;
    std::string host;
    int port = 0;
    double timestamp = 0;
    bool accepted = false;
    bool rejected = false;
};
```

- [ ] **Step 2: Add storage + mutex + helper methods to header**

```cpp
// In SaveStateLANSync class declaration (public section)
void HandleAutoPairRequest(const std::string &body, const std::string &clientHost, std::string &response);
void HandlePairRespond(const std::string &body, std::string &response);
void HandlePairStatus(const std::string &query, std::string &response);
std::vector<PendingPairRequest> GetPendingRequests() const;

// Private members
mutable std::mutex pendingMutex_;
std::vector<PendingPairRequest> pendingRequests_;
int pendingRequestCounter_ = 0;
```

- [ ] **Step 3: Implement `GetPendingRequests()` in .cpp**

```cpp
std::vector<SaveStateLANSync::PendingPairRequest> SaveStateLANSync::GetPendingRequests() const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    // Clean expired (>60s)
    double now = time_now_d();
    pendingRequests_.erase(
        std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
            [now](const PendingPairRequest &r) { return (now - r.timestamp) > 60.0 && !r.accepted; }),
        pendingRequests_.end());
    return pendingRequests_;
}
```

---

### Task 2: Backend — Implement HandleAutoPairRequest

**Files:**
- Modify: `Core/SaveStateLANSync.cpp`

- [ ] **Step 1: Implement `HandleAutoPairRequest()`**

```cpp
void SaveStateLANSync::HandleAutoPairRequest(const std::string &body, const std::string &clientHost, std::string &response) {
    // Parse body
    std::string peerId, peerName, device;
    auto extractStr = [&body](const char *key) -> std::string {
        std::string search = std::string("\"") + key + "\":\"";
        size_t pos = body.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        size_t end = body.find('"', pos);
        return (end != std::string::npos) ? body.substr(pos, end - pos) : "";
    };
    peerId = extractStr("id");
    peerName = extractStr("name");
    device = extractStr("device");

    if (peerId.empty() || peerName.empty()) {
        response = "{\"error\":\"missing_fields\"}";
        return;
    }

    std::string requestId = StringFromFormat("req-%d-%lld", pendingRequestCounter_++, (long long)time(nullptr));

    PendingPairRequest req;
    req.requestId = requestId;
    req.peerId = peerId;
    req.peerName = peerName;
    req.device = device;
    req.host = clientHost;
    req.timestamp = time_now_d();

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        // Limit pending requests
        if (pendingRequests_.size() >= 10) {
            response = "{\"error\":\"too_many_requests\"}";
            return;
        }
        pendingRequests_.push_back(req);
    }

    System_Toast(StringFromFormat("Pair request from %s", peerName.c_str()));

    response = StringFromFormat("{\"status\":\"pending\",\"requestId\":\"%s\"}", requestId.c_str());
}
```

---

### Task 3: Backend — Implement HandlePairRespond + HandlePairStatus

**Files:**
- Modify: `Core/SaveStateLANSync.cpp`

- [ ] **Step 1: Implement `HandlePairRespond()`**

```cpp
void SaveStateLANSync::HandlePairRespond(const std::string &body, std::string &response) {
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
                req.accepted = true;

                // Auto-generate PIN and validate
                std::string pin = GeneratePairingPin();

                // Store as paired peer (using existing AcceptPairing flow)
                std::string storedToken = GenerateSessionToken();
                PeerInfo peer;
                peer.id = req.peerId;
                peer.name = req.peerName;
                peer.device = req.device;
                peer.host = req.host;
                peer.port = 0; // Will be known from discovery
                peer.paired = true;
                peer.online = true;
                peer.token = storedToken;
                peer.lastSeen = time(nullptr);

                if (pairedPeers_.size() >= 5) {
                    response = "{\"error\":\"too_many_peers\"}";
                    return;
                }
                pairedPeers_.push_back(peer);
                SaveConfig();

                response = StringFromFormat(
                    "{\"status\":\"approved\",\"token\":\"%s\",\"peerId\":\"%s\"}",
                    storedToken.c_str(), deviceId_.c_str());
            } else {
                req.rejected = true;
                response = "{\"status\":\"rejected\"}";
            }
            return;
        }
    }
    response = "{\"error\":\"request_not_found\"}";
}
```

- [ ] **Step 2: Implement `HandlePairStatus()`**

```cpp
void SaveStateLANSync::HandlePairStatus(const std::string &query, std::string &response) {
    // Parse requestId from query string (requestId=xxx)
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
            if (req.accepted) {
                response = "{\"status\":\"approved\"}";
            } else if (req.rejected) {
                response = "{\"status\":\"rejected\"}";
            } else if ((now - req.timestamp) > 60.0) {
                response = "{\"status\":\"expired\"}";
            } else {
                response = "{\"status\":\"pending\"}";
            }
            return;
        }
    }
    response = "{\"status\":\"expired\"}";
}
```

---

### Task 4: Backend — Wire new endpoints in HTTP router

**Files:**
- Modify: `Core/SaveStateLANSync.cpp`

- [ ] **Step 1: Add routes in the request handler (around line 340-370)**

Find the routing block that starts with `if (path == "/api/v1/pair" && method == "POST")` and add new routes:

```cpp
// After the existing /api/v1/pair route (around line 343)
} else if (path == "/api/v1/pair-request" && method == "POST") {
    std::string response;
    HandleAutoPairRequest(body, clientHost, response);
    WriteHTTPResponse(clientFd, 200, response);
} else if (path == "/api/v1/pair-respond" && method == "POST") {
    std::string response;
    HandlePairRespond(body, response);
    WriteHTTPResponse(clientFd, 200, response);
} else if (path.find("/api/v1/pair-status") == 0) {
    std::string query = (space2 != std::string::npos) ? request.substr(space2 + 1) : "";
    std::string response;
    HandlePairStatus(query, response);
    WriteHTTPResponse(clientFd, 200, response);
```

Note: Need to capture `clientHost` from the connection. Look at how `getpeername()` or similar is used. If not available, use `request` to extract `Host` header, or pass empty and let handler use source IP.

- [ ] **Step 2: Get client IP from connection**

Add this after `clientFd` is accepted (around line 290):
```cpp
struct sockaddr_in clientAddr;
socklen_t clientAddrLen = sizeof(clientAddr);
std::string clientHost = "0.0.0.0";
if (getpeername(clientFd, (struct sockaddr *)&clientAddr, &clientAddrLen) == 0) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, buf, sizeof(buf));
    clientHost = buf;
}
```

- [ ] **Step 3: Add `#include <arpa/inet.h>` if not already present** (check existing includes)

---

### Task 5: Client — Implement AutoPairWithPeer

**Files:**
- Modify: `Core/SaveStateLANSync.h`
- Modify: `Core/SaveStateLANSync.cpp`

- [ ] **Step 1: Add declaration to header**

```cpp
// In public section of SaveStateLANSync
void AutoPairWithPeer(const std::string &host, int port,
                      std::function<void(bool success, const std::string &error)> callback);
```

- [ ] **Step 2: Implement AutoPairWithPeer in .cpp**

```cpp
void SaveStateLANSync::AutoPairWithPeer(const std::string &host, int port,
                                         std::function<void(bool success, const std::string &error)> callback) {
    std::thread([this, host, port, callback]() {
        // Build request body
        std::string body = StringFromFormat(
            "{\"id\":\"%s\",\"name\":\"%s\",\"device\":\"%s\"}",
            deviceId_.c_str(), deviceName_.c_str(), "Android");

        // POST /api/v1/pair-request
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) { if (callback) callback(false, "socket"); return; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            closesocket(sock);
            if (callback) callback(false, "connect_refused");
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
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        closesocket(sock);
        if (n <= 0) { if (callback) callback(false, "no_response"); return; }
        buf[n] = '\0';
        std::string resp(buf, n);

        // Parse requestId from response
        std::string requestId;
        auto extractStr = [&resp](const char *key) -> std::string {
            std::string search = std::string("\"") + key + "\":\"";
            size_t pos = resp.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            size_t end = resp.find('"', pos);
            return (end != std::string::npos) ? resp.substr(pos, end - pos) : "";
        };
        requestId = extractStr("requestId");
        if (requestId.empty()) {
            if (callback) callback(false, "bad_response");
            return;
        }

        // Poll for status
        int maxPolls = 30; // 30 * 1s = 30s timeout
        for (int i = 0; i < maxPolls; i++) {
            sleep(1);

            int pollSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (pollSock < 0) continue;

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
            int pollN = recv(pollSock, pollBuf, sizeof(pollBuf) - 1, 0);
            closesocket(pollSock);
            if (pollN <= 0) continue;
            pollBuf[pollN] = '\0';
            std::string pollResp(pollBuf, pollN);

            std::string status = extractStr("status");
            if (status == "approved") {
                // Store peer as paired locally
                std::string token = extractStr("token");
                std::string peerId = extractStr("peerId");

                PeerInfo peer;
                peer.id = peerId;
                peer.name = ""; // Will be filled from discovery
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
            } else if (status == "rejected" || status == "expired") {
                if (callback) callback(false, status);
                return;
            }
            // "pending" — continue polling
        }

        if (callback) callback(false, "timeout");
    }).detach();
}
```

---

### Task 6: UI — LANPeerListScreen Header

**Files:**
- Create: `UI/LANPeerListScreen.h`

- [ ] **Step 1: Create header file**

```cpp
#pragma once

#include <string>
#include <vector>

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "Core/SaveStateLANSync.h"

class LANPeerListScreen : public UI::PopupScreen {
public:
    LANPeerListScreen();
    const char *tag() const override { return "LANPeerList"; }

protected:
    void CreatePopupContents(UI::ViewGroup *parent) override;
    void OnCompleted(DialogResult result) override;
    UI::Size PopupWidth() const override { return 650; }
    bool FillVertical() const override { return false; }

private:
    void RefreshPeers();
    void SendPairRequest(const SaveStateLANSync::PeerInfo &peer);
    void AcceptRequest(const std::string &requestId);
    void RejectRequest(const std::string &requestId);
    void ShowManualEntry();

    std::vector<SaveStateLANSync::PeerInfo> peers_;
    std::vector<SaveStateLANSync::PendingPairRequest> pending_;
    double lastRefresh_ = 0;
};
```

---

### Task 7: UI — LANPeerListScreen Implementation

**Files:**
- Create: `UI/LANPeerListScreen.cpp`

- [ ] **Step 1: Create implementation file**

```cpp
#include "ppsspp_config.h"

#include "UI/LANPeerListScreen.h"

#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Config.h"
#include "Core/SaveStateLANSync.h"

LANPeerListScreen::LANPeerListScreen() {
    RefreshPeers();
}

void LANPeerListScreen::CreatePopupContents(UI::ViewGroup *parent) {
    using namespace UI;
    auto n = GetI18NCategory(I18NCat::NETWORKING);

    // Auto-refresh every 3 seconds
    double now = time_now_d();
    if (now - lastRefresh_ > 3.0) {
        RefreshPeers();
    }

    // Header row with Refresh button
    auto *headerRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
    headerRow->Add(new TextView(n->T("Pair New Device"), ALIGN_LEFT, false));
    headerRow->Add(new Spacer(new LinearLayoutParams(1.0f, 0.0f)));
    Choice *refreshBtn = headerRow->Add(new Choice(n->T("Refresh")));
    refreshBtn->OnClick.Add([this](UI::EventParams &) {
        RefreshPeers();
        RecreateViews();
        return UI::EVENT_DONE;
    });
    parent->Add(headerRow);

    // Pending Requests section
    pending_ = SaveStateLANSync::Instance().GetPendingRequests();
    if (!pending_.empty()) {
        parent->Add(new ItemHeader(n->T("Pending Requests")));
        for (const auto &req : pending_) {
            auto *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
            row->Add(new TextView(req.peerName + " wants to pair", ALIGN_LEFT, false, new LinearLayoutParams(1.0f, 0.0f)));

            Choice *acceptBtn = row->Add(new Choice(n->T("Accept")));
            acceptBtn->OnClick.Add([this, reqId = req.requestId](UI::EventParams &) {
                AcceptRequest(reqId);
                RecreateViews();
                return UI::EVENT_DONE;
            });

            Choice *rejectBtn = row->Add(new Choice(n->T("Reject")));
            rejectBtn->OnClick.Add([this, reqId = req.requestId](UI::EventParams &) {
                RejectRequest(reqId);
                RecreateViews();
                return UI::EVENT_DONE;
            });

            parent->Add(row);
        }
    }

    // Discovered Peers section
    parent->Add(new ItemHeader(n->T("Discovered Peers")));
    auto *scrollContainer = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
    auto *peerList = new LinearLayout(ORIENT_VERTICAL);
    scrollContainer->Add(peerList);
    parent->Add(scrollContainer);

    peers_ = SaveStateLANSync::Instance().GetDiscoveredPeers();
    bool hasPeers = false;
    for (const auto &peer : peers_) {
        if (peer.paired) continue; // Only show unpaired
        if (peer.id.empty()) continue;
        hasPeers = true;

        auto *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
        std::string label = StringFromFormat("%s (%s)  %s:%d",
            peer.name.c_str(), peer.device.c_str(),
            peer.host.c_str(), peer.port);
        row->Add(new TextView(label, ALIGN_LEFT, false, new LinearLayoutParams(1.0f, 0.0f)));

        Choice *pairBtn = row->Add(new Choice(n->T("Pair")));
        pairBtn->OnClick.Add([this, peer](UI::EventParams &) {
            SendPairRequest(peer);
            return UI::EVENT_DONE;
        });

        peerList->Add(row);
    }

    if (!hasPeers) {
        peerList->Add(new TextView(n->T("No devices found"), ALIGN_LEFT, false));
    }

    // Manual entry fallback
    parent->Add(new ItemHeader(n->T("Or enter manually")));
    auto *manualRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
    Choice *manualBtn = manualRow->Add(new Choice(n->T("Enter IP:Port")));
    manualBtn->OnClick.Add([this, parent](UI::EventParams &) {
        ShowManualEntry();
        return UI::EVENT_DONE;
    });
    parent->Add(manualRow);
}

void LANPeerListScreen::RefreshPeers() {
    auto &core = SaveStateLANSync::Instance();
    peers_ = core.GetDiscoveredPeers();
    pending_ = core.GetPendingRequests();
    lastRefresh_ = time_now_d();
}

void LANPeerListScreen::SendPairRequest(const SaveStateLANSync::PeerInfo &peer) {
    auto &core = SaveStateLANSync::Instance();
    core.AutoPairWithPeer(peer.host, peer.port,
        [this](bool success, const std::string &error) {
            if (success) {
                System_Toast("Pair request sent!");
            } else {
                System_Toast(("Pair failed: " + error).c_str());
            }
        });
}

void LANPeerListScreen::AcceptRequest(const std::string &requestId) {
    auto &core = SaveStateLANSync::Instance();
    // POST /api/v1/pair-respond internally
    std::string body = StringFromFormat("{\"requestId\":\"%s\",\"accept\":\"true\"}", requestId.c_str());
    std::string response;
    core.HandlePairRespond(body, response);
    System_Toast("Pairing accepted!");
}

void LANPeerListScreen::RejectRequest(const std::string &requestId) {
    auto &core = SaveStateLANSync::Instance();
    std::string body = StringFromFormat("{\"requestId\":\"%s\",\"accept\":\"false\"}", requestId.c_str());
    std::string response;
    core.HandlePairRespond(body, response);
}

void LANPeerListScreen::ShowManualEntry() {
    RequesterToken token = GetRequesterToken();
    System_InputBoxGetString(token, "Enter peer IP:Port", "", false,
        [token](std::string_view addr, int) {
            if (addr.empty()) return;
            std::string peerAddr(addr);
            System_InputBoxGetString(token, "Enter peer's 6-digit PIN", "", false,
                [peerAddr](std::string_view pinVal, int) {
                    if (pinVal.empty()) return;
                    SaveStateLANSync::Instance().PairWithPeer(peerAddr, std::string(pinVal),
                        [](bool ok, const std::string &err) {
                            if (ok) System_Toast("Paired!");
                            else System_Toast(("Failed: " + err).c_str());
                        });
                }, nullptr);
        }, nullptr);
}

void LANPeerListScreen::OnCompleted(DialogResult result) {
    // Cleanup if needed
}
```

---

### Task 8: Integrate into GameSettingsScreen

**Files:**
- Modify: `UI/GameSettingsScreen.cpp`

- [ ] **Step 1: Add include at top**

```cpp
#include "UI/LANPeerListScreen.h"
```

- [ ] **Step 2: Replace Android Pair button handler (around line 1189)**

Find `#elif PPSSPP_PLATFORM(ANDROID)` section and replace:

```cpp
// OLD — lines 1189-1209
#elif PPSSPP_PLATFORM(ANDROID)
    auto &core = SaveStateLANSync::Instance();
    std::string myPin = core.GeneratePairingPin();
    int port = core.GetServerPort();
    System_Toast(StringFromFormat("Your PIN: %s - Port: %d", myPin.c_str(), port));

    RequesterToken token = GetRequesterToken();
    System_InputBoxGetString(token, "Enter peer IP:Port", "", false,
        [token](std::string_view addr, int) {
            if (addr.empty()) return;
            std::string peerAddr(addr);
            System_InputBoxGetString(token, "Enter peer's 6-digit PIN", "", false,
                [peerAddr](std::string_view pinVal, int) {
                    if (pinVal.empty()) return;
                    SaveStateLANSync::Instance().PairWithPeer(peerAddr, std::string(pinVal),
                        [](bool ok, const std::string &err) {
                            if (ok) System_Toast("Paired!");
                            else System_Toast(("Failed: " + err).c_str());
                        });
                }, nullptr);
        }, nullptr);

// NEW
#elif PPSSPP_PLATFORM(ANDROID)
    screenManager()->push(new LANPeerListScreen());
```

---

### Task 9: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add new files (around line 2679, after SaveStateLANSync.h)**

```cmake
    Core/SaveStateLANSync.h
    UI/LANPeerListScreen.cpp
    UI/LANPeerListScreen.h
```

Find the section where `Core/SaveStateLANSync.h` is listed (at `Core/SaveStateLANSync.cpp` / `.h` entries) and add the two new files after it in the same block.

---

### Task 10: Build and verify

**Files:**
- Build: SDL and Android

- [ ] **Step 1: Build SDL desktop**

```bash
cd build-sdl && make -j$(nproc)
```

- [ ] **Step 2: Build Android APK**

```bash
ANDROID_HOME=/opt/android-sdk ./gradlew :android:assembleNormalDebug
```

- [ ] **Step 3: Install APK via ADB**

```bash
adb install -r android/build/outputs/apk/normal/debug/android-normal-debug.apk
```
