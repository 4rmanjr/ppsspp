#include "ppsspp_config.h"

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
	// Auto-refresh every 3 seconds
	double now = time_now_d();
	if (now - lastRefresh_ > 3.0) {
		RefreshPeers();
		RecreateViews();
	}
	UI::PopupScreen::update();
}

void LANPeerListScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	double now = time_now_d();
	if (now - lastRefresh_ > 3.0) {
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
	if (!pending_.empty()) {
		content->Add(new ItemHeader(n->T("Pending Requests")));
		for (const auto &req : pending_) {
			auto *item = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4)));
			std::string label = req.peerName + " wants to pair";
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

	peers_ = SaveStateLANSync::Instance().GetDiscoveredPeers();
	bool hasPeers = false;
	for (const auto &peer : peers_) {
		if (peer.paired) continue;
		if (peer.id.empty()) continue;
		hasPeers = true;

		auto *item = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4)));
		std::string label = StringFromFormat("%s (%s)  %s:%d",
			peer.name.c_str(), peer.device.c_str(),
			peer.host.c_str(), peer.port);
		auto *text = item->Add(new TextView(label, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		text->SetWordWrap();

		auto *buttonRow = item->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		Choice *pairBtn = buttonRow->Add(new Choice(n->T("Pair"), new LinearLayoutParams(1.0f)));
		pairBtn->OnClick.Add([this, peer](UI::EventParams &) {
			SendPairRequest(peer);
		});

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
		[](bool success, const std::string &error) {
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
