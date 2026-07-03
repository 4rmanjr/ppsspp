// [PPSSPP-FORK] LANSync: Qt UI dialogs for LAN save state sync - Phase 7
// Only add new lines. Do not delete/modify upstream lines.

#ifdef PPSSPP_LANSYNC

#pragma once

class LANSyncQtUI {
public:
	// All methods create dialogs with Qt::WA_DeleteOnClose.
	// Call from Qt main thread only.
	static void ShowSettings();
	static void ShowPairing();
	static void ShowProgress();
};

#endif // PPSSPP_LANSYNC
