// PPSSPP Project - LAN Save State Sync
// Qt UI dialogs for LAN save state sync - Phase 7
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

class LANSyncQtUI {
public:
	// All methods create dialogs with Qt::WA_DeleteOnClose.
	// Call from Qt main thread only.
	static void ShowSettings();
	static void ShowPairing();
	static void ShowProgress();
};
