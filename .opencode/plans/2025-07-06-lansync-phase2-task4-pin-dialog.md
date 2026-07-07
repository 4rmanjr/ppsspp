# Phase 2, Task 4: PIN Pairing Dialog Screen

**Goal:** Replace the auto-confirm in `PairWithPeer()` with an interactive PIN dialog. The initiator sees a 6-digit PIN to read aloud or show, the receiver enters it. Adds actual security to pairing.

## Current state

`PairingManager::PairWithPeer()` (line 238):
```cpp
// Auto-confirm (in production this would show UI and wait for user)
ConfirmPin(pin);
```

The PIN is computed but never shown — pairing auto-completes with zero user interaction.

## Architecture

- `PairWithPeer()` runs in a **detached thread** (line 191). After computing the PIN, it cannot push a dialog directly.
- **Solution:** `PairingManager` stores a `ScreenManager*`, sets a `pendingDialog_` flag, and the background thread **waits** on a condition variable.
- `LANSyncScreen::update()` polls the pending dialog flag and pushes `LANSyncPairingDialog` on the main thread.
- Dialog completes → calls `PairingManager::ConfirmPin(pin)` → signals the waiting thread → verify request proceeds.

## Files

| File | Change |
|------|--------|
| NEW: `LANSync/LANSyncPairingDialog.h` | Dialog screen header |
| NEW: `LANSync/LANSyncPairingDialog.cpp` | Dialog screen impl |
| `LANSync/LANSyncPairing.h` | Add `SetScreenManager()`, pending dialog fields, condvar |
| `LANSync/LANSyncPairing.cpp` | Replace auto-confirm with dialog flow |
| `LANSync/SaveStateLANSync.h` | Add `SetScreenManager()` passthrough |
| `LANSync/SaveStateLANSync.cpp` | Forward to `pairing_` |
| `LANSync/LANSyncScreen.cpp` | Poll `HasPendingDialog()`, push screen |
| `CMakeLists.txt` | Add new .cpp/.h files |

## Step-by-step

### Step 1: Create LANSyncPairingDialog.h

New file `LANSync/LANSyncPairingDialog.h`:

```cpp
#pragma once
#include <string>
#include <functional>
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/BaseScreens.h"

namespace UI { class TextView; class TextEdit; }

class LANSyncPairingDialog : public UIBaseScreen {
public:
    using PinCallback = std::function<void(const std::string &pin)>;

    LANSyncPairingDialog(bool isInitiator, const std::string &pin,
                         const std::string &peerName, PinCallback onConfirm);

    const char *tag() const override { return "LANSyncPairingDialog"; }

protected:
    void CreateViews() override;

private:
    void OnConfirm(UI::EventParams &e);
    void OnCancel(UI::EventParams &e);

    bool isInitiator_;
    std::string pin_;
    std::string peerName_;
    PinCallback onConfirm_;
    UI::TextEdit *pinInput_ = nullptr;
};
```

### Step 2: Create LANSyncPairingDialog.cpp

New file `LANSync/LANSyncPairingDialog.cpp`:

```cpp
#include "LANSync/LANSyncPairingDialog.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"

LANSyncPairingDialog::LANSyncPairingDialog(bool isInitiator, const std::string &pin,
    const std::string &peerName, PinCallback onConfirm)
    : UIBaseScreen()
    , isInitiator_(isInitiator)
    , pin_(pin)
    , peerName_(peerName)
    , onConfirm_(std::move(onConfirm)) {}

void LANSyncPairingDialog::CreateViews() {
    using namespace UI;
    auto n = GetI18NCategory(I18NCat::NETWORKING);

    ScrollView *scroll = new ScrollView(ORIENT_VERTICAL,
        new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
    LinearLayout *contents = new LinearLayout(ORIENT_VERTICAL);
    contents->SetSpacing(10.0f);
    contents->Add(new Margins(10, 10, 10, 10));

    contents->Add(new ItemHeader(n->T("Pairing")));

    char header[256];
    snprintf(header, sizeof(header), "Pairing with %s", peerName_.c_str());
    contents->Add(new TextView(header, ALIGN_LEFT | FLAG_WRAP_TEXT, false));

    if (isInitiator_) {
        contents->Add(new TextView(
            n->T("Enter this PIN on the other device:"),
            ALIGN_CENTER | FLAG_WRAP_TEXT, false));
        char pinBuf[16];
        snprintf(pinBuf, sizeof(pinBuf), "   %s   ", pin_.c_str());
        contents->Add(new TextView(pinBuf, ALIGN_CENTER, true,
            new LinearLayoutParams(Margins(20, 10))));
    } else {
        contents->Add(new TextView(
            n->T("Enter the PIN shown on the other device:"),
            ALIGN_CENTER | FLAG_WRAP_TEXT, false));
        pinInput_ = new TextEdit("", n->T("PIN"), "",
            new LinearLayoutParams(200, 50));
        pinInput_->SetMaxLen(6);
        contents->Add(pinInput_);
        Choice *confirmBtn = new Choice(n->T("Confirm"));
        confirmBtn->OnClick.Handle(this, &LANSyncPairingDialog::OnConfirm);
        contents->Add(confirmBtn);
    }

    Choice *cancelBtn = new Choice(n->T("Cancel"));
    cancelBtn->OnClick.Handle(this, &LANSyncPairingDialog::OnCancel);
    contents->Add(cancelBtn);

    scroll->Add(contents);
    root_ = scroll;
}

void LANSyncPairingDialog::OnConfirm(UI::EventParams &e) {
    if (onConfirm_) {
        if (isInitiator_) {
            onConfirm_(pin_);
        } else if (pinInput_) {
            onConfirm_(pinInput_->GetText());
        }
    }
    screenManager()->finishDialog(this, DR_OK);
}

void LANSyncPairingDialog::OnCancel(UI::EventParams &e) {
    if (onConfirm_) {
        onConfirm_("");
    }
    screenManager()->finishDialog(this, DR_CANCEL);
}
```

### Step 3: Update LANSyncPairing.h

Add to `LANSyncPairing.h`:

**Include** (after `#include <vector>`):
```cpp
#include <condition_variable>
#include "Common/UI/Screen.h"
```

**Add public methods** (after `ConfirmPin` at line 27):
```cpp
void SetScreenManager(ScreenManager *sm);
bool HasPendingDialog() const;
```

**Add private struct** (before `TLSContext` forward decl at line 12):
```cpp
struct PendingDialogInfo {
    bool isInitiator;
    std::string pin;
    std::string peerName;
};
```

**Add private members** (after `nonceMutex_` at line 57):
```cpp
ScreenManager *screenManager_ = nullptr;
std::unique_ptr<PendingDialogInfo> pendingDialog_;
mutable std::mutex dialogMutex_;
std::condition_variable dialogCv_;
std::string confirmPin_;
```

**Add public accessor** (after `HasPendingDialog`):
```cpp
PendingDialogInfo *GetPendingDialog() const;
```

### Step 4: Update LANSyncPairing.cpp

**Replace the auto-confirm block** (lines 238-239):

Before (lines 236-239):
```cpp
        {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_) {
                pending_->nonce = nonce;
                pending_->expectedPin = pin;
            }
        }

        // Auto-confirm (in production this would show UI and wait for user)
        ConfirmPin(pin);
```

After:
```cpp
        std::string localPeerId = GetLocalPeerId();
        {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_) {
                pending_->nonce = nonce;
                pending_->expectedPin = pin;
                pending_->localPeerId = localPeerId;
            }
        }

        // Show PIN dialog and wait for user confirmation
        {
            std::unique_lock<std::mutex> lk(dialogMutex_);
            pendingDialog_ = std::make_unique<PendingDialogInfo>();
            pendingDialog_->isInitiator = true;
            pendingDialog_->pin = pin;
            pendingDialog_->peerName = host + ":" + std::to_string(port);
            dialogCv_.wait(lk, [this] { return !pendingDialog_; });
        }

        std::string enteredPin;
        {
            std::lock_guard<std::mutex> l(mutex_);
            enteredPin = pending_ ? confirmPin_ : "";
        }

        std::string verifyBody = "{\"nonce\":\"" + nonce + "\",\"pin\":\"" + enteredPin + "\",\"peerId\":\"" + localPeerId + "\"}";
```

Then add an `#include <condition_variable>` at the top.

**Add `localPeerId` field to `PendingPairing`** in the header:

```cpp
struct PendingPairing {
    // ...existing fields...
    std::string localPeerId;
};
```

**Implement new methods** (add after `ConfirmPin`):

```cpp
void PairingManager::SetScreenManager(ScreenManager *sm) {
    screenManager_ = sm;
}

bool PairingManager::HasPendingDialog() const {
    std::lock_guard<std::mutex> lock(dialogMutex_);
    return pendingDialog_ != nullptr;
}

PairingManager::PendingDialogInfo *PairingManager::GetPendingDialog() const {
    std::lock_guard<std::mutex> lock(dialogMutex_);
    return pendingDialog_.get();
}
```

**Update `ConfirmPin`** to signal the waiting thread:

```cpp
void PairingManager::ConfirmPin(const std::string &pin) {
    {
        std::lock_guard<std::mutex> lk(dialogMutex_);
        confirmPin_ = pin;
        pendingDialog_.reset();
    }
    dialogCv_.notify_one();
}
```

**Update `CancelPairing`** to also clean up pending dialog:

```cpp
void PairingManager::CancelPairing() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_ && pending_->callback) {
            pending_->callback(false, "");
        }
        pending_.reset();
    }
    {
        std::lock_guard<std::mutex> lk(dialogMutex_);
        pendingDialog_.reset();
    }
    dialogCv_.notify_one();
}
```

### Step 5: Update SaveStateLANSync.h

Add public method after `SetDiscoveryCallback` at line 42:
```cpp
void SetScreenManager(ScreenManager *sm);
```

### Step 6: Update SaveStateLANSync.cpp

Add implementation (after `SetDiscoveryCallback`):
```cpp
void SaveStateLANSync::SetScreenManager(ScreenManager *sm) {
    if (pairing_) {
        pairing_->SetScreenManager(sm);
    }
}
```

Add `#include "Common/UI/Screen.h"` at the top.

### Step 7: Wire screen manager and dialog poll in LANSyncScreen

In `LANSyncScreen.h`, after existing declarations:

```cpp
void OnPairingConfirm(const std::string &pin);
```

In `LANSyncScreen.cpp`:

In `CreateViews()`, after `SetDiscoveryCallback`:
```cpp
g_LANSync.SetScreenManager(screenManager());
```

In `update()`, after the peersDirty_ check:
```cpp
if (g_LANSync.Pairing() && g_LANSync.Pairing()->HasPendingDialog()) {
    auto info = g_LANSync.Pairing()->GetPendingDialog();
    if (info) {
        screenManager()->push(new LANSyncPairingDialog(
            info->isInitiator, info->pin, info->peerName,
            [this](const std::string &enteredPin) {
                OnPairingConfirm(enteredPin);
            }));
    }
}
```

Add `#include "LANSync/LANSyncPairingDialog.h"` at the top.

Add implementation:
```cpp
void LANSyncScreen::OnPairingConfirm(const std::string &pin) {
    if (g_LANSync.Pairing()) {
        if (pin.empty()) {
            g_LANSync.Pairing()->CancelPairing();
        } else {
            g_LANSync.Pairing()->ConfirmPin(pin);
        }
    }
}
```

### Step 8: Add new files to CMakeLists.txt

Find the existing LANSync file group (around line 2150-2160) and add:
```cmake
LANSync/LANSyncPairingDialog.h
LANSync/LANSyncPairingDialog.cpp
```

Also add to `android/jni/Android.mk` (around line 794).

### Step 9: Build verification

```bash
cmake --build build-fresh -j$(nproc)
```

Expected: 100% built without warnings.

### Step 10: Commit

```bash
git add LANSync/LANSyncPairingDialog.h LANSync/LANSyncPairingDialog.cpp \
       LANSync/LANSyncPairing.h LANSync/LANSyncPairing.cpp \
       LANSync/SaveStateLANSync.h LANSync/SaveStateLANSync.cpp \
       LANSync/LANSyncScreen.h LANSync/LANSyncScreen.cpp \
       CMakeLists.txt android/jni/Android.mk
git commit -m "feat(lansync): add PIN pairing dialog screen

Replaced auto-confirm with interactive dialog. Initiator shows
6-digit PIN, receiver has text entry field. Background thread
waits on condition variable until user confirms or cancels.

[PPSSPP-FORK]"
```

## Thread-safety note

- `pendingDialog_` is protected by `dialogMutex_`
- `dialogCv_` notifies the background thread when dialog completes
- `screenManager_` is set once from main thread, never changed
- The dialog is pushed from `LANSyncScreen::update()` which runs on the main thread
- `ConfirmPin()` can be called from either thread (it just sets a string and signals)

## Limitations

- Pairing can only be confirmed while `LANSyncScreen` is open (it's the only polling site). If pairing triggers while another screen is active, the dialog won't appear until user navigates to LANSyncScreen. This is acceptable for MVP — pairing is triggered from LANSyncScreen.
