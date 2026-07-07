#include "LANSync/LANSyncPairingDialog.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/ScrollView.h"
#include "Common/Data/Text/I18n.h"
#include <cstdio>

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

  contents->Add(new ItemHeader(n->T("Pairing")));

  char header[256];
  snprintf(header, sizeof(header), "Pairing with %s", peerName_.c_str());
  contents->Add(new TextView(header, ALIGN_LEFT | FLAG_WRAP_TEXT, false));

  contents->Add(new Spacer(10.0f));

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
