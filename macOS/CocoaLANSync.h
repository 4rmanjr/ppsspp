// [PPSSPP-FORK] LANSync: macOS Cocoa (AppKit) native UI
// Only add new lines. Do not delete/modify upstream lines.

#ifdef PPSSPP_LANSYNC

// PPSSPP Project - LAN Save State Sync
// macOS Cocoa (AppKit) native UI - Phase 9
// NSWindow, NSPanel, CIQRCodeGenerator, NSVisualEffectView
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#pragma once

// Phase 9: Full Cocoa UI
// Implementation in CocoaLANSync.mm provides:
//   - CocoaLANSyncServerPanel (NSPanel)   - server pairing with QR + PIN
//   - CocoaLANSyncSettingsWindow (NSWindow) - settings with NSTableView
//   - CocoaLANSyncProgressPanel (NSPanel) - sync progress with NSProgressIndicator
//   - ShowCocoaConflictDialog()           - NSAlert-based conflict resolution
//   - GenerateQRCode() via CIFilter        - zero external dependency
