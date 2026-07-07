# Phase 2, Task 6: Sync Error Handling

**Goal:** Add retry logic, configurable timeouts, and cleanup to make LAN sync resilient to transient failures.

**Note:** Socket read/write timeouts (`setsockopt SO_RCVTIMEO/SO_SNDTIMEO`) already implemented in `LANSyncClient::Connect()` with `timeoutSec=10`. This plan covers the remaining items.

## Files

| File | Change |
|------|--------|
| `Core/Config.h` | Add `iLANSyncRetryCount`, `iLANSyncRetryDelayMs` fields |
| `Core/Config.cpp` | Add `ConfigSetting` entries in `lansyncSettings[]` |
| `LANSync/LANSyncConfig.h` | Add `iSyncRetryCount`, `iSyncRetryDelayMs` fields |
| `LANSync/LANSyncConfig.cpp` | Load/save new fields in `Load()`/`Save()` |
| `LANSync/SaveStateLANSync.cpp` | Retry loop in `DoSyncWithPeer()`, `.tmp` cleanup in `CancelSync()` |

## Steps

### Step 1: Add retry config fields to Core/Config.h

After existing `int iLANSyncPort = 27314;` (line 606), add:

```cpp
int iLANSyncRetryCount = 3;
int iLANSyncRetryDelayMs = 2000;
```

### Step 2: Add ConfigSetting entries in Core/Config.cpp

In `lansyncSettings[]` array (after line 1180), add:

```cpp
ConfigSetting("LANSyncRetryCount", SETTING(g_Config, iLANSyncRetryCount), 3, CfgFlag::DEFAULT),
ConfigSetting("LANSyncRetryDelayMs", SETTING(g_Config, iLANSyncRetryDelayMs), 2000, CfgFlag::DEFAULT),
```

### Step 3: Add retry fields to LANSyncConfigInfo

In `LANSync/LANSyncConfig.h`, add to struct:

```cpp
int iSyncRetryCount = 3;
int iSyncRetryDelayMs = 2000;
```

In `LANSync/LANSyncConfig.cpp`:
- `Load()`: add `iSyncRetryCount = g_Config.iLANSyncRetryCount;` + `iSyncRetryDelayMs = g_Config.iLANSyncRetryDelayMs;`
- `Save()`: add `g_Config.iLANSyncRetryCount = iSyncRetryCount;` + `g_Config.iLANSyncRetryDelayMs = iSyncRetryDelayMs;`

### Step 4: Add retry loop in DoSyncWithPeer()

Replace current `DoSyncWithPeer()` body with:

```cpp
void SaveStateLANSync::DoSyncWithPeer(const DiscoveredPeer &peer) {
  LANSyncConfigInfo config;
  config.Load();

  for (int attempt = 0; attempt <= config.iSyncRetryCount; attempt++) {
    if (!syncing_) return;

    LANSyncClient client(tlsCtx_.get());
    if (!client.Connect(peer.host, peer.port)) {
      if (attempt < config.iSyncRetryCount) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config.iSyncRetryDelayMs));
        continue;
      }
      UpdateProgress(SyncProgress::ERROR, peer.deviceName, 0, 0,
          "Failed after " + std::to_string(config.iSyncRetryCount + 1) + " attempts");
      return;
    }

    // ... existing sync logic (GET /states → compare → download/upload) ...

    return;  // success
  }
}
```

The existing sync logic (lines 337-413) stays unchanged — moved inside the retry loop body.

### Step 5: Add .tmp file cleanup in CancelSync()

Replace current `CancelSync()` body:

```cpp
void SaveStateLANSync::CancelSync() {
  syncing_ = false;
  {
    std::lock_guard<std::mutex> lock(syncMutex_);
  }

  // Clean up orphan .tmp files from interrupted transfers
  std::vector<File::FileInfo> files;
  if (File::GetFilesInDir(stateDir_, &files)) {
    for (const auto &f : files) {
      if (f.name.size() > 4 &&
          f.name.substr(f.name.size() - 4) == ".tmp") {
        File::Delete(stateDir_ / f.name);
      }
    }
  }

  UpdateProgress(SyncProgress::IDLE, "", 0, 0);
}
```

### Step 6: Build verification

```
cmake --build build-fresh -j$(nproc)
```

Expected: 100% built without warnings.

### Step 7: Commit

```
git add Core/Config.h Core/Config.cpp \
       LANSync/LANSyncConfig.h LANSync/LANSyncConfig.cpp \
       LANSync/SaveStateLANSync.cpp
git commit -m "feat(lansync): add sync retry and .tmp cleanup

- Configurable retry count (default 3) and delay (default 2s)
- Retry loop in DoSyncWithPeer wraps connection + sync
- CancelSync removes orphan .tmp files from interrupted transfers

[PPSSPP-FORK]"
```
