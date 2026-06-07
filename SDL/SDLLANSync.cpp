// PPSSPP Project - LAN Save State Sync
// SDL ImGui UI implementation for LAN sync dialogs.
// Follows existing patterns from UI/ImDebugger/

#include "ppsspp_config.h"

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>

#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"

#include "SDL/SDLLANSync.h"
#include "SDL/LinuxLANSync.h"

#include "Core/SaveStateLANSync.h"
#include "Core/System.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/Path.h"

// Global instance (created externally, e.g. in SDLMain.cpp)
SDLLANSyncUI *g_LANSyncUI = nullptr;

SDLLANSyncUI::SDLLANSyncUI() {
	g_LANSyncUI = this;
}

SDLLANSyncUI::~SDLLANSyncUI() {
	g_LANSyncUI = nullptr;
}

void SDLLANSyncUI::OpenSettings() {
	settingsOpen_ = true;
}

void SDLLANSyncUI::OpenPairing() {
	pairingOpen_ = true;
	awaitingPIN_ = false;
	memset(ipBuf_, 0, sizeof(ipBuf_));
	memset(pinBuf_, 0, sizeof(pinBuf_));
	port_ = 0;
}

void SDLLANSyncUI::OpenServerPairing() {
	auto &core = SaveStateLANSync::Instance();
	pairingPin_ = core.GeneratePairingPin();
	serverPairingOpen_ = true;
}

void SDLLANSyncUI::StartSync(const std::string &peerId) {
	// Check for large save states before syncing
	largestSaveBytes_ = 0;
	largestSaveName_.clear();
	
	Path saveDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(saveDir, &files, ".ppst");
	
	for (const auto &f : files) {
		if ((int64_t)f.size > LARGE_SAVE_BYTES) {
			if ((int64_t)f.size > largestSaveBytes_) {
				largestSaveBytes_ = (int64_t)f.size;
				largestSaveName_ = f.name;
			}
		}
	}
	
	if (largestSaveBytes_ > LARGE_SAVE_BYTES) {
		// Show warning before syncing
		pendingSyncPeerId_ = peerId;
		showLargeSaveWarning_ = true;
		return;
	}
	
	// No large saves, proceed directly
	DoStartSync(peerId);
}

void SDLLANSyncUI::DoStartSync(const std::string &peerId) {
	progressOpen_ = true;
	currentPeer_ = peerId;
	completed_ = 0;
	total_ = 0;
	progress_ = 0.0f;
	slotLog_.clear();

	auto &core = SaveStateLANSync::Instance();
	core.SyncWithPeer(peerId,
		SaveStateLANSync::SyncDirection::BIDIRECTIONAL,
		[this](const SaveStateLANSync::SyncProgress &p) {
			completed_ = p.completedSlots;
			total_ = p.totalSlots;
			if (total_ > 0) progress_ = (float)completed_ / (float)total_;
			currentPeer_ = p.currentPeer;
		},
		[this](const SaveStateLANSync::SyncResult &r) {
			if (r.success) {
				slotLog_.push_back(StringFromFormat("Sync done: %d up, %d down, %d skipped",
					r.uploaded, r.downloaded, r.skipped));
			} else {
				slotLog_.push_back("Sync failed");
			}
		}
	);
}

void SDLLANSyncUI::UpdateProgress() {
	// Poll peer list every 5 seconds
	double now = time_now_d();
	if (now - lastPeerRefresh_ > 5.0) {
		cachedPeers_ = SaveStateLANSync::Instance().GetDiscoveredPeers();
		lastPeerRefresh_ = now;
	}
}

// ==================== Settings Window ====================

void SDLLANSyncUI::DrawSettingsWindow(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("LAN Save State Sync", open, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	auto &core = SaveStateLANSync::Instance();
	auto &lanSync = GetLinuxLANSync();

	// Enable/Disable toggle
	bool enabled = core.IsServerRunning();
	if (ImGui::Checkbox("Enable LAN Sync", &enabled)) {
		if (enabled) {
			lanSync.Enable("PPSSPP-Linux");
		} else {
			lanSync.Disable();
		}
	}

	ImGui::Separator();

	if (enabled) {
		ImGui::Text("Server port: %d", core.GetServerPort());

		// Local IPs
		ImGui::Text("Local addresses:");
		auto ips = lanSync.GetLocalIPs();
		for (const auto &ip : ips) {
			ImGui::BulletText("%s", ip.c_str());
		}
	}

	ImGui::Separator();

	// Paired devices
	ImGui::Text("Paired Devices:");
	ImGui::BeginChild("Peers", ImVec2(0, 100), true);

	auto peers = core.GetDiscoveredPeers();
	bool hasPeers = false;
	for (const auto &peer : peers) {
		if (!peer.paired) continue;
		hasPeers = true;

		const char *status = peer.online ? "Online" : "Offline";
		ImVec4 color = peer.online ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
		ImGui::TextColored(color, "%s %s (%s)", peer.online ? "O" : "X", peer.name.c_str(), status);
		ImGui::SameLine();
		if (ImGui::SmallButton(StringFromFormat("Unpair##%s", peer.id.c_str()).c_str())) {
			core.UnpairPeer(peer.id);
		}
	}
	if (!hasPeers) {
		ImGui::TextDisabled("No paired devices");
	}

	ImGui::EndChild();

	// Action buttons
	if (ImGui::Button("Pair New Device", ImVec2(150, 30))) {
		OpenPairing();
	}
	ImGui::SameLine();
	if (ImGui::Button("Sync Now", ImVec2(120, 30))) {
		// Start sync - will show warning if large saves detected
		if (!cachedPeers_.empty()) {
			for (const auto &p : cachedPeers_) {
				if (p.paired && p.online) {
					StartSync(p.id);
					break;
				}
			}
		}
	}

	// Show large save warning if triggered
	if (showLargeSaveWarning_) {
		bool warningOpen = true;
		DrawLargeSaveWarningDialog(&warningOpen);
		if (!warningOpen) showLargeSaveWarning_ = false;
	}

	ImGui::End();
}

// ==================== Pairing Dialog ====================

void SDLLANSyncUI::DrawPairingDialog(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Pair New Device", open, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	DrawAutoDiscoverSection();

	ImGui::Separator();
	ImGui::Text("Or enter manually:");
	DrawManualEntrySection();

	if (awaitingPIN_) {
		ImGui::Separator();
		DrawPINSection();
	}

	ImGui::End();
}

void SDLLANSyncUI::DrawAutoDiscoverSection() {
	ImGui::Text("Auto Discover");
	if (ImGui::Button("Refresh")) {
		cachedPeers_ = SaveStateLANSync::Instance().GetDiscoveredPeers();
		lastPeerRefresh_ = time_now_d();
	}

	ImGui::SameLine();
	ImGui::Text("(%d peers found)", (int)cachedPeers_.size());

	ImGui::BeginChild("Discovered", ImVec2(0, 120), true);

	bool hasUnpaired = false;
	for (const auto &peer : cachedPeers_) {
		if (peer.paired) continue;
		hasUnpaired = true;

		ImGui::Text("%s (%s)", peer.name.c_str(), peer.device.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton(StringFromFormat("Pair##%s", peer.id.c_str()).c_str())) {
			snprintf(ipBuf_, sizeof(ipBuf_), "%s", peer.host.c_str());
			port_ = peer.port;
			awaitingPIN_ = true;
		}
	}

	if (!hasUnpaired) {
		ImGui::TextDisabled("No unpaired devices found");
	}

	ImGui::EndChild();
}

void SDLLANSyncUI::DrawManualEntrySection() {
	ImGui::InputText("IP", ipBuf_, sizeof(ipBuf_),
	                 ImGuiInputTextFlags_CharsDecimal);
	ImGui::SameLine();
	ImGui::InputInt("Port", &port_, 1, 100, ImGuiInputTextFlags_CharsDecimal);

	if (ImGui::Button("Connect")) {
		if (strlen(ipBuf_) > 0 && port_ > 0) {
			awaitingPIN_ = true;
		}
	}
}

void SDLLANSyncUI::DrawPINSection() {
	ImGui::Text("Enter the PIN shown on the other device:");

	// 6-digit PIN input (numeric only)
	ImGui::PushItemWidth(200);
	ImGui::InputText("##pin", pinBuf_, sizeof(pinBuf_),
	                 ImGuiInputTextFlags_CharsDecimal);
	ImGui::PopItemWidth();

	if (ImGui::Button("Confirm")) {
		std::string pin(pinBuf_);
		if (pin.length() == 6) {
			auto &core = SaveStateLANSync::Instance();
			std::string peerId = std::string(ipBuf_) + ":" + std::to_string(port_);
			core.PairWithPeer(peerId, pin, [this](bool success, const std::string &error) {
				if (success) {
					memset(pinBuf_, 0, sizeof(pinBuf_));
					awaitingPIN_ = false;
					pairingOpen_ = false;
				}
			});
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		awaitingPIN_ = false;
		memset(pinBuf_, 0, sizeof(pinBuf_));
	}
}

// ==================== Server Pairing Screen ====================

void SDLLANSyncUI::DrawServerPairingScreen(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Pairing Mode", open, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	// PIN display
	ImGui::Text("PIN:");
	ImGui::Text("  %s", pairingPin_.c_str());

	ImGui::Separator();

	// QR Code (generated via libqrencode)
	ImGui::Text("QR Code:");
	auto &core = SaveStateLANSync::Instance();
	auto &lanSync2 = GetLinuxLANSync();
	auto ips = lanSync2.GetLocalIPs();
	std::string host = ips.empty() ? "0.0.0.0" : ips[0];
	std::string qrPayload = StringFromFormat(
		"ppsspp-sync://pair?host=%s&port=%d&fp=%s&pin=%s&name=%s",
		host.c_str(), core.GetServerPort(),
		core.GetCurrentPin().c_str(), pairingPin_.c_str(), "PPSSPP");
	
	std::vector<uint8_t> qrData = lanSync2.GenerateQRCode(qrPayload);
	if (!qrData.empty()) {
		// Render QR BMP as ImGui image
		// For now, display as text placeholder until texture loading is implemented
		ImGui::Text("QR Code generated (%d bytes)", (int)qrData.size());
		ImGui::Text("Scan with PPSSPP on another device");
	} else {
		ImGui::Text("QR generation failed - use PIN method");
	}

	ImGui::Separator();

	// Connection info
	ImGui::Text("Listening on:");
	for (const auto &ip : ips) {
		ImGui::BulletText("%s:%d", ip.c_str(), core.GetServerPort());
	}

	ImGui::Text("Fingerprint: ...");

	// Actions
	if (ImGui::Button("Change PIN", ImVec2(120, 30))) {
		pairingPin_ = core.GeneratePairingPin();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 30))) {
		core.CancelPairing();
		*open = false;
	}

	ImGui::End();
}

// ==================== Progress Dialog ====================

void SDLLANSyncUI::DrawProgressDialog(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Syncing##sync", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Peer: %s", currentPeer_.c_str());

	DrawProgressBar();

	ImGui::Separator();
	ImGui::Text(" %d / %d slots", completed_, total_);

	DrawSlotList();

	ImGui::Separator();

	auto &core = SaveStateLANSync::Instance();
	auto status = core.GetStatus();
	if (status == SaveStateLANSync::SyncStatus::SYNCING) {
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Syncing...");
		if (ImGui::Button("Cancel", ImVec2(100, 30))) {
			core.CancelSync();
		}
	} else if (status == SaveStateLANSync::SyncStatus::DONE) {
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Sync complete!");
		if (ImGui::Button("Close", ImVec2(100, 30))) {
			*open = false;
		}
	} else if (status == SaveStateLANSync::SyncStatus::ERROR) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Sync failed");
		ImGui::TextWrapped("The sync could not be completed. Check that both devices are on the same network and try again.");
		if (ImGui::Button("Retry", ImVec2(100, 30))) {
			if (!currentPeer_.empty()) {
				DoStartSync(currentPeer_);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(100, 30))) {
			*open = false;
		}
	} else if (status == SaveStateLANSync::SyncStatus::CANCELLED) {
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Sync cancelled");
		if (ImGui::Button("Close", ImVec2(100, 30))) {
			*open = false;
		}
	}

	ImGui::End();
}

void SDLLANSyncUI::DrawProgressBar() {
	ImVec2 barSize = ImVec2(-1, 20);
	ImVec2 barPos = ImGui::GetCursorScreenPos();

	// Background
	ImGui::GetWindowDrawList()->AddRectFilled(barPos,
		ImVec2(barPos.x + ImGui::GetContentRegionAvail().x, barPos.y + barSize.y),
		IM_COL32(60, 60, 60, 255));

	// Progress
	float barWidth = ImGui::GetContentRegionAvail().x * progress_;
	if (barWidth > 0) {
		ImGui::GetWindowDrawList()->AddRectFilled(barPos,
			ImVec2(barPos.x + barWidth, barPos.y + barSize.y),
			IM_COL32(50, 150, 50, 255));
	}

	// Percentage text
	char buf[16];
	snprintf(buf, sizeof(buf), "%.0f%%", progress_ * 100);
	ImVec2 textSize = ImGui::CalcTextSize(buf);
	ImVec2 textPos(barPos.x + (ImGui::GetContentRegionAvail().x - textSize.x) * 0.5f,
	               barPos.y + (barSize.y - textSize.y) * 0.5f);
	ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 255), buf);

	ImGui::Dummy(barSize);
}

void SDLLANSyncUI::DrawSlotList() {
	ImGui::BeginChild("Slots", ImVec2(0, 100), true);

	for (const auto &line : slotLog_) {
		ImGui::TextUnformatted(line.c_str());
	}

	if (slotLog_.empty()) {
		ImGui::TextDisabled("Waiting...");
	}

	ImGui::EndChild();
}

// ==================== Conflict Dialog ====================

void SDLLANSyncUI::DrawConflictDialog(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Sync Conflict", open, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Both this device and the peer have modified the save state independently.");
	ImGui::TextWrapped("Which version should be kept?");

	ImGui::Separator();

	ImGui::Text("This device:  14:30 (24.5 MB)");
	ImGui::Text("Remote (PC):  14:35 (24.7 MB)");

	ImGui::Separator();

	if (ImGui::Button("Keep Local", ImVec2(120, 30))) {
		// Resolve with KEEP_LOCAL
		*open = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Keep Remote", ImVec2(120, 30))) {
		*open = false;
	}

	if (ImGui::Button("Keep Both", ImVec2(120, 30))) {
		*open = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Skip", ImVec2(100, 30))) {
		*open = false;
	}

	ImGui::End();
}

// ==================== Large Save Warning Dialog ====================

void SDLLANSyncUI::DrawLargeSaveWarningDialog(bool *open) {
	if (!*open) return;

	ImGui::SetNextWindowSize(ImVec2(400, 220), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Large Save State", open, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	float sizeMB = largestSaveBytes_ / (1024.0f * 1024.0f);
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Warning: Large save state detected");
	ImGui::Separator();
	ImGui::Text("File: %s", largestSaveName_.c_str());
	ImGui::Text("Size: %.1f MB", sizeMB);
	ImGui::Separator();
	ImGui::TextWrapped("Syncing this file may take a while on slow networks.");
	ImGui::TextWrapped("Continue anyway?");
	ImGui::Separator();

	if (ImGui::Button("Continue Sync", ImVec2(140, 30))) {
		*open = false;
		showLargeSaveWarning_ = false;
		DoStartSync(pendingSyncPeerId_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 30))) {
		*open = false;
		showLargeSaveWarning_ = false;
		pendingSyncPeerId_.clear();
	}

	ImGui::End();
}

// ==================== Peer List Helper ====================

void SDLLANSyncUI::DrawPeerList() {
	auto &core = SaveStateLANSync::Instance();
	auto peers = core.GetDiscoveredPeers();

	for (const auto &peer : peers) {
		const char *icon = peer.device == "Android" ? "P" : "L";  // Phone / Laptop
		ImGui::Text("%s %s (%s:%d) %s",
			icon,
			peer.name.c_str(),
			peer.host.c_str(),
			peer.port,
			peer.online ? "Online" : "Offline");
	}
}
