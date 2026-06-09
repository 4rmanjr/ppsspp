#pragma once

#include <string>
#include <vector>

#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "Core/SaveStateLANSync.h"

class LANPeerListScreen : public UI::PopupScreen {
public:
	LANPeerListScreen() : UI::PopupScreen("Pair New Device", "Close") {}
	const char *tag() const override { return "LANPeerList"; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 650; }
	// Using default FillVertical() = false — popup wraps to content height, not full screen

private:
	void RefreshPeers();
	void SendPairRequest(const SaveStateLANSync::PeerInfo &peer);
	void AcceptRequest(const std::string &requestId);
	void RejectRequest(const std::string &requestId);
	void ShowManualEntry();

	std::vector<SaveStateLANSync::PeerInfo> peers_;
	std::vector<SaveStateLANSync::PendingPairRequest> pending_;
	double lastRefresh_ = 0;
};
