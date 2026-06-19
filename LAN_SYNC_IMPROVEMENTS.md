# LAN Save State Sync - Improvement Plan & Design

**Version:** 1.0  
**Last Updated:** 2026-06-13  
**Feature Branch:** `feature/lan-sync`

---

## Table of Contents

1. [Current Architecture Overview](#1-current-architecture-overview)
2. [Identified Weaknesses](#2-identified-weaknesses)
3. [Phase 1: Network Stability](#3-phase-1-network-stability)
4. [Phase 2: Sync Protocol Reliability](#4-phase-2-sync-protocol-reliability)
5. [Phase 3: Conflict Resolution](#5-phase-3-conflict-resolution)
6. [Phase 4: Discovery & Peer Management](#6-phase-4-discovery--peer-management)
7. [Phase 5: Performance](#7-phase-5-performance)
8. [Phase 6: Testing & Observability](#8-phase-6-testing--observability)
9. [File Change Summary](#9-file-change-summary)
10. [Rollback Strategy](#10-rollback-strategy)

---

## 1. Current Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    SaveStateLANSync (Singleton)              │
├──────────────┬──────────────┬──────────────┬────────────────┤
│  Discovery   │   Server     │   Client     │   Sync Engine  │
│  mDNS+UDP    │  TLS 1.3    │  HTTP/1.1    │   HLC + Meta   │
│  Browser/    │  Custom     │  Custom      │   Conflict     │
│  Announcer   │  HTTP       │  HTTP        │   Detection    │
└──────────────┴──────────────┴──────────────┴────────────────┘
```

**Key components:**
- `Core/SaveStateLANSync.cpp` (~2182 lines) — Main orchestration
- `Core/SaveStateLANSync.h` (~286 lines) — Public API
- `Core/SaveStateSyncMetadata.cpp/.h` — Sidecar metadata
- `Core/LANSyncConfig.cpp/.h` — Config block
- `Common/Data/HLC.cpp/.h` — Hybrid Logical Clock
- `Common/Net/*` — TLS, HTTP, mDNS, UDP discovery
- `UI/LANSyncSettings.cpp/.h` — Settings UI
- `SDL/SDLLANSync.cpp/.h` — Desktop sync dialogs
- `android/.../LANSync*.java` — Android platform layer

**Protocol flow:**

1. **Announce**: mDNS-SD + UDP broadcast announces presence
2. **Discover**: mDNS browser + UDP listener collects peers
3. **Pair**: PIN (6-digit) or numeric comparison (verification code)
4. **Sync**: HTTP GET `/api/v1/saves/list` → compare HLC → download/upload
5. **Conflict**: If both sides modified independently → MERGE action

---

## 2. Identified Weaknesses

### Critical
| # | Issue | Impact |
|---|-------|--------|
| 1 | No retry logic on network errors | Sync fails on transient disconnects |
| 2 | No partial transfer resume | Large saves restart from zero |
| 3 | No atomic writes on receive | Crash during write corrupts save |
| 4 | No sync state persistence | Crash loses sync progress |
| 5 | Custom HTTP parser is fragile | Malformed requests can crash server |

### High
| # | Issue | Impact |
|---|-------|--------|
| 6 | No connection pooling | O(n) TCP handshakes = slow |
| 7 | No compression | Wastes bandwidth (saves are compressible) |
| 8 | No peer health checks | Syncs to offline/disconnected peers |
| 9 | No IP change handling | Paired peers break after IP change |
| 10 | Fixed 30s timeouts | Large saves timeout prematurely |
| 11 | No disk space pre-check | Full disk causes silent failures |
| 12 | Clock skew unhandled | HLC ordering breaks >30s skew |

### Medium
| # | Issue | Impact |
|---|-------|--------|
| 13 | No background auto-sync | User must manually trigger |
| 14 | No delta sync | Always sends full file |
| 15 | No conflict history | Can't review past conflicts |
| 16 | Thumbnails not tracked in HLC | Thumb/ppst can desync |
| 17 | No audit log | Can't trace who changed what |
| 18 | No metrics/telemetry | Can't monitor sync health |

---

## 3. Phase 1: Network Stability

### 3.1 Retry with Exponential Backoff

**File:** `Core/SaveStateLANSync.cpp`  
**New helper class:** `RetryHelper` (anonymous namespace or header)

```cpp
// Retry configuration (tunable via config)
struct RetryConfig {
    int maxRetries = 3;           // Max retry attempts
    int baseDelayMs = 1000;       // Initial backoff (1s)
    int maxDelayMs = 30000;       // Max backoff (30s)
    double jitterFactor = 0.25;   // +/-25% jitter
};

// Usage pattern in DoSync, UploadSave, DownloadSave:
auto retryFn = [&]() -> bool {
    return network_operation();
};
RetryResult result = RetryWithBackoff(retryFn, retryConfig, syncCancelled_);
```

**Implementation:**
- Add `RetryResult` struct: `{bool success, int attempts, int lastError}`
- Wrap all `connect()`/`send()`/`recv()` calls with retry
- Check `syncCancelled_` between retries
- Log each retry attempt with reason + attempt number

### 3.2 Partial Transfer Resume (HTTP Range)

**Files:** `Core/SaveStateLANSync.cpp` (client side), `Core/SaveStateLANSync.h` (API)

**Server changes** (`HandleSaveDownload`):
- Parse `Range: bytes=N-` header
- Seek to offset N in file
- Return `206 Partial Content` with `Content-Range: bytes N-M/TOTAL`
- Handle `If-Unmodified-Since` for ETag-style protection

**Client changes** (`DownloadSave`):
- Check if `.tmp` file exists (partial download)
- Get its size → send `Range: bytes=SIZE-`
- Append to `.tmp` on receive
- Rename `.tmp` → `.ppst` on completion
- Verify SHA-256 at end

**Schema:**
```cpp
// New HTTP request builder
struct HTTPRequest {
    std::string method;   // GET, POST
    std::string path;
    std::string host;
    int port;
    std::string token;
    std::string body;
    int64_t rangeStart = -1;  // -1 = no range
    std::string fileName;     // For resume detection
};
```

### 3.3 Connection Pooling

**New file:** `Core/LANSyncConnectionPool.h` / `.cpp`

```cpp
class LANSyncConnectionPool {
public:
    struct Connection {
        int fd = -1;
        std::string peerId;
        int64_t lastUsed = 0;
        bool inUse = false;
    };

    // Get or create connection (max 2 per peer)
    int Acquire(const std::string &peerId, const std::string &host, int port);
    void Release(const std::string &peerId, int fd);
    void CloseAll();

private:
    std::vector<Connection> pool_;
    std::mutex mutex_;
    static const int MAX_PER_PEER = 2;
    static const int IDLE_TIMEOUT_MS = 30000;  // 30s idle = close
};
```

**Key behavior:**
- Reuse TCP connection for list + download sequence (saves 2x handshake)
- Close idle connections after 30s
- Max 2 concurrent connections per peer
- Thread-safe with mutex

### 3.4 Adaptive Timeouts

**File:** `Core/SaveStateLANSync.cpp`

Replace fixed timeouts with dynamic calculation:

```cpp
struct AdaptiveTimeout {
    int connectMs = 5000;     // 5s (was 30s)
    int transferMs = 15000;   // 15s base
    int idleMs = 60000;       // 60s (no data received)
    
    // Calculate transfer timeout based on file size
    static int ForFileSize(int64_t bytes) {
        // 15s + 1s per MB, max 120s
        return std::min(120000, 15000 + (int)(bytes / (1024 * 1024)) * 1000);
    }
};
```

---

## 4. Phase 2: Sync Protocol Reliability

### 4.1 Atomic Writes & Crash Recovery

**File:** `Core/SaveStateLANSync.cpp` (already partial — improve)

Current: Uses `.tmp` + rename. Missing `fsync()` and directory sync.

**New helper:**
```cpp
static bool AtomicWriteFile(const void *data, size_t size, const Path &finalPath) {
    Path tmpPath = finalPath.WithExtraExtension(".tmp");
    
    // 1. Write to .tmp
    if (!File::WriteDataToFile(true, data, size, tmpPath)) {  // true = fsync
        File::Delete(tmpPath);
        return false;
    }
    
    // 2. Sync parent directory (ensures rename survives crash)
    File::SyncDirectory(finalPath.GetDirectory());
    
    // 3. Atomic rename
    if (!File::Rename(tmpPath, finalPath)) {
        File::Delete(tmpPath);
        return false;
    }
    
    // 4. Sync directory again (confirms rename)
    File::SyncDirectory(finalPath.GetDirectory());
    return true;
}
```

**New method needed on FileUtil:** Add `SyncDirectory()` if not available:
```cpp
namespace File {
    bool SyncDirectory(const Path &path);  // fsync directory handle
}
```

### 4.2 Sync State Persistence

**New file:** `Core/LANSyncStateStore.h` / `.cpp`

Persists sync progress to disk so interrupted syncs can resume:

```cpp
struct SyncStateRecord {
    std::string peerId;
    std::string syncId;          // UUID for this sync session
    int64_t startedAt;
    std::vector<std::pair<std::string,int>> completedFiles;
    std::vector<std::pair<std::string,int>> pendingFiles;
    int64_t totalBytes;
    int64_t completedBytes;
};

class LANSyncStateStore {
public:
    bool SaveInProgress(const SyncStateRecord &state);
    bool LoadInProgress(SyncStateRecord &state);
    void ClearInProgress();
    Path GetStatePath() const;  // <savestate_dir>/.lansync_state.json
};
```

**Usage in DoSync:**
- Before sync: save state → `{peerId, syncId, pendingFiles}`
- After each file: update `completedFiles`, `completedBytes`
- On startup: check for stale state → prompt resume/discard
- On success: clear state
- Stale timeout: delete after 24h

### 4.3 Pre-Flight Validation

**File:** `Core/SaveStateLANSync.cpp` — new method `ValidateSyncPreconditions()`

```cpp
struct PreflightResult {
    bool ok = false;
    std::string error;
    int64_t freeSpace = 0;
    int64_t largestFile = 0;
    bool peerReachable = false;
    double clockSkewSec = 0.0;
};

PreflightResult ValidateSyncPreconditions(const PeerInfo &peer) {
    PreflightResult r;
    
    // 1. Disk space
    r.freeSpace = File::GetFreeSpace(GetSysDirectory(DIRECTORY_SAVESTATE));
    r.largestFile = GetLargestLocalSave();
    if (r.freeSpace < r.largestFile * 2) {
        r.error = "Insufficient disk space";
        return r;
    }
    
    // 2. Peer reachable
    r.peerReachable = PingPeer(peer, 3000);  // 3s timeout
    
    // 3. Clock skew (if peer provides timestamp in status)
    int64_t localTime = time(nullptr);
    int64_t remoteTime = GetPeerTime(peer);
    r.clockSkewSec = std::abs((double)(localTime - remoteTime));
    if (r.clockSkewSec > 30.0) {
        WARN_LOG(Log::System, "LANSync: Clock skew %.1fs > 30s — HLC may be unreliable",
                 r.clockSkewSec);
    }
    
    r.ok = true;
    return r;
}
```

### 4.4 Post-Sync Verification

**New section in DoSync:**

```cpp
// After all transfers complete
struct VerificationResult {
    bool allMatch = false;
    std::vector<std::pair<std::string,int>> mismatched;
};

VerificationResult VerifySync(const PeerInfo &peer, 
                               const std::vector<std::pair<std::string,int>> &transferred) {
    VerificationResult r;
    for (auto &[gameId, slot] : transferred) {
        Path localPath = SaveState::GenerateSaveSlotPath(gameId, slot, "ppst");
        
        // Compute local hash
        std::string localData;
        if (!File::ReadBinaryFileToString(localPath, &localData)) {
            mismatched.push_back({gameId, slot});
            continue;
        }
        std::string localHash = ComputeSHA256(localData);
        
        // Download remote hash
        std::string remoteHash = GetRemoteHash(peer, gameId, slot);
        
        if (localHash != remoteHash) {
            r.mismatched.push_back({gameId, slot});
        }
    }
    r.allMatch = r.mismatched.empty();
    return r;
}
```

---

## 5. Phase 3: Conflict Resolution

### 5.1 Enhanced HLC with Clock Skew Awareness

**File:** `Common/Data/HLC.cpp`

```cpp
// Add clock skew tracking
struct ClockSkewInfo {
    bool calibrated = false;
    int64_t localTimeAtSync = 0;
    int64_t remoteTimeAtSync = 0;
    double estimatedSkewSec = 0.0;
};

// When receiving remote HLC, compensate for known skew
HLC HLC::AdjustForSkew(const ClockSkewInfo &skew) const {
    if (!skew.calibrated) return *this;
    HLC adjusted = *this;
    // Subtract estimated skew from remote wallTime
    adjusted.wallTime -= (int64_t)(skew.estimatedSkewSec * 1000000);
    return adjusted;
}
```

### 5.2 Conflict History

**New file:** `Core/LANSyncConflictStore.h` / `.cpp`

```cpp
struct ConflictRecord {
    std::string conflictId;      // UUID
    std::string gameId;
    int slot;
    std::string peerId;
    double timestamp;
    
    HLC localHlc, remoteHlc;
    HLC localParentHlc, remoteParentHlc;
    
    std::string localHash, remoteHash;
    int64_t localSize, remoteSize;
    
    ConflictResolution resolvedWith;  // How it was resolved
    double resolvedAt;                // When
};

class LANSyncConflictStore {
public:
    void RecordConflict(const ConflictRecord &cr);
    std::vector<ConflictRecord> GetHistory(const std::string &gameId, int slot);
    std::vector<ConflictRecord> GetAll();
    void Clear();
    Path GetStorePath() const;  // <savestate_dir>/.lansync_conflicts.json
};
```

### 5.3 Thumbnail HLC Integration

**File:** `Core/SaveStateSyncMetadata.h`

Add thumbnail tracking to metadata:

```cpp
struct SaveStateSyncMetadata {
    // ... existing fields ...
    
    // NEW: Thumbnail tracking
    std::string thumbnailHash;    // SHA-256 of .jpg
    HLC thumbnailHlc;             // HLC for thumbnail changes
    HLC thumbnailParentHlc;       // Parent for conflict detection
    bool hasThumbnail = false;
};
```

**Version bump:** `version = 2` → `version = 3`

**Migration:** `ReadFromFile` handles v2 (sets thumbnail to defaults).

### 5.4 3-Way Conflict Viewer Data

**File:** `Core/SaveStateLANSync.h` — extend `ConflictInfo`

```cpp
struct ConflictInfo {
    // ... existing fields ...
    
    // NEW: For 3-way merge
    std::string commonAncestorHash;  // Hash at last common parent
    std::vector<uint8_t> localSaveData;   // Actual save bytes (for UI diff)
    std::vector<uint8_t> remoteSaveData;  // Actual save bytes
};
```

**Caution:** Only load save data into memory for PROMPT mode; for auto-resolve modes, skip data load.

---

## 6. Phase 4: Discovery & Peer Management

### 6.1 Peer Health Checks

**File:** `Core/SaveStateLANSync.cpp`

Add periodic health check thread:

```cpp
void SaveStateLANSync::StartHealthChecks() {
    healthCheckRunning_ = true;
    AddBackgroundThread(std::thread([this]() {
        while (healthCheckRunning_) {
            sleep_ms(30000, "health-check");  // Every 30s
            
            std::lock_guard<std::mutex> lock(peerMutex_);
            for (auto &peer : pairedPeers_) {
                if (peer.online) {
                    bool reachable = PingPeer(peer, 3000);
                    if (!reachable) {
                        peer.failCount++;
                        if (peer.failCount >= 3) {
                            peer.online = false;
                            INFO_LOG(Log::System, "LANSync: peer %s marked offline after 3 failures",
                                     peer.id.c_str());
                        }
                    } else {
                        peer.failCount = 0;
                    }
                }
            }
        }
    }));
}
```

### 6.2 IP Change Detection

**File:** `Core/SaveStateLANSync.cpp` — new method:

```cpp
void SaveStateLANSync::HandlePeerAddressChange(const std::string &peerId, 
                                                const std::string &newHost, int newPort) {
    std::lock_guard<std::mutex> lock(peerMutex_);
    for (auto &p : pairedPeers_) {
        if (p.id == peerId) {
            if (p.host != newHost || p.port != newPort) {
                INFO_LOG(Log::System, "LANSync: peer %s address changed %s:%d → %s:%d",
                         peerId.c_str(), p.host.c_str(), p.port, newHost.c_str(), newPort);
                p.host = newHost;
                p.port = newPort;
                p.online = true;
                SaveConfig();
            }
            return;
        }
    }
}
```

**Trigger:** In discovery callbacks, if re-discovered peer matches known `id` but different `host`/`port`, call `HandlePeerAddressChange`.

### 6.3 QR Code Pairing

**Files (Android):** `LANSyncQRScanActivity.java`  
**Files (Desktop):** `SDL/SDLLANSync.cpp` — add QR display

**Format:** `ppsspp-lansync://pair?host=IP&port=PORT&name=DEVICE_NAME&id=DEVICE_ID`

**On receive:** Parse URL → call `AutoPairWithPeer(host, port, callback)`

**Data flow:**
```
Device A:  Shows QR code (encode: ppsspp-lansync://pair?host=...)
Device B:  Scans QR → extracts host/port/id → initiates auto-pair
           Shows verification code → both compare → confirm → paired
```

---

## 7. Phase 5: Performance

### 7.1 Compression (zstd)

**File:** `Core/SaveStateLANSync.cpp` — wrap upload/download

```cpp
// Check peer capability (from /api/v1/status)
bool SupportsCompression(const PeerInfo &peer) {
    // Peer must advertise "version" >= 2.0 or explicit "features":["zstd"]
    return true;  // Simplified for now
}

// Compressed upload
std::string compressed = ZSTD_Compress(data, 3);  // Level 3
UploadSave(peer, gameId, slot, compressed, "ppst.zst");

// Server decompression
if (endsWith(filename, ".zst")) {
    std::string decompressed = ZSTD_Decompress(data);
    // process decompressed
}
```

**Extension convention:**
- `.ppst.zst` for compressed saves
- `.jpg` stays uncompressed (already compressed format)
- Fallback: if peer doesn't support compression, send uncompressed `.ppst`

### 7.2 Background Auto-Sync

**File:** `Core/SaveStateLANSync.h` — new API

```cpp
class SaveStateLANSync {
    // ...
    void StartAutoSync(int intervalSeconds);
    void StopAutoSync();
    bool IsAutoSyncRunning() const;
    
private:
    std::atomic<bool> autoSyncRunning_{false};
    int autoSyncInterval_ = 300;  // 5 min default
    double lastAutoSyncTime_ = 0;
    
    void AutoSyncLoop();
};
```

**Android integration:** `LANSyncService.java` uses `WorkManager` with constraints:
- Network: UNMETERED (WiFi only)
- Battery: NOT_LOW
- DeviceIdle: false

**Desktop integration:** Uses timer checks via `CoreTiming` or `SDL_Timer`.

### 7.3 Save State Debouncing

**File:** `Core/SaveStateLANSync.cpp`

When game rapidly saves to multiple slots (common in RPGs), debounce and coalesce:

```cpp
struct DebounceEntry {
    std::string gameId;
    std::vector<int> slots;
    double lastSaveTime;
};

std::map<std::string, DebounceEntry> debounceMap_;

void SaveStateLANSync::OnSaveStateSaved(const std::string &gamePrefix, int slot) {
    auto &entry = debounceMap_[gamePrefix];
    entry.gameId = gamePrefix;
    entry.lastSaveTime = time_now_d();
    
    // Add slot if not already queued
    if (std::find(entry.slots.begin(), entry.slots.end(), slot) == entry.slots.end()) {
        entry.slots.push_back(slot);
    }
    
    // Batch: if no new saves within 2 seconds, trigger sync
    // (handled by a timer that checks debounceMap_ periodically)
}
```

### 7.4 Protocol Version Negotiation

**File:** `Core/SaveStateLANSync.cpp` — `/api/v1/status` response

```json
{
    "deviceId": "...",
    "name": "...",
    "version": "2.0",
    "features": ["zstd", "range", "delta"],
    "maxUploadSize": 104857600
}
```

Client reads `features` array and adapts behavior:
- If "zstd" present → use compression
- If "range" present → support resume
- If "delta" present → can do delta sync

---

## 8. Phase 6: Testing & Observability

### 8.1 Integration Test Framework

**File:** `test_e2e_lansync.cpp` — extend with:

```cpp
// Test scenarios:
// 1. Normal bidirectional sync (3 saves, verify hashes match)
// 2. Conflict detection (both sides modify same slot)
// 3. Network interruption during transfer
// 4. Partial download resume
// 5. Pairing flow (PIN + numeric comparison)
// 6. Peer offline handling
// 7. Large save (>10MB) transfer
// 8. Compression enabled/disabled
// 9. Multiple peers simultaneous sync
// 10. Crash recovery (kill + restart mid-sync)
```

**Helper: Network Simulator**
```cpp
class NetworkSimulator {
public:
    // Simulate various conditions
    void SetLatency(int ms);          // Add delay
    void SetLossRate(float rate);     // Drop N% of packets
    void SetBandwidthKbps(int kbps);  // Throttle
    void SetPartition(bool isolated); // Simulate network partition
};
```

### 8.2 Structured Logging

**File:** All LAN sync files

```
LANSync [syncId=abc-123] [peer=X] [game=ULUS12345] [slot=1] Starting download (size=2.1MB)
LANSync [syncId=abc-123] [peer=X] [game=ULUS12345] [slot=1] Download complete (hash=sha256:...)
LANSync [syncId=abc-123] [peer=X] Conflict detected: local(HLCA) remote(HLCB) → MERGE
LANSync [syncId=abc-123] [peer=X] Sync complete: 3 up, 2 down, 0 conflicts, 0 failed
```

**Implementation:** Use existing `Log` system with correlation ID:
```cpp
#define LANSYNC_LOG(level, syncId, peerId, fmt, ...) \
    INFO_LOG(Log::System, "LANSync [sync=%s] [peer=%s] " fmt, \
             syncId.c_str(), peerId.c_str(), ##__VA_ARGS__)
```

### 8.3 Metrics & Telemetry

**New file:** `Core/LANSyncMetrics.h` / `.cpp`

```cpp
class LANSyncMetrics {
public:
    void RecordSyncStarted(const std::string &peerId);
    void RecordSyncCompleted(const std::string &peerId, const SyncResult &result);
    void RecordTransfer(int64_t bytes, double durationSec);
    void RecordConflict();
    void RecordError(const std::string &errorType);
    
    // Stats
    int totalSyncs() const;
    int totalConflicts() const;
    double totalBytesTransferred() const;
    double averageTransferSpeedMBps() const;
    int failedSyncs() const;
    
private:
    std::atomic<int> syncCount_{0};
    std::atomic<int> conflictCount_{0};
    std::atomic<int64_t> bytesTransferred_{0};
    // ...
};
```

---

## 9. File Change Summary

### New Files
| File | Purpose | Phase |
|------|---------|-------|
| `Core/LANSyncConnectionPool.h/.cpp` | TCP connection reuse | 1 |
| `Core/LANSyncStateStore.h/.cpp` | Sync progress persistence | 2 |
| `Core/LANSyncConflictStore.h/.cpp` | Conflict history persistence | 3 |
| `Core/LANSyncMetrics.h/.cpp` | Telemetry counters | 6 |

### Modified Files
| File | Changes | Phase |
|------|---------|-------|
| `Core/SaveStateLANSync.cpp` | Retry, timeouts, atomic writes, compression, auto-sync | 1-5 |
| `Core/SaveStateLANSync.h` | New public API methods, config fields | 1-5 |
| `Core/SaveStateSyncMetadata.h/.cpp` | Version 3: thumbnail HLC | 3 |
| `Common/Data/HLC.cpp/.h` | Clock skew compensation | 3 |
| `Common/Net/HTTPClient.cpp/.h` | Range request support | 1 |
| `Common/Net/HTTPServer.cpp/.h` | Range response + 206 handling | 1 |
| `Common/File/FileUtil.cpp/.h` | `SyncDirectory()` function | 2 |
| `UI/LANSyncSettings.cpp/.h` | Auto-sync settings UI | 5 |
| `SDL/SDLLANSync.cpp/.h` | QR display, conflict history viewer | 4, 3 |
| `android/.../LANSync*.java` | WorkManager auto-sync, QR scan | 4, 5 |
| `Core/Config.cpp/.h` | New config fields for Phase 1-5 | 1-5 |
| `Core/LANSyncConfig.h` | New config fields (autoSync, compression, etc.) | 5 |

### Test Files
| File | Purpose | Phase |
|------|---------|-------|
| `test_e2e_lansync.cpp` | Extended integration tests | 6 |
| `test_lansync.cpp` | HLC edge case unit tests | 6 |
| `test_e2e_full.cpp` | End-to-end sync scenarios | 6 |

---

## 10. Rollback Strategy

### Partial Rollback
If a specific phase causes issues, revert individual files:
```bash
git checkout feature/lan-sync~1 -- Core/SaveStateLANSync.cpp Core/SaveStateLANSync.h
```

### Config Migration
New config keys use defaults if missing (safe to add):
```cpp
ConfigSetting("LANSyncCompression", SETTING(g_Config.lanSync, bCompression), CfgFlag::DEFAULT),
// ^ Default false — opt-in until verified stable
```

### Feature Toggle
Each major feature is gated:
```cpp
if (g_Config.lanSync.bCompression && peerSupportsZstd) {
    // Use compressed transfer
} else {
    // Use legacy uncompressed transfer
}
```

---

## Implementation Order (Recommended)

```
Week 1: Phase 1 (Network Stability)
  ├── Retry + backoff (3d)
  ├── Atomic writes + fsync (1d)
  ├── Connection pooling (2d)
  └── Adaptive timeouts (1d)

Week 2: Phase 2 (Sync Protocol)
  ├── Sync state persistence (2d)
  ├── Pre-flight validation (1d)
  ├── Post-sync verification (1d)
  └── Compression (zstd) (2d)

Week 3: Phase 3 (Conflict Resolution)
  ├── Thumbnail HLC + metadata v3 (2d)
  ├── Conflict history store (2d)
  ├── Clock skew handling (1d)
  └── Conflict viewer UI (2d)

Week 4: Phase 4-5 (Discovery + Performance)
  ├── Health checks (1d)
  ├── IP change handling (1d)
  ├── QR code pairing (2d)
  ├── Background auto-sync (2d)
  └── Save state debouncing (1d)

Week 5: Phase 6 (Testing + Polish)
  ├── Integration tests (2d)
  ├── Metrics/telemetry (1d)
  ├── Structured logging (1d)
  └── Bug fixes + review (2d)
```
