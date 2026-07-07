#pragma once
#include <string>
#include <functional>
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/BaseScreens.h"

namespace UI { class TextEdit; }

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
