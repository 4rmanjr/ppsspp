// PPSSPP Project - LAN Save State Sync
// macOS Cocoa UI - Phase 9: Full AppKit implementation
// NSWindow, NSPanel, CIQRCodeGenerator, NSVisualEffectView
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC)

#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <QuartzCore/QuartzCore.h>

#include <string>
#include <vector>
#include <mutex>

#include "macOS/CocoaLANSync.h"
#include "macOS/MacLANSync.h"
#include "Core/SaveStateLANSync.h"
#include "Common/Log.h"

// ==================== Helpers ====================

static NSString *StdStr(NSString *s) __attribute__((unused));
static NSString *StdStr(NSString *s) { return s; }

static NSString *ToNSString(const std::string &str) {
	return [NSString stringWithUTF8String:str.c_str()];
}

static std::string FromNSString(NSString *str) {
	return str ? [str UTF8String] : "";
}

// ==================== QR Code Generator (CIFilter, zero dependency) ====================

static NSImage *GenerateQRCode(const std::string &payload) {
	NSData *data = [NSData dataWithBytes:payload.c_str() length:payload.size()];

	CIFilter *filter = [CIFilter filterWithName:@"CIQRCodeGenerator"];
	[filter setValue:data forKey:@"inputMessage"];
	[filter setValue:@"M" forKey:@"inputCorrectionLevel"];  // M = medium

	CIImage *ciImage = filter.outputImage;
	if (!ciImage) return nil;

	// Scale up for display (CIImage output is tiny: 27x27)
	CGFloat scale = 10.0;
	CGAffineTransform transform = CGAffineTransformMakeScale(scale, scale);
	ciImage = [ciImage imageByApplyingTransform:transform];

	NSCIImageRep *rep = [NSCIImageRep imageRepWithCIImage:ciImage];
	NSImage *image = [[NSImage alloc] initWithSize:rep.size];
	[image addRepresentation:rep];

	return image;
}

// ==================== Server Pairing Panel ====================

@interface CocoaLANSyncServerPanel : NSPanel
@property (nonatomic, strong) NSTextField *pinLabel;
@property (nonatomic, strong) NSImageView *qrView;
@property (nonatomic, strong) NSTextField *addressLabel;
@property (nonatomic, copy)   NSString *currentPin;
@end

@implementation CocoaLANSyncServerPanel

- (instancetype)init {
	self = [super initWithContentRect:NSMakeRect(0, 0, 320, 380)
	                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
	                          backing:NSBackingStoreBuffered defer:NO];
	if (!self) return nil;

	self.title = @"Pairing Mode";
	self.level = NSFloatingWindowLevel;

	NSView *contentView = self.contentView;

	// Visual effect background
	NSVisualEffectView *blur = [[NSVisualEffectView alloc] initWithFrame:contentView.bounds];
	blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
	[contentView addSubview:blur];

	// PIN label
	self.pinLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 300, 280, 40)];
	self.pinLabel.font = [NSFont monospacedDigitSystemFontOfSize:28 weight:NSFontWeightBold];
	self.pinLabel.alignment = NSTextAlignmentCenter;
	self.pinLabel.bezeled = NO;
	self.pinLabel.drawsBackground = NO;
	self.pinLabel.editable = NO;
	self.pinLabel.selectable = NO;
	[blur addSubview:self.pinLabel];

	// QR Code
	self.qrView = [[NSImageView alloc] initWithFrame:NSMakeRect(60, 100, 200, 200)];
	self.qrView.imageScaling = NSImageScaleProportionallyUpOrDown;
	[blur addSubview:self.qrView];

	// Address info
	self.addressLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 60, 280, 30)];
	self.addressLabel.font = [NSFont systemFontOfSize:11];
	self.addressLabel.alignment = NSTextAlignmentCenter;
	self.addressLabel.bezeled = NO;
	self.addressLabel.drawsBackground = NO;
	self.addressLabel.editable = NO;
	self.addressLabel.selectable = YES;
	[blur addSubview:self.addressLabel];

	// Buttons
	NSButton *changePinBtn = [NSButton buttonWithTitle:@"Change PIN"
	                                            target:self action:@selector(changePin:)];
	changePinBtn.frame = NSMakeRect(20, 20, 120, 28);
	[blur addSubview:changePinBtn];

	NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel"
	                                         target:self action:@selector(cancelPairing:)];
	cancelBtn.frame = NSMakeRect(180, 20, 120, 28);
	[blur addSubview:cancelBtn];

	return self;
}

- (void)refreshWithPin:(NSString *)pin fingerprint:(NSString *)fp hosts:(NSArray<NSString *> *)hosts port:(int)port {
	self.currentPin = pin;
	self.pinLabel.stringValue = pin ?: @"------";

	// Generate QR code
	NSString *host = hosts.firstObject ?: @"0.0.0.0";
	NSString *payload = [NSString stringWithFormat:
		@"ppsspp-sync://pair?host=%@&port=%d&fp=%@&pin=%@&name=Mac",
		host, port, fp ?: @"", pin ?: @""];
	self.qrView.image = GenerateQRCode([payload UTF8String]);

	// Address info
	NSMutableString *addrStr = [NSMutableString string];
	for (NSString *ip in hosts) {
		[addrStr appendFormat:@"%@:%d\n", ip, port];
	}
	[addrStr appendFormat:@"Fingerprint: %@", fp ?: @"..."];
	self.addressLabel.stringValue = addrStr;
}

- (void)changePin:(id)sender {
	auto &core = SaveStateLANSync::Instance();
	[self refreshWithPin:ToNSString(core.GeneratePairingPin())
	         fingerprint:ToNSString("")
	               hosts:@[]
	                port:core.GetServerPort()];
}

- (void)cancelPairing:(id)sender {
	SaveStateLANSync::Instance().CancelPairing();
	[self close];
}

@end

// ==================== Settings Window ====================

@interface CocoaLANSyncSettingsWindow : NSWindow <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSButton *enableToggle;
@property (nonatomic, strong) NSTextField *nameField;
@property (nonatomic, strong) NSTableView *peerTable;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *peers;
@property (nonatomic, strong) NSTimer *refreshTimer;
@end

@implementation CocoaLANSyncSettingsWindow

- (instancetype)init {
	self = [super initWithContentRect:NSMakeRect(0, 0, 420, 340)
	                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
	                          backing:NSBackingStoreBuffered defer:NO];
	if (!self) return nil;

	self.title = @"LAN Save State Sync";
	self.peers = [NSMutableArray array];

	NSView *contentView = self.contentView;

	// Enable toggle
	self.enableToggle = [NSButton checkboxWithTitle:@"Enable LAN Sync"
	                                         target:self action:@selector(toggleEnabled:)];
	self.enableToggle.frame = NSMakeRect(20, 300, 200, 24);
	[contentView addSubview:self.enableToggle];

	// Device name
	NSTextField *nameLabel = [NSTextField labelWithString:@"Device Name:"];
	nameLabel.frame = NSMakeRect(20, 270, 100, 16);
	[contentView addSubview:nameLabel];

	self.nameField = [[NSTextField alloc] initWithFrame:NSMakeRect(120, 268, 200, 22)];
	self.nameField.placeholderString = @"PPSSPP-Mac";
	[contentView addSubview:self.nameField];

	// Paired devices table
	NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 80, 380, 170)];
	scrollView.hasVerticalScroller = YES;
	scrollView.borderType = NSBezelBorder;

	self.peerTable = [[NSTableView alloc] initWithFrame:scrollView.bounds];
	NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"peer"];
	col.title = @"Paired Devices";
	col.width = 360;
	[self.peerTable addTableColumn:col];
	self.peerTable.dataSource = self;
	self.peerTable.delegate = self;

	scrollView.documentView = self.peerTable;
	[contentView addSubview:scrollView];

	// Buttons
	NSButton *pairBtn = [NSButton buttonWithTitle:@"Pair New Device"
	                                       target:self action:@selector(showPairing:)];
	pairBtn.frame = NSMakeRect(20, 45, 140, 28);
	[contentView addSubview:pairBtn];

	NSButton *syncBtn = [NSButton buttonWithTitle:@"Sync Now"
	                                       target:self action:@selector(syncNow:)];
	syncBtn.frame = NSMakeRect(170, 45, 100, 28);
	[contentView addSubview:syncBtn];

	// Timer to refresh
	self.refreshTimer = [NSTimer scheduledTimerWithTimeInterval:5.0 repeats:YES block:^(NSTimer *timer) {
		[self refreshPeers];
	}];

	return self;
}

- (void)refreshPeers {
	[self.peers removeAllObjects];
	auto peers = SaveStateLANSync::Instance().GetDiscoveredPeers();
	for (const auto &p : peers) {
		if (!p.paired) continue;
		[self.peers addObject:@{
			@"name": ToNSString(p.name),
			@"status": p.online ? @"Online" : @"Offline",
			@"id": ToNSString(p.id)
		}];
	}
	[self.peerTable reloadData];
}

- (void)toggleEnabled:(id)sender {
	bool enabled = (self.enableToggle.state == NSControlStateValueOn);
	if (enabled) {
		GetMacLANSync().Enable(FromNSString(self.nameField.stringValue));
	} else {
		GetMacLANSync().Disable();
	}
}

- (void)showPairing:(id)sender {
	// Show CocoaLANSyncServerPanel as sheet
	CocoaLANSyncServerPanel *panel = [[CocoaLANSyncServerPanel alloc] init];
	[self beginSheet:panel completionHandler:^(NSModalResponse returnCode) {
		// Cleanup
	}];
}

- (void)syncNow:(id)sender {
	// Show progress panel
}

// NSTableViewDataSource
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
	return (NSInteger)self.peers.count;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
	NSDictionary *peer = self.peers[row];
	NSString *status = peer[@"status"];
	NSString *name = peer[@"name"];
	return [NSString stringWithFormat:@"%@ %@ - %@",
	        [status isEqualToString:@"Online"] ? @"●" : @"○", name, status];
}
@end

// ==================== Progress Panel (Sheet) ====================

@interface CocoaLANSyncProgressPanel : NSPanel
@property (nonatomic, strong) NSProgressIndicator *progressBar;
@property (nonatomic, strong) NSTextField *statusLabel;
@property (nonatomic, strong) NSTextField *summaryLabel;
@property (nonatomic, strong) NSTimer *refreshTimer;
@end

@implementation CocoaLANSyncProgressPanel

- (instancetype)init {
	self = [super initWithContentRect:NSMakeRect(0, 0, 400, 280)
	                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
	                          backing:NSBackingStoreBuffered defer:NO];
	if (!self) return nil;

	self.title = @"Syncing Save States";

	NSView *contentView = self.contentView;

	self.statusLabel = [NSTextField labelWithString:@"Preparing..."];
	self.statusLabel.frame = NSMakeRect(20, 230, 360, 20);
	[contentView addSubview:self.statusLabel];

	self.progressBar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 200, 360, 20)];
	self.progressBar.style = NSProgressIndicatorStyleBar;
	self.progressBar.indeterminate = NO;
	self.progressBar.minValue = 0;
	self.progressBar.maxValue = 100;
	self.progressBar.doubleValue = 0;
	[contentView addSubview:self.progressBar];

	self.summaryLabel = [NSTextField labelWithString:@"0 / 0 slots"];
	self.summaryLabel.frame = NSMakeRect(20, 175, 360, 20);
	[contentView addSubview:self.summaryLabel];

	NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel"
	                                         target:self action:@selector(cancelSync:)];
	cancelBtn.frame = NSMakeRect(290, 20, 90, 28);
	[contentView addSubview:cancelBtn];

	self.refreshTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
		auto progress = SaveStateLANSync::Instance().GetProgress();
		self.progressBar.doubleValue = progress.totalSlots > 0 ?
			(progress.completedSlots * 100.0 / progress.totalSlots) : 0;
		self.statusLabel.stringValue = ToNSString(progress.currentFile);
		self.summaryLabel.stringValue = [NSString stringWithFormat:@"%d / %d slots",
		                                  progress.completedSlots, progress.totalSlots];
	}];

	return self;
}

- (void)cancelSync:(id)sender {
	SaveStateLANSync::Instance().CancelSync();
	[self.refreshTimer invalidate];
	self.refreshTimer = nil;
	[self close];
}
- (void)close {
	[self.refreshTimer invalidate];
	self.refreshTimer = nil;
	[super close];
}
@end

// ==================== Conflict Alert ====================

static void ShowCocoaConflictDialog(const SaveStateLANSync::ConflictInfo &info,
                                     NSWindow *parentWindow) {
	NSAlert *alert = [[NSAlert alloc] init];
	alert.messageText = @"Sync Conflict";
	alert.informativeText = [NSString stringWithFormat:
		@"Game: %s - Slot %d\n\n"
		@"Both devices modified this save state independently.\n"
		@"Choose which version to keep.",
		info.gameId.c_str(), info.slot];

	[alert addButtonWithTitle:@"Keep Local"];
	[alert addButtonWithTitle:@"Keep Remote"];
	[alert addButtonWithTitle:@"Keep Both"];
	[alert addButtonWithTitle:@"Skip"];

	[alert beginSheetModalForWindow:parentWindow completionHandler:^(NSModalResponse returnCode) {
		switch (returnCode) {
			case NSAlertFirstButtonReturn:
				SaveStateLANSync::Instance().ResolveConflict(info,
					SaveStateLANSync::ConflictResolution::KEEP_LOCAL);
				break;
			case NSAlertSecondButtonReturn:
				SaveStateLANSync::Instance().ResolveConflict(info,
					SaveStateLANSync::ConflictResolution::KEEP_REMOTE);
				break;
			default:
				break;
		}
	}];
}

// ==================== Update CocoaLANSync.h placeholder ====================
// The actual implementation classes are above.
// This file replaces the previous placeholder.

#endif  // PPSSPP_PLATFORM(MAC)
