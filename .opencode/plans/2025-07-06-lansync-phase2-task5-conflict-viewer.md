# Phase 2, Task 5: Conflict Viewer Screen

**Goal:** Create a screen that lists `.ppst.conflict` files created by LWW conflict resolution. User can choose to "Keep Local" (delete .conflict), "Keep Remote" (rename .conflict → .ppst), or "Delete" (just delete .conflict).

## Context

`ResolveConflict()` in `SaveStateLANSync.cpp:464` creates conflict files:
```cpp
Path conflictPath(localPath.ToString() + ".conflict");
```

Result: `{gameId}_{slot}.ppst.conflict` in `DIRECTORY_SAVESTATE`. No existing UI to view or resolve them — they accumulate forever.

## Files

| File | Change |
|------|--------|
| NEW: `LANSync/LANSyncConflictScreen.h` | Conflict screen header |
| NEW: `LANSync/LANSyncConflictScreen.cpp` | Conflict screen implementation |
| `LANSync/LANSyncScreen.h` | Add `OnViewConflicts` method |
| `LANSync/LANSyncScreen.cpp` | Add "View Conflicts" button + handler |
| `CMakeLists.txt` | Add new source files |
| `android/jni/Android.mk` | Add new source files |

## Step-by-step

### Step 1: Create LANSyncConflictScreen.h

New file `LANSync/LANSyncConflictScreen.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/BaseScreens.h"

struct ConflictEntry {
    Path conflictPath;
    Path originalPath;
    std::string displayName;
    int slot;
    uint64_t conflictMtime;
    uint64_t originalMtime;
};

class LANSyncConflictScreen : public UIBaseScreen {
public:
    LANSyncConflictScreen();
    const char *tag() const override { return "LANSyncConflict"; }

protected:
    void CreateViews() override;

private:
    void RebuildList();
    void OnKeepLocal(const ConflictEntry &entry);
    void OnKeepRemote(const ConflictEntry &entry);
    void OnDeleteConflict(const ConflictEntry &entry);
    void OnBack(UI::EventParams &e);

    std::vector<ConflictEntry> conflicts_;
    UI::LinearLayout *listContainer_ = nullptr;
};
```

### Step 2: Create LANSyncConflictScreen.cpp

New file `LANSync/LANSyncConflictScreen.cpp`:

```cpp
#include "LANSync/LANSyncConflictScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/Util/PathUtil.h"
#include <cstdio>

LANSyncConflictScreen::LANSyncConflictScreen() : UIBaseScreen() {}

void LANSyncConflictScreen::CreateViews() {
    using namespace UI;
    auto n = GetI18NCategory(I18NCat::NETWORKING);
    auto di = GetI18NCategory(I18NCat::DIALOG);

    ScrollView *scroll = new ScrollView(ORIENT_VERTICAL,
        new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
    LinearLayout *contents = new LinearLayout(ORIENT_VERTICAL);
    contents->SetSpacing(5.0f);

    contents->Add(new ItemHeader(n->T("Sync Conflicts")));

    contents->Add(new ItemHeader(n->T("Resolve conflicts")));

    listContainer_ = new LinearLayout(ORIENT_VERTICAL);
    contents->Add(listContainer_);

    RebuildList();

    contents->Add(new Spacer(10.0f));
    Choice *backBtn = new Choice(di->T("Back"));
    backBtn->OnClick.Handle(this, &LANSyncConflictScreen::OnBack);
    contents->Add(backBtn);

    scroll->Add(contents);
    root_ = scroll;
}

void LANSyncConflictScreen::RebuildList() {
    using namespace UI;
    auto n = GetI18NCategory(I18NCat::NETWORKING);
    listContainer_->Clear();
    conflicts_.clear();

    Path stateDir = GetSysDirectory(DIRECTORY_SAVESTATE);
    std::vector<File::FileInfo> files;
    if (!File::GetFilesInDir(stateDir, &files)) return;

    for (const auto &f : files) {
        size_t conflictPos = f.name.find(".ppst.conflict");
        if (conflictPos == std::string::npos) continue;

        std::string base = f.name.substr(0, conflictPos);
        ConflictEntry entry;
        entry.conflictPath = stateDir / f.name;
        entry.originalPath = stateDir / (base + ".ppst");
        entry.conflictMtime = f.mtime;

        // Extract gameId and slot from "ULES12345_0" format
        size_t underscore = base.rfind('_');
        if (underscore != std::string::npos) {
            entry.displayName = base.substr(0, underscore);
            entry.slot = std::atoi(base.substr(underscore + 1).c_str());
        } else {
            entry.displayName = base;
            entry.slot = 0;
        }

        if (File::Exists(entry.originalPath)) {
            time_t t;
            File::GetModifTimeT(entry.originalPath, &t);
            entry.originalMtime = (uint64_t)t;
        } else {
            entry.originalMtime = 0;
        }

        conflicts_.push_back(entry);
    }

    // Sort by display name then slot
    std::stable_sort(conflicts_.begin(), conflicts_.end(),
        [](const ConflictEntry &a, const ConflictEntry &b) {
            if (a.displayName != b.displayName)
                return a.displayName < b.displayName;
            return a.slot < b.slot;
        });

    if (conflicts_.empty()) {
        listContainer_->Add(new TextView(n->T("No conflicts found."),
            ALIGN_LEFT | FLAG_WRAP_TEXT, false));
        return;
    }

    char countBuf[64];
    snprintf(countBuf, sizeof(countBuf), "%zu conflict(s) found", conflicts_.size());
    listContainer_->Add(new TextView(countBuf, ALIGN_LEFT, false,
        new LinearLayoutParams(Margins(10, 5, 0, 5))));

    for (size_t i = 0; i < conflicts_.size(); i++) {
        LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL);
        row->SetSpacing(4.0f);

        char label[256];
        snprintf(label, sizeof(label), "%s [%d]",
            conflicts_[i].displayName.c_str(), conflicts_[i].slot);
        row->Add(new TextView(label, ALIGN_LEFT, false, new LinearLayoutParams(1.0f)));

        ConflictEntry copy = conflicts_[i];

        Choice *keepLocal = new Choice(n->T("Keep Local"));
        keepLocal->OnClick.Add([this, copy](UI::EventParams &) {
            OnKeepLocal(copy);
        });
        row->Add(keepLocal);

        Choice *keepRemote = new Choice(n->T("Keep Remote"));
        keepRemote->OnClick.Add([this, copy](UI::EventParams &) {
            OnKeepRemote(copy);
        });
        row->Add(keepRemote);

        listContainer_->Add(row);
    }
}

void LANSyncConflictScreen::OnKeepLocal(const ConflictEntry &entry) {
    File::Delete(entry.conflictPath);
    RebuildList();
}

void LANSyncConflictScreen::OnKeepRemote(const ConflictEntry &entry) {
    File::Delete(entry.originalPath);
    File::Rename(entry.conflictPath, entry.originalPath);
    RebuildList();
}

void LANSyncConflictScreen::OnDeleteConflict(const ConflictEntry &entry) {
    File::Delete(entry.conflictPath);
    RebuildList();
}

void LANSyncConflictScreen::OnBack(UI::EventParams &e) {
    screenManager()->finishDialog(this, DR_OK);
}
```

Need to add `#include <algorithm>` for `std::stable_sort`.

### Step 3: Update LANSyncScreen.h

Add after `OnPairingConfirm`:
```cpp
void OnViewConflicts(UI::EventParams &e);
```

### Step 4: Update LANSyncScreen.cpp

**Add include** at the top (after `#include "LANSync/LANSyncPairing.h"`):
```cpp
#include "LANSync/LANSyncConflictScreen.h"
```

**Add button in `CreateViews()`** after `syncAllBtn_` block (after line 68):
```cpp
  Choice *conflictsBtn = new Choice(n->T("View Conflicts"));
  conflictsBtn->OnClick.Handle(this, &LANSyncScreen::OnViewConflicts);
  contents->Add(conflictsBtn);
```

**Add handler implementation** after `OnPairingConfirm`:
```cpp
void LANSyncScreen::OnViewConflicts(UI::EventParams &e) {
    screenManager()->push(new LANSyncConflictScreen());
}
```

### Step 5: Update CMakeLists.txt

Add after `LANSync/LANSyncPairingDialog.cpp` line:
```cmake
LANSync/LANSyncConflictScreen.h
LANSync/LANSyncConflictScreen.cpp
```

### Step 6: Update android/jni/Android.mk

Add after `$(SRC)/LANSync/LANSyncPairingDialog.cpp \`:
```makefile
  $(SRC)/LANSync/LANSyncConflictScreen.cpp \
```

### Step 7: Build verification

```bash
cmake --build build-fresh -j$(nproc)
```

Expected: 100% built without warnings.

### Step 8: Commit

```bash
git add LANSync/LANSyncConflictScreen.h LANSync/LANSyncConflictScreen.cpp \
       LANSync/LANSyncScreen.h LANSync/LANSyncScreen.cpp \
       CMakeLists.txt android/jni/Android.mk
git commit -m "feat(lansync): add conflict viewer screen

Lists .ppst.conflict files with Keep Local / Keep Remote actions.
Accessible via 'View Conflicts' button in LANSyncScreen.

[PPSSPP-FORK]"
```

## Thread-safety

- All file operations happen on the main thread (UI callback path)
- `RebuildList()` is called from `CreateViews()` and after each action
- No shared state with the sync background thread — conflict files are created only by `ResolveConflict()` which runs in the sync thread, but the conflict screen is read-only

## Edge cases

- **Original file deleted**: If `.ppst` was deleted but `.ppst.conflict` remains, `OnKeepRemote` renames the conflict to the original name
- **Empty state**: "No conflicts found." message shown
- **Race**: If a conflict is being created by the sync thread while viewing, `RebuildList()` picks it up on next action
