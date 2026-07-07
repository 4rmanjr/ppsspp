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

#pragma once

#include <mutex>

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/BaseScreens.h"
#include "LANSync/LANSyncProtocol.h"

namespace UI {
class Choice;
class LinearLayout;
class TextView;
}  // namespace UI

class LANSyncScreen : public UIBaseScreen {
public:
  LANSyncScreen();
  ~LANSyncScreen();

  const char *tag() const override { return "LANSync"; }

protected:
  void CreateViews() override;
  void update() override;

private:
  void RebuildPeerList();
  void OnProgress(const LANSync::SyncProgress &progress);

  void OnToggleDiscovery(UI::EventParams &e);
  void OnSyncAll(UI::EventParams &e);
  void OnBack(UI::EventParams &e);

  UI::TextView *serverStatus_ = nullptr;
  UI::Choice *toggleDiscoveryBtn_ = nullptr;
  UI::TextView *peerCountText_ = nullptr;
  UI::LinearLayout *peerListContainer_ = nullptr;
  UI::Choice *syncAllBtn_ = nullptr;
  UI::TextView *progressText_ = nullptr;

  int frameCount_ = 0;
  bool discoveryActive_ = false;
  bool peersDirty_ = false;
  LANSync::SyncProgress currentProgress_;
  mutable std::mutex progressMutex_;
};
