#include "ppsspp_config.h"

#include <set>
#include <atomic>

#include "UI/LANPeerListScreen.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"

#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Config.h"
#include "Core/SaveStateLANSync.h"

void LANPeerListScreen::update() {
	// Auto-refresh every 1 second (responsive to incoming pair requests)
	double now = time_now_d();
	if (now - lastRefresh_ > 1.0) {
		RefreshPeers();
		RecreateViews();
	}
	// Handle background-thread refresh requests (pair callback, etc.)
	if (pendingRefresh_.exchange(false)) {
		pairingPeerId_.clear();
		RefreshPeers();
		RecreateViews();
	}
	UI::PopupScreen::update();
}

void LANPeerListScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	double now = time_now_d();
	if (now - lastRefresh_ > 1.0) {
		RefreshPeers();
	}

	// Single ScrollView wrapping ALL content
	auto *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	auto *content = new LinearLayout(ORIENT_VERTICAL);

	// Header row with Refresh
	auto *headerRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	headerRow->Add(new TextView(n->T("Pair New Device"), ALIGN_LEFT, false));
	headerRow->Add(new Spacer(new LinearLayoutParams(1.0f, 0.0f)));
	Choice *refreshBtn = headerRow->Add(new Choice(n->T("Refresh")));
	refreshBtn->OnClick.Add([this](UI::EventParams &) {
		RefreshPeers();
		RecreateViews();
	});
	content->Add(headerRow);

	// Pending Requests section
	pending_ = SaveStateLANSync::Instance().GetPendingRequests();
	std::set<std::string> pendingPeerIds;  // To deduplicate with discovered peers
	if (!pending_.empty()) {
		content->Add(new ItemHeader(n->T("Pending Requests")));
		for (const auto &req : pending_) {
			pendingPeerIds.insert(req.peerId);
			auto *item = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4)));
			std::string label = req.peerName + " wants to pair";
			if (!req.verificationCode.empty()) {
				label += "\nCode: " + req.verificationCode + " (verify on both devices)";
			}
			auto *text = item->Add(new TextView(label, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			text->SetWordWrap();

			auto *buttonRow = item->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			Choice *acceptBtn = buttonRow->Add(new Choice(n->T("Accept"), new LinearLayoutParams(1.0f)));
			acceptBtn->OnClick.Add([this, reqId = req.requestId](UI::EventParams &) {
				AcceptRequest(reqId);
				RecreateViews();
			});

			Choice *rejectBtn = buttonRow->Add(new Choice(n->T("Reject"), new LinearLayoutParams(1.0f)));
			rejectBtn->OnClick.Add([this, reqId = req.requestId](UI::EventParams &) {
				RejectRequest(reqId);
				RecreateViews();
			});

			content->Add(item);
		}
	}

	// Discovered Peers section
	content->Add(new ItemHeader(n->T("Discovered Peers")));

	// Collect pending peer IDs to avoid showing them twice
	peers_ = SaveStateLANSync::Instance().GetDiscoveredPeers();
	bool hasPeers = false;
	for (const auto &peer : peers_) {
		if (peer.paired) continue;
		if (peer.id.empty()) continue;
		// Skip peers already shown in Pending Requests section
		if (pendingPeerIds.find(peer.id) != pendingPeerIds.end()) continue;
		hasPeers = true;

		bool isPairing = (peer.id == pairingPeerId_);

		auto *item = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4)));
		std::string label = StringFromFormat("%s (%s)  %s:%d",
			peer.name.c_str(), peer.device.c_str(),
			peer.host.c_str(), peer.port);
		auto *text = item->Add(new TextView(label, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		text->SetWordWrap();

		auto *buttonRow = item->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		Choice *pairBtn = buttonRow->Add(new Choice(isPairing ? n->T("Pairing...") : n->T("Pair"), new LinearLayoutParams(1.0f)));
		if (isPairing) {
			pairBtn->SetEnabled(false);
		} else {
			pairBtn->OnClick.Add([this, peer](UI::EventParams &) {
				pairingPeerId_ = peer.id;
				SendPairRequest(peer);
			});
		}

		content->Add(item);
	}

	if (!hasPeers) {
		content->Add(new TextView(n->T("No devices found"), ALIGN_LEFT, false));
	}

	// Manual entry fallback
	content->Add(new ItemHeader(n->T("Or enter manually")));
	auto *manualRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	Choice *manualBtn = manualRow->Add(new Choice(n->T("Enter IP:Port")));
	manualBtn->OnClick.Add([this](UI::EventParams &) {
		ShowManualEntry();
	});
	content->Add(manualRow);

	scrollView->Add(content);
	parent->Add(scrollView);
}

void LANPeerListScreen::RefreshPeers() {
	auto &core = SaveStateLANSync::Instance();
	peers_ = core.GetDiscoveredPeers();
	pending_ = core.GetPendingRequests();
	lastRefresh_ = time_now_d();
}

void LANPeerListScreen::SendPairRequest(const SaveStateLANSync::PeerInfo &peer) {
	auto &core = SaveStateLANSync::Instance();
	core.AutoPairWithPeer(peer.host, peer.port,
		[this](bool success, const std::string &error) {
			pendingRefresh_ = true;  // Safe: main thread reads in update()
			if (success) {
				System_Toast("Pairing successful!");
			} else {
				System_Toast(("Pair failed: " + error).c_str());
			}
		});
}

void LANPeerListScreen::AcceptRequest(const std::string &requestId) {
	INFO_LOG(Log::System, "LANPeerList: Accepting request %s", requestId.c_str());
	auto &core = SaveStateLANSync::Instance();
	std::string body = StringFromFormat("{\"requestId\":\"%s\",\"accept\":\"true\"}", requestId.c_str());
	std::string response;
	core.HandlePairRespond(body, response);
	INFO_LOG(Log::System, "LANPeerList: Accept response: %s", response.c_str());
	System_Toast("Pairing accepted!");
}

void LANPeerListScreen::RejectRequest(const std::string &requestId) {
	auto &core = SaveStateLANSync::Instance();
	std::string body = StringFromFormat("{\"requestId\":\"%s\",\"accept\":\"false\"}", requestId.c_str());
	std::string response;
	core.HandlePairRespond(body, response);
}

void LANPeerListScreen::ShowManualEntry() {
	auto &core = SaveStateLANSync::Instance();
	std::string myPin = core.GeneratePairingPin();
	int port = core.GetServerPort();
	System_Toast(StringFromFormat("Your PIN: %s - Port: %d", myPin.c_str(), port));

	RequesterToken token = GetRequesterToken();
	System_InputBoxGetString(token, "Enter peer IP:Port", "", false,
		[token](std::string_view addr, int) {
			if (addr.empty()) return;
			std::string peerAddr(addr);
			System_InputBoxGetString(token, "Enter peer's 6-digit PIN", "", false,
				[peerAddr](std::string_view pinVal, int) {
					if (pinVal.empty()) return;
					SaveStateLANSync::Instance().PairWithPeer(peerAddr, std::string(pinVal),
						[](bool ok, const std::string &err) {
							if (ok) System_Toast("Paired!");
							else System_Toast(("Failed: " + err).c_str());
						});
				}, nullptr);
		}, nullptr);
}

void LANPeerListScreen::OnCompleted(DialogResult result) {
}

// [PPSSPP-FORK] LANSync: Progress dialog implementation
void LANSyncProgressPopupScreen::SetProgress(int percent, int64_t bytesTransferred, int64_t totalBytes, const std::string &statusText) {
	percent_ = percent;
	bytesTransferred_ = bytesTransferred;
	totalBytes_ = totalBytes;
	statusText_ = statusText;
}

void LANSyncProgressPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto *content = new LinearLayout(ORIENT_VERTICAL);

	// Progress percentage
	std::string progressStr = StringFromFormat("%d%%", percent_);
	progressText_ = content->Add(new TextView(progressStr, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// Bytes transferred
	if (totalBytes_ > 0) {
		std::string bytesStr = StringFromFormat("%lld / %lld bytes", (long long)bytesTransferred_, (long long)totalBytes_);
		bytesText_ = content->Add(new TextView(bytesStr, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	}

	// Status text
	if (!statusText_.empty()) {
		statusView_ = content->Add(new TextView(statusText_, ALIGN_CENTER | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		statusView_->SetWordWrap();
	}

	// Progress bar (simple text-based)
	content->Add(new Spacer(10));
	std::string bar;
	int filled = percent_ / 5;
	for (int i = 0; i < 20; i++) {
		bar += (i < filled) ? "█" : "░";
	}
	content->Add(new TextView(bar, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	parent->Add(content);
}

void LANSyncProgressPopupScreen::update() {
	// Update display if progress changed
	if (progressText_) {
		progressText_->SetText(StringFromFormat("%d%%", percent_));
	}
	if (bytesText_ && totalBytes_ > 0) {
		bytesText_->SetText(StringFromFormat("%lld / %lld bytes", (long long)bytesTransferred_, (long long)totalBytes_));
	}
	if (statusView_ && !statusText_.empty()) {
		statusView_->SetText(statusText_);
	}

	// Auto-close when complete
	if (percent_ >= 100) {
		TriggerFinish(DR_OK);
	}
	UI::PopupScreen::update();
}

void LANSyncProgressPopupScreen::OnCompleted(DialogResult result) {
	// User cancelled or sync completed
}

// [PPSSPP-FORK] LANSync: Conflict resolution dialog implementation
LANSyncConflictPopupScreen::LANSyncConflictPopupScreen(const std::string &slotName, int64_t localTime, int64_t remoteTime, int64_t localSize, int64_t remoteSize)
	: UI::PopupScreen("Conflict Resolution", ""), slotName_(slotName), localTime_(localTime), remoteTime_(remoteTime), localSize_(localSize), remoteSize_(remoteSize) {
}

void LANSyncConflictPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	auto *content = new LinearLayout(ORIENT_VERTICAL);

	// Slot name
	TextView *slotText = content->Add(new TextView(slotName_, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	slotText->SetTextSize(TextSize::Big);

	// Info
	content->Add(new Spacer(10));
	std::string localInfo = StringFromFormat("Local: %lld bytes, modified %lld", (long long)localSize_, (long long)localTime_);
	std::string remoteInfo = StringFromFormat("Remote: %lld bytes, modified %lld", (long long)remoteSize_, (long long)remoteTime_);
	content->Add(new TextView(localInfo, ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	content->Add(new TextView(remoteInfo, ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	content->Add(new Spacer(20));

	// Resolution buttons
	auto *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	Choice *keepLocalBtn = buttonRow->Add(new Choice(n->T("Keep Local"), new LinearLayoutParams(1.0f)));
	keepLocalBtn->OnClick.Add([this](UI::EventParams &) {
		result_ = 0;
		TriggerFinish(DR_OK);
	});

	Choice *keepRemoteBtn = buttonRow->Add(new Choice(n->T("Keep Remote"), new LinearLayoutParams(1.0f)));
	keepRemoteBtn->OnClick.Add([this](UI::EventParams &) {
		result_ = 1;
		TriggerFinish(DR_OK);
	});

	Choice *keepBothBtn = buttonRow->Add(new Choice(n->T("Keep Both"), new LinearLayoutParams(1.0f)));
	keepBothBtn->OnClick.Add([this](UI::EventParams &) {
		result_ = 2;
		TriggerFinish(DR_OK);
	});

	Choice *skipBtn = buttonRow->Add(new Choice(n->T("Skip"), new LinearLayoutParams(1.0f)));
	skipBtn->OnClick.Add([this](UI::EventParams &) {
		result_ = 3;
		TriggerFinish(DR_OK);
	});

	content->Add(buttonRow);
	parent->Add(content);
}

void LANSyncConflictPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_CANCEL) {
		result_ = 3;  // Skip on cancel
	}
}

// [PPSSPP-FORK] LANSync: Server pairing screen implementation
void LANSyncServerPairingScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	auto *content = new LinearLayout(ORIENT_VERTICAL);

	// Get current PIN
	auto &core = SaveStateLANSync::Instance();
	currentPin_ = core.GeneratePairingPin();
	int port = core.GetServerPort();

	// PIN display
	content->Add(new TextView(n->T("Your PIN:"), ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	TextView *pinText = content->Add(new TextView(currentPin_, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	pinText->SetTextSize(TextSize::Big);

	content->Add(new Spacer(10));

	// Port info
	std::string portStr = StringFromFormat("Port: %d", port);
	content->Add(new TextView(portStr, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// QR code placeholder (will be updated in update())
	content->Add(new Spacer(10));
	content->Add(new TextView("QR Code: " + currentPin_, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// Change PIN button
	content->Add(new Spacer(10));
	Choice *changePinBtn = content->Add(new Choice(n->T("Change PIN")));
	changePinBtn->OnClick.Add([this](UI::EventParams &) {
		pinChanged_ = true;
		RecreateViews();
	});

	parent->Add(content);
}

void LANSyncServerPairingScreen::update() {
	if (pinChanged_.exchange(false)) {
		RecreateViews();
	}
	UI::PopupScreen::update();
}

void LANSyncServerPairingScreen::OnCompleted(DialogResult result) {
}

// [PPSSPP-FORK] LANSync: Large save warning dialog implementation
LANSyncLargeSaveWarningPopup::LANSyncLargeSaveWarningPopup(const std::string &slotName, int64_t sizeBytes)
	: UI::PopupScreen("Large Save Warning", ""), slotName_(slotName), sizeBytes_(sizeBytes) {
}

void LANSyncLargeSaveWarningPopup::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	auto *content = new LinearLayout(ORIENT_VERTICAL);

	// Warning text
	TextView *warnText = content->Add(new TextView(n->T("Large Save State"), ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	warnText->SetTextSize(TextSize::Big);

	content->Add(new Spacer(10));

	// Size info
	double sizeMB = (double)sizeBytes_ / (1024.0 * 1024.0);
	std::string sizeStr = StringFromFormat("%s: %.1f MB", slotName_.c_str(), sizeMB);
	content->Add(new TextView(sizeStr, ALIGN_CENTER, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	content->Add(new Spacer(10));

	// Warning message
	content->Add(new TextView(n->T("This may take a while to sync. Continue?"), ALIGN_CENTER | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// Confirm/Cancel buttons
	content->Add(new Spacer(20));
	auto *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	Choice *confirmBtn = buttonRow->Add(new Choice(n->T("Continue"), new LinearLayoutParams(1.0f)));
	confirmBtn->OnClick.Add([this](UI::EventParams &) {
		confirmed_ = true;
		TriggerFinish(DR_OK);
	});

	Choice *cancelBtn = buttonRow->Add(new Choice(n->T("Cancel"), new LinearLayoutParams(1.0f)));
	cancelBtn->OnClick.Add([this](UI::EventParams &) {
		confirmed_ = false;
		TriggerFinish(DR_CANCEL);
	});

	content->Add(buttonRow);
	parent->Add(content);
}

void LANSyncLargeSaveWarningPopup::OnCompleted(DialogResult result) {
	confirmed_ = (result == DR_OK);
}
