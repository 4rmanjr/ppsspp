#pragma once

#ifdef PPSSPP_LANSYNC

#include <string>
#include <vector>
#include <atomic>

#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "LANSync/SaveStateLANSync.h"

class LANPeerListScreen : public UI::PopupScreen {
public:
	LANPeerListScreen() : UI::PopupScreen("Pair New Device", "Close") {}
	const char *tag() const override { return "LANPeerList"; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 650; }

private:
	void RefreshPeers();
	void SendPairRequest(const SaveStateLANSync::PeerInfo &peer);
	void AcceptRequest(const std::string &requestId);
	void RejectRequest(const std::string &requestId);
	void ShowManualEntry();

	std::vector<SaveStateLANSync::PeerInfo> peers_;
	std::vector<SaveStateLANSync::PendingPairRequest> pending_;
	std::string pairingPeerId_;
	std::atomic<bool> pendingRefresh_{false};
	double lastRefresh_ = 0;
};

// [PPSSPP-FORK] LANSync: Progress dialog for sync operations
class LANSyncProgressPopupScreen : public UI::PopupScreen {
public:
	LANSyncProgressPopupScreen() : UI::PopupScreen("Syncing...", "") {}
	const char *tag() const override { return "LANSyncProgress"; }

	void SetProgress(int percent, int64_t bytesTransferred, int64_t totalBytes, const std::string &statusText);
	void SetCancelable(bool cancelable) { cancelable_ = cancelable; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 500; }

private:
	int percent_ = 0;
	int64_t bytesTransferred_ = 0;
	int64_t totalBytes_ = 0;
	std::string statusText_;
	bool cancelable_ = true;

	UI::TextView *progressText_ = nullptr;
	UI::TextView *bytesText_ = nullptr;
	UI::TextView *statusView_ = nullptr;
};

// [PPSSPP-FORK] LANSync: Conflict resolution dialog
class LANSyncConflictPopupScreen : public UI::PopupScreen {
public:
	LANSyncConflictPopupScreen(const std::string &slotName, int64_t localTime, int64_t remoteTime, int64_t localSize, int64_t remoteSize);
	const char *tag() const override { return "LANSyncConflict"; }

	// Result accessors after dialog closes
	bool keepLocal() const { return result_ == 0; }
	bool keepRemote() const { return result_ == 1; }
	bool keepBoth() const { return result_ == 2; }
	bool skip() const { return result_ == 3; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 600; }

private:
	std::string slotName_;
	int64_t localTime_;
	int64_t remoteTime_;
	int64_t localSize_;
	int64_t remoteSize_;
	int result_ = 3;  // Default: skip
};

// [PPSSPP-FORK] LANSync: Server pairing screen with QR and PIN
class LANSyncServerPairingScreen : public UI::PopupScreen {
public:
	LANSyncServerPairingScreen() : UI::PopupScreen("Server Pairing", "Close") {}
	const char *tag() const override { return "LANSyncServerPairing"; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 600; }

private:
	std::string currentPin_;
	std::string qrData_;
	std::atomic<bool> pinChanged_{false};
};

// [PPSSPP-FORK] LANSync: Large save state warning dialog
class LANSyncLargeSaveWarningPopup : public UI::PopupScreen {
public:
	LANSyncLargeSaveWarningPopup(const std::string &slotName, int64_t sizeBytes);
	const char *tag() const override { return "LANSyncLargeSaveWarning"; }

	bool confirmed() const { return confirmed_; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	UI::Size PopupWidth() const override { return 450; }

private:
	std::string slotName_;
	int64_t sizeBytes_;
	bool confirmed_ = false;
};

#endif // PPSSPP_LANSYNC
