// Copyright (c) 2023- PPSSPP Project

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "LANSync/LANSyncScreen.h"
#include "LANSync/SaveStateLANSync.h"
#include "LANSync/LANSyncDiscovery.h"
#include "LANSync/LANSyncPairingDialog.h"
#include "LANSync/LANSyncPairing.h"
#include "LANSync/LANSyncConflictScreen.h"
#include "Core/Config.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"

extern LANSync::SaveStateLANSync g_LANSync;

LANSyncScreen::LANSyncScreen() : UIBaseScreen() {
  currentProgress_.status = LANSync::SyncProgress::IDLE;
}

LANSyncScreen::~LANSyncScreen() {
  g_LANSync.SetProgressCallback(nullptr);
}

void LANSyncScreen::CreateViews() {
  using namespace UI;
  auto n = GetI18NCategory(I18NCat::NETWORKING);
  auto di = GetI18NCategory(I18NCat::DIALOG);

  ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
  LinearLayout *contents = new LinearLayout(ORIENT_VERTICAL);
  contents->SetSpacing(5.0f);

  contents->Add(new ItemHeader(n->T("LAN Save Sync")));

  serverStatus_ = contents->Add(new TextView("", ALIGN_LEFT | FLAG_WRAP_TEXT, false,
    new LinearLayoutParams(Margins(12, 5, 0, 5))));

  toggleDiscoveryBtn_ = new Choice(n->T("Start Discovery"));
  toggleDiscoveryBtn_->OnClick.Handle(this, &LANSyncScreen::OnToggleDiscovery);
  contents->Add(toggleDiscoveryBtn_);

  autoSyncToggle_ = new Choice("");
  autoSyncToggle_->OnClick.Handle(this, &LANSyncScreen::OnToggleAutoSync);
  contents->Add(autoSyncToggle_);

  peerCountText_ = contents->Add(new TextView("", ALIGN_LEFT, false,
    new LinearLayoutParams(Margins(12, 0, 0, 0))));

  contents->Add(new ItemHeader(n->T("Discovered Peers")));

  peerListContainer_ = new LinearLayout(ORIENT_VERTICAL);
  contents->Add(peerListContainer_);

  syncAllBtn_ = new Choice(n->T("Sync with All"));
  syncAllBtn_->OnClick.Handle(this, &LANSyncScreen::OnSyncAll);
  syncAllBtn_->SetEnabled(false);
  contents->Add(syncAllBtn_);

  Choice *conflictsBtn = new Choice(n->T("View Conflicts"));
  conflictsBtn->OnClick.Handle(this, &LANSyncScreen::OnViewConflicts);
  contents->Add(conflictsBtn);

  contents->Add(new ItemHeader(n->T("Progress")));
  progressText_ = contents->Add(new TextView("", ALIGN_LEFT | FLAG_WRAP_TEXT, false,
    new LinearLayoutParams(Margins(12, 5, 0, 5))));

  contents->Add(new Spacer(10.0f));
  Choice *backBtn = new Choice(di->T("Back"));
  backBtn->OnClick.Handle(this, &LANSyncScreen::OnBack);
  contents->Add(backBtn);

  scroll->Add(contents);
  root_ = scroll;

  g_LANSync.SetProgressCallback([this](const LANSync::SyncProgress &progress) {
    OnProgress(progress);
  });

  g_LANSync.SetDiscoveryCallback([this](const LANSync::DiscoveryEvent &event) {
    peersDirty_ = true;
  });

  g_LANSync.SetScreenManager(screenManager());
}

void LANSyncScreen::update() {
  UIBaseScreen::update();

  if (frameCount_++ % 60 != 0 && !peersDirty_)
    return;
  peersDirty_ = false;

  if (!g_LANSync.IsInitialized())
    return;

  if (g_LANSync.Discovery() && g_LANSync.Discovery()->IsRunning()) {
    toggleDiscoveryBtn_->SetText("Stop Discovery");
    discoveryActive_ = true;
  } else {
    toggleDiscoveryBtn_->SetText("Start Discovery");
    discoveryActive_ = false;
  }

  {
    std::string statusText;
    if (g_LANSync.IsSyncing()) {
      statusText = "Syncing in progress...";
    } else if (discoveryActive_) {
      statusText = "Discovery active - scanning for peers";
    } else {
      statusText = "Server ready on port 27314";
    }
    if (g_Config.bLANSyncAutoSync && discoveryActive_) {
      statusText += "\nAuto-sync enabled (every ";
      statusText += std::to_string(g_Config.iLANSyncAutoSyncInterval) + "s)";
    }
    serverStatus_->SetText(statusText);
  }

  if (g_Config.bLANSyncAutoSync) {
    autoSyncToggle_->SetText("Disable Auto-Sync");
  } else {
    autoSyncToggle_->SetText("Enable Auto-Sync");
  }

  std::vector<LANSync::DiscoveredPeer> peers;
  if (g_LANSync.Discovery()) {
    peers = g_LANSync.Discovery()->GetPeers();
  }
  char peerCountBuf[64];
  snprintf(peerCountBuf, sizeof(peerCountBuf), "%zu peers discovered", peers.size());
  peerCountText_->SetText(peerCountBuf);

  syncAllBtn_->SetEnabled(!peers.empty() && !g_LANSync.IsSyncing());

  RebuildPeerList();

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

  {
    std::lock_guard<std::mutex> lock(progressMutex_);
    std::string progressStr;

    const char *statusStr = "";
    switch (currentProgress_.status) {
    case LANSync::SyncProgress::IDLE:
      statusStr = "Idle";
      break;
    case LANSync::SyncProgress::DISCOVERING:
      statusStr = "Discovering";
      break;
    case LANSync::SyncProgress::PAIRING:
      statusStr = "Pairing";
      break;
    case LANSync::SyncProgress::SYNCING:
      statusStr = "Syncing";
      break;
    case LANSync::SyncProgress::COMPLETED:
      statusStr = "Completed";
      break;
    case LANSync::SyncProgress::ERROR:
      statusStr = "Error";
      break;
    }

    progressStr = statusStr;
    if (currentProgress_.status == LANSync::SyncProgress::SYNCING) {
      char buf[128];
      snprintf(buf, sizeof(buf), "\n%d/%d files synced", currentProgress_.completedFiles, currentProgress_.totalFiles);
      progressStr += buf;
      if (!currentProgress_.currentFile.empty()) {
        progressStr += "\nFile: ";
        progressStr += currentProgress_.currentFile;
      }
    } else if (currentProgress_.status == LANSync::SyncProgress::ERROR && !currentProgress_.errorMessage.empty()) {
      progressStr += "\n";
      progressStr += currentProgress_.errorMessage;
    }

    progressText_->SetText(progressStr);
  }
}

void LANSyncScreen::RebuildPeerList() {
  using namespace UI;
  auto n = GetI18NCategory(I18NCat::NETWORKING);

  peerListContainer_->Clear();

  std::vector<LANSync::DiscoveredPeer> peers;
  if (g_LANSync.Discovery()) {
    peers = g_LANSync.Discovery()->GetPeers();
  }

  for (const auto &peer : peers) {
    LinearLayout *peerRow = new LinearLayout(ORIENT_HORIZONTAL);

    std::string label = peer.deviceName.empty()
      ? peer.host + ":" + std::to_string(peer.port)
      : peer.deviceName + " (" + peer.host + ":" + std::to_string(peer.port) + ")";
    peerRow->Add(new TextView(label, ALIGN_LEFT, false, new LinearLayoutParams(1.0f)));

    LANSync::DiscoveredPeer peerCopy = peer;
    Choice *syncBtn = new Choice(n->T("Sync"));
    syncBtn->OnClick.Add([this, peerCopy](UI::EventParams &e) {
      g_LANSync.SyncWithPeer(peerCopy);
    });
    peerRow->Add(syncBtn);

    peerListContainer_->Add(peerRow);
  }
}

void LANSyncScreen::OnProgress(const LANSync::SyncProgress &progress) {
  std::lock_guard<std::mutex> lock(progressMutex_);
  currentProgress_ = progress;
}

void LANSyncScreen::OnToggleDiscovery(UI::EventParams &e) {
  if (!g_LANSync.IsInitialized())
    return;

  if (discoveryActive_) {
    g_LANSync.StopDiscovery();
  } else {
    g_LANSync.StartDiscovery();
  }
}

void LANSyncScreen::OnToggleAutoSync(UI::EventParams &e) {
  g_Config.bLANSyncAutoSync = !g_Config.bLANSyncAutoSync;
  g_Config.Save("LANSyncConfig");
}

void LANSyncScreen::OnSyncAll(UI::EventParams &e) {
  if (!g_LANSync.IsInitialized())
    return;

  g_LANSync.SyncWithAllPeers();
}

void LANSyncScreen::OnBack(UI::EventParams &e) {
  g_LANSync.SetProgressCallback(nullptr);
  screenManager()->finishDialog(this, DR_OK);
}

void LANSyncScreen::OnPairingConfirm(const std::string &pin) {
  if (g_LANSync.Pairing()) {
    if (pin.empty()) {
      g_LANSync.Pairing()->CancelPairing();
    } else {
      g_LANSync.Pairing()->ConfirmPin(pin);
    }
  }
}

void LANSyncScreen::OnViewConflicts(UI::EventParams &e) {
  screenManager()->push(new LANSyncConflictScreen());
}
