# Phase 2, Task 3: Discovery Event Callback Wiring

**Goal:** Replace polling-based peer list updates with event-driven callbacks. When `LANSyncDiscovery` fires `PEER_FOUND`/`PEER_LOST`, the UI updates immediately instead of waiting for the next 60-frame poll cycle.

**Current behavior:** `LANSyncScreen::update()` polls every 60 frames (~1s) via `g_LANSync.Discovery()->GetPeers()`. No callback is wired — `StartDiscovery()` is called with `nullptr`.

**After:** `LANSyncDiscovery` fires `DiscoveryCallback` → forwarded through `SaveStateLANSync` → `LANSyncScreen` sets `peersDirty_ = true` → `update()` processes immediately.

## Files

| File | Change |
|------|--------|
| `LANSync/SaveStateLANSync.h` | Add `SetDiscoveryCallback()` setter + `discoveryCb_` member, include `LANSyncDiscovery.h` |
| `LANSync/SaveStateLANSync.cpp` | Replace `discovery_->Start(nullptr)` with callback-forwarding lambda |
| `LANSync/LANSyncScreen.h` | Add `peersDirty_` flag |
| `LANSync/LANSyncScreen.cpp` | Wire callback in `CreateViews()`, change `update()` poll guard |

## Step-by-step

### Step 1: Add discovery callback support to SaveStateLANSync.h

**Add include** (after existing `#include "LANSync/LANSyncProtocol.h"` on line 9):
```cpp
#include "LANSync/LANSyncDiscovery.h"
```

**Add public setter** (after `SetProgressCallback` at line 38):
```cpp
using DiscoveryCallback = std::function<void(const LANSync::DiscoveryEvent &event)>;
void SetDiscoveryCallback(DiscoveryCallback cb);
```

**Add private member** (after `cbMutex_` at line 77):
```cpp
DiscoveryCallback discoveryCb_;
```

> **Why:** `DiscoveryCallback` is `std::function<void(const DiscoveryEvent&)>` defined in `LANSyncDiscovery.h`. By including that header and adding the member, `SaveStateLANSync` can forward discovery events to whoever registered (i.e., `LANSyncScreen`).

### Step 2: Wire callback in SaveStateLANSync::StartDiscovery()

Replace line 108:
```cpp
  return discovery_->Start(nullptr);
```
with:
```cpp
  return discovery_->Start(
      [this](const DiscoveryEvent &event) {
          std::lock_guard<std::mutex> lock(cbMutex_);
          if (discoveryCb_) {
              discoveryCb_(event);
          }
      }
  );
```

Add setter implementation after `SetProgressCallback`:
```cpp
void SaveStateLANSync::SetDiscoveryCallback(DiscoveryCallback cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    discoveryCb_ = std::move(cb);
}
```

> **Reuses `cbMutex_`** — same mutex already protects progress callback. No need for a second mutex.

### Step 3: Add peersDirty_ to LANSyncScreen.h

Add after line 61 (`bool discoveryActive_`):
```cpp
bool peersDirty_ = false;
```

### Step 4: Wire discovery callback in LANSyncScreen.cpp

In `CreateViews()`, after `g_LANSync.SetProgressCallback(...)` (line 82):
```cpp
g_LANSync.SetDiscoveryCallback([this](const LANSync::DiscoveryEvent &event) {
    peersDirty_ = true;
});
```

Change `update()` poll guard (line 88):
```cpp
// Before:
if (frameCount_++ % 60 != 0)
    return;

// After:
if (frameCount_++ % 60 != 0 && !peersDirty_)
    return;
peersDirty_ = false;
```

> **Effect:** Normal poll still happens every 60 frames (backward compatible), but discovery events now trigger an immediate update. `peersDirty_` is reset after processing.

### Step 5: Build verification

```bash
cmake --build build-fresh -j$(nproc)
```

Expected: 100% built without warnings.

### Step 6: Commit

```bash
git add LANSync/SaveStateLANSync.h LANSync/SaveStateLANSync.cpp \
       LANSync/LANSyncScreen.h LANSync/LANSyncScreen.cpp
git commit -m "feat(lansync): wire discovery event callbacks

Replaced null discovery callback with real forwarding to
LANSyncScreen. UI now reacts to PEER_FOUND/PEER_LOST events
instead of polling only.

[PPSSPP-FORK]"
```

## Why this works

1. `LANSyncDiscovery::Start(DiscoveryCallback)` already accepts a callback
2. `SaveStateLANSync` owns `discovery_` — just passes through the callback
3. `LANSyncScreen` sets the callback → sets `peersDirty_ = true` on any event
4. `update()` checks `peersDirty_` on every frame (not just every 60th) — skips the rate-limit when dirty

Total: ~15 lines added across 4 files.
