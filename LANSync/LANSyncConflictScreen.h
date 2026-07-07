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
