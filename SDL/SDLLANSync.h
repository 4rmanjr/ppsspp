// PPSSPP Project - LAN Save State Sync
// SDL ImGui UI dialogs for LAN save state sync
//
// Rendered via Dear ImGui in the SDL frontend.
// Drawer functions are called from SDLMain.cpp's render loop.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

#include <string>
#include <vector>
#include <functional>

#include "SDL/LinuxLANSync.h"

// SDL ImGui-based UI for LAN sync.
// All draw functions return true if the window should remain open.
// Called from SDLMain.cpp top-level render loop.

class SDLLANSyncUI {
public:
	SDLLANSyncUI();
	~SDLLANSyncUI();

	// === Main Draw Functions ===

	// Settings → Network → LAN Sync panel
	// Call inside Popup or Settings screen
	void DrawSettingsWindow(bool *open);

	// Pair New Device dialog
	void DrawPairingDialog(bool *open);

	// Server pairing screen (shows PIN + QR)
	void DrawServerPairingScreen(bool *open);

	// Sync progress dialog
	void DrawProgressDialog(bool *open);

	// Conflict resolution dialog
	void DrawConflictDialog(bool *open);

	// Large save warning dialog
	void DrawLargeSaveWarningDialog(bool *open);

	// Called from SDLMain.cpp background thread polling
	void UpdateProgress();

	// === Actions ===

	void OpenSettings();
	void OpenPairing();
	void OpenServerPairing();

	// Start sync with selected peer
	void StartSync(const std::string &peerId);

private:
	void DoStartSync(const std::string &peerId);
	void DrawPeerList();
	void DrawAutoDiscoverSection();
	void DrawManualEntrySection();
	void DrawPINSection();
	void DrawProgressBar();
	void DrawSlotList();

public:
	// UI state (public for SDLMain.cpp render loop)
	bool settingsOpen_ = false;
	bool pairingOpen_ = false;
	bool serverPairingOpen_ = false;
	bool progressOpen_ = false;
	bool conflictOpen_ = false;
	bool showLargeSaveWarning_ = false;

private:
	// Pairing state
	bool awaitingPIN_ = false;
	char ipBuf_[64] = {};
	int port_ = 0;
	char pinBuf_[8] = {};
	std::string pairingPin_;

	// Sync progress
	float progress_ = 0.0f;
	int completed_ = 0;
	int total_ = 0;
	std::string currentPeer_;
	std::vector<std::string> slotLog_;

	// Large save warning
	static constexpr int64_t LARGE_SAVE_BYTES = 50 * 1024 * 1024;  // 50MB
	std::string pendingSyncPeerId_;
	int64_t largestSaveBytes_ = 0;
	std::string largestSaveName_;

	// Peers (cached)
	std::vector<SaveStateLANSync::PeerInfo> cachedPeers_;
	double lastPeerRefresh_ = 0.0;
};

// Global instance (created in SDLMain.cpp)
extern SDLLANSyncUI *g_LANSyncUI;
