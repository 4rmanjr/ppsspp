#include "LANSync/LANSyncConflictScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/Util/PathUtil.h"
#include <algorithm>
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
