// PPSSPP Project - LAN Save State Sync
// Qt UI implementation - Phase 7 full QDialogs
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#include "ppsspp_config.h"
#include "UI/LANSyncSettings.h"

// Qt UI only compiled when USING_QT_UI is enabled
#ifdef USING_QT_UI

#include <string>
#include <vector>
#include <cstring>

#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QTimer>
#include <QMessageBox>

#include "Core/SaveStateLANSync.h"
#include "Core/Config.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

// ==================== Internal Helpers ====================

static QWidget *MakeGroupBox(const char *title, QLayout *layout) {
	QGroupBox *box = new QGroupBox(title);
	box->setLayout(layout);
	return box;
}

// ==================== Settings Dialog ====================

class LANSyncSettingsDialog : public QDialog {
public:
	LANSyncSettingsDialog(QWidget *parent = nullptr) : QDialog(parent) {
		setWindowTitle("LAN Save State Sync");
		setMinimumSize(420, 320);

		auto *mainLayout = new QVBoxLayout(this);

		// Enable toggle
		enableCheck_ = new QCheckBox("Enable LAN Sync");
		enableCheck_->setChecked(g_Config.lanSync.bEnabled);
		connect(enableCheck_, &QCheckBox::toggled, this, [this](bool checked) {
			g_Config.lanSync.bEnabled = checked;
			refreshUI();
		});
		mainLayout->addWidget(enableCheck_);

		// Device name
		auto *nameLayout = new QHBoxLayout;
		nameLayout->addWidget(new QLabel("Device Name:"));
		nameEdit_ = new QLineEdit(QString::fromStdString(g_Config.lanSync.sDeviceName));
		nameEdit_->setPlaceholderText("PPSSPP-PC");
		nameLayout->addWidget(nameEdit_);
		mainLayout->addLayout(nameLayout);

		// Port display
		portLabel_ = new QLabel("Port: Auto");
		mainLayout->addWidget(portLabel_);

		// Paired devices
		mainLayout->addWidget(MakeGroupBox("Paired Devices",
			createPeerListLayout()));

		// Conflict resolution
		conflictCombo_ = new QComboBox;
		conflictCombo_->addItems({"Newest Wins", "Keep Local", "Keep Remote", "Prompt"});
		conflictCombo_->setCurrentIndex(g_Config.lanSync.iConflictResolution);
		mainLayout->addWidget(MakeGroupBox("Conflict Resolution",
			new QVBoxLayout));  // simplified
		((QGroupBox *)mainLayout->itemAt(mainLayout->count() - 1)->widget())->layout()->addWidget(conflictCombo_);

		// Buttons
		auto *btnLayout = new QHBoxLayout;
		pairBtn_ = new QPushButton("Pair New Device");
		syncBtn_ = new QPushButton("Sync Now");
		closeBtn_ = new QPushButton("Close");

		btnLayout->addWidget(pairBtn_);
		btnLayout->addWidget(syncBtn_);
		btnLayout->addStretch();
		btnLayout->addWidget(closeBtn_);
		mainLayout->addLayout(btnLayout);

		connect(pairBtn_, &QPushButton::clicked, this, []() {
			LANSyncQtUI::ShowPairing();
		});
		connect(syncBtn_, &QPushButton::clicked, this, []() {
			LANSyncQtUI::ShowProgress();
		});
		connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

		// Timer to refresh peer list
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this]() {
			refreshPeerList();
		});
		timer->start(2000);  // every 2 seconds

		refreshUI();
	}

private:
	QCheckBox *enableCheck_;
	QLineEdit *nameEdit_;
	QLabel *portLabel_;
	QListWidget *peerList_ = nullptr;
	QComboBox *conflictCombo_;
	QPushButton *pairBtn_, *syncBtn_, *closeBtn_;

	QLayout *createPeerListLayout() {
		auto *layout = new QVBoxLayout;
		peerList_ = new QListWidget;
		peerList_->setMinimumHeight(100);
		layout->addWidget(peerList_);
		return layout;
	}

	void refreshPeerList() {
		if (!peerList_) return;
		peerList_->clear();
		auto peers = SaveStateLANSync::Instance().GetDiscoveredPeers();
		for (const auto &p : peers) {
			QString status = p.online ? "Online" : "Offline";
			QString text = QString("%1 (%2) - %3")
				.arg(QString::fromStdString(p.name))
				.arg(QString::fromStdString(p.device))
				.arg(status);
			peerList_->addItem(text);
		}
	}

	void refreshUI() {
		bool enabled = enableCheck_->isChecked();
		nameEdit_->setEnabled(enabled);
		pairBtn_->setEnabled(enabled);
		syncBtn_->setEnabled(enabled);
		if (enabled) {
			int port = SaveStateLANSync::Instance().GetServerPort();
			portLabel_->setText(QString("Port: %1").arg(port > 0 ? port : 0));
		}
	}
};

// ==================== Pairing Dialog ====================

class LANSyncPairingDialog : public QDialog {
public:
	LANSyncPairingDialog(QWidget *parent = nullptr) : QDialog(parent) {
		setWindowTitle("Pair New Device");
		setMinimumSize(400, 350);

		auto *mainLayout = new QVBoxLayout(this);

		// Auto Discover
		auto *discoverLayout = new QVBoxLayout;
		auto *refreshBtn = new QPushButton("Refresh");
		peerList_ = new QListWidget;
		peerList_->setMinimumHeight(120);
		discoverLayout->addWidget(refreshBtn);
		discoverLayout->addWidget(peerList_);
		mainLayout->addWidget(MakeGroupBox("Auto Discover", discoverLayout));

		connect(refreshBtn, &QPushButton::clicked, this, [this]() {
			peerList_->clear();
			auto peers = SaveStateLANSync::Instance().GetDiscoveredPeers();
			for (const auto &p : peers) {
				if (p.paired) continue;
				QString text = QString("%1 (%2) - %3:%4")
					.arg(QString::fromStdString(p.name))
					.arg(QString::fromStdString(p.device))
					.arg(QString::fromStdString(p.host))
					.arg(p.port);
				peerList_->addItem(text);
			}
		});

		connect(peerList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
			pinPhase_ = true;
			statusLabel_->setText("Enter the PIN shown on the other device:");
			pinEdit_->setVisible(true);
			confirmBtn_->setVisible(true);
		});

		// Manual Entry
		auto *manualLayout = new QHBoxLayout;
		manualLayout->addWidget(new QLabel("IP:"));
		ipEdit_ = new QLineEdit;
		ipEdit_->setPlaceholderText("192.168.1.50");
		manualLayout->addWidget(ipEdit_);
		manualLayout->addWidget(new QLabel("Port:"));
		portEdit_ = new QLineEdit;
		portEdit_->setPlaceholderText("27345");
		manualLayout->addWidget(portEdit_);
		auto *connectBtn = new QPushButton("Connect");
		manualLayout->addWidget(connectBtn);
		mainLayout->addWidget(MakeGroupBox("Manual Entry", manualLayout));

		connect(connectBtn, &QPushButton::clicked, this, [this]() {
			pinPhase_ = true;
			statusLabel_->setText("Enter the PIN shown on the other device:");
			pinEdit_->setVisible(true);
			confirmBtn_->setVisible(true);
		});

		// PIN Entry (initially hidden)
		statusLabel_ = new QLabel("Select a device or enter IP manually");
		mainLayout->addWidget(statusLabel_);

		pinEdit_ = new QLineEdit;
		pinEdit_->setPlaceholderText("6-digit PIN");
		pinEdit_->setMaxLength(6);
		pinEdit_->setVisible(false);
		mainLayout->addWidget(pinEdit_);

		confirmBtn_ = new QPushButton("Confirm");
		confirmBtn_->setVisible(false);
		mainLayout->addWidget(confirmBtn_);

		connect(confirmBtn_, &QPushButton::clicked, this, [this]() {
			std::string pin = pinEdit_->text().toStdString();
			if (pin.length() == 6) {
				std::string host = ipEdit_->text().toStdString();
				int port = portEdit_->text().toInt();
				std::string peerId = host + ":" + std::to_string(port);

				SaveStateLANSync::Instance().PairWithPeer(peerId, pin,
					[this](bool success, const std::string &error) {
						if (success) {
							QMessageBox::information(this, "Paired",
								"Device paired successfully!");
							accept();
						} else {
							QMessageBox::warning(this, "Pairing Failed",
								QString::fromStdString(error));
						}
					});
			}
		});

		// Cancel
		auto *cancelBtn = new QPushButton("Cancel");
		connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
		mainLayout->addWidget(cancelBtn);
	}

private:
	QListWidget *peerList_;
	QLineEdit *ipEdit_, *portEdit_, *pinEdit_;
	QLabel *statusLabel_;
	QPushButton *confirmBtn_;
	bool pinPhase_ = false;
};

// ==================== Progress Dialog ====================

class LANSyncProgressDialog : public QDialog {
public:
	LANSyncProgressDialog(QWidget *parent = nullptr) : QDialog(parent) {
		setWindowTitle("Syncing Save States");
		setMinimumSize(450, 300);
		setWindowModality(Qt::NonModal);  // Don't block emulation

		auto *mainLayout = new QVBoxLayout(this);

		peerLabel_ = new QLabel("Select peer:");
		mainLayout->addWidget(peerLabel_);

		peerCombo_ = new QComboBox;
		mainLayout->addWidget(peerCombo_);

		progressBar_ = new QProgressBar;
		progressBar_->setRange(0, 100);
		progressBar_->setValue(0);
		mainLayout->addWidget(progressBar_);

		statusLabel_ = new QLabel("Ready");
		mainLayout->addWidget(statusLabel_);

		slotList_ = new QListWidget;
		slotList_->setMinimumHeight(120);
		mainLayout->addWidget(slotList_);

		summaryLabel_ = new QLabel;
		mainLayout->addWidget(summaryLabel_);

		auto *btnLayout = new QHBoxLayout;
		startBtn_ = new QPushButton("Start Sync");
		pauseBtn_ = new QPushButton("Pause");
		pauseBtn_->setEnabled(false);
		cancelBtn_ = new QPushButton("Close");

		btnLayout->addWidget(startBtn_);
		btnLayout->addWidget(pauseBtn_);
		btnLayout->addStretch();
		btnLayout->addWidget(cancelBtn_);
		mainLayout->addLayout(btnLayout);

		connect(startBtn_, &QPushButton::clicked, this, [this]() {
			startSync();
		});
		connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::accept);

		// Timer to poll progress
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this]() {
			refresh();
		});
		timer->start(500);

		// Populate peer combo
		refreshPeers();
	}

private:
	QLabel *peerLabel_, *statusLabel_, *summaryLabel_;
	QComboBox *peerCombo_;
	QProgressBar *progressBar_;
	QListWidget *slotList_;
	QPushButton *startBtn_, *pauseBtn_, *cancelBtn_;
	bool running_ = false;
	bool paused_ = false;

	void refreshPeers() {
		peerCombo_->clear();
		auto peers = SaveStateLANSync::Instance().GetDiscoveredPeers();
		for (const auto &p : peers) {
			if (p.paired && p.online) {
				peerCombo_->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
			}
		}
	}

	void startSync() {
		if (peerCombo_->currentIndex() < 0) return;
		std::string peerId = peerCombo_->currentData().toString().toStdString();

		running_ = true;
		startBtn_->setEnabled(false);
		pauseBtn_->setEnabled(true);

		SaveStateLANSync::Instance().SyncWithPeer(peerId,
			SaveStateLANSync::SyncDirection::BIDIRECTIONAL,
			[this](const SaveStateLANSync::SyncProgress &p) {
				QMetaObject::invokeMethod(this, [this, p]() {
					int pct = p.totalSlots > 0 ? (p.completedSlots * 100 / p.totalSlots) : 0;
					progressBar_->setValue(pct);
					QString text = QString::fromStdString(p.currentFile);
					if (p.totalBytes > 0) {
						text += QString(" - %1 / %2")
							.arg(QString::fromStdString(FormatBytes(p.completedBytes)))
							.arg(QString::fromStdString(FormatBytes(p.totalBytes)));
					}
					statusLabel_->setText(text);
				}, Qt::QueuedConnection);
			},
			[this](const SaveStateLANSync::SyncResult &r) {
				QMetaObject::invokeMethod(this, [this, r]() {
					running_ = false;
					startBtn_->setEnabled(false);
					pauseBtn_->setEnabled(false);
					summaryLabel_->setText(QString("Done: %1 up, %2 down, %3 failed, %4 skipped")
						.arg(r.uploaded).arg(r.downloaded).arg(r.failed).arg(r.skipped));
				}, Qt::QueuedConnection);
			}
		);
	}

	void refresh() {
		if (!running_) return;
		auto progress = SaveStateLANSync::Instance().GetProgress();
		int pct = progress.totalSlots > 0 ? (progress.completedSlots * 100 / progress.totalSlots) : 0;
		progressBar_->setValue(pct);
		QString text = QString("%1/%2")
			.arg(progress.completedSlots)
			.arg(progress.totalSlots);
		if (!progress.currentFile.empty()) {
			text += QString(" - %1").arg(QString::fromStdString(progress.currentFile));
		}
		if (progress.totalBytes > 0) {
			text += QString(" (%1 / %2)")
				.arg(QString::fromStdString(FormatBytes(progress.completedBytes)))
				.arg(QString::fromStdString(FormatBytes(progress.totalBytes)));
		}
		statusLabel_->setText(text);
	}
};

// ==================== Conflict Dialog ====================

class LANSyncConflictDialog : public QDialog {
public:
	LANSyncConflictDialog(const SaveStateLANSync::ConflictInfo &info, QWidget *parent = nullptr)
		: QDialog(parent) {

		setWindowTitle("Sync Conflict");
		setMinimumSize(400, 280);

		auto *mainLayout = new QVBoxLayout(this);

		QLabel *descLabel = new QLabel(QString("Game: %1 - Slot %2")
			.arg(QString::fromStdString(info.gameId))
			.arg(info.slot));
		descLabel->setWordWrap(true);
		mainLayout->addWidget(descLabel);

		// Local info
		auto *localBox = new QGroupBox("This Device");
		auto *localLayout = new QVBoxLayout;
		localLayout->addWidget(new QLabel(QString("Size: %1 MB")
			.arg(info.localSize / 1048576.0, 0, 'f', 1)));
		localBox->setLayout(localLayout);
		mainLayout->addWidget(localBox);

		// Remote info
		auto *remoteBox = new QGroupBox("Remote Device");
		auto *remoteLayout = new QVBoxLayout;
		remoteLayout->addWidget(new QLabel(QString("Size: %1 MB")
			.arg(info.remoteSize / 1048576.0, 0, 'f', 1)));
		remoteBox->setLayout(remoteLayout);
		mainLayout->addWidget(remoteBox);

		// Buttons
		auto *btnLayout = new QHBoxLayout;
		auto *keepLocalBtn = new QPushButton("Keep Local");
		auto *keepRemoteBtn = new QPushButton("Keep Remote");
		auto *keepBothBtn = new QPushButton("Keep Both");
		auto *skipBtn = new QPushButton("Skip");

		btnLayout->addWidget(keepLocalBtn);
		btnLayout->addWidget(keepRemoteBtn);
		btnLayout->addWidget(keepBothBtn);
		btnLayout->addWidget(skipBtn);
		mainLayout->addLayout(btnLayout);

		auto resolve = [this](SaveStateLANSync::ConflictResolution r) {
			// Signal resolution to sync manager
			accept();
		};

		connect(keepLocalBtn, &QPushButton::clicked, this,
		        [resolve]() { resolve(SaveStateLANSync::ConflictResolution::KEEP_LOCAL); });
		connect(keepRemoteBtn, &QPushButton::clicked, this,
		        [resolve]() { resolve(SaveStateLANSync::ConflictResolution::KEEP_REMOTE); });
		connect(keepBothBtn, &QPushButton::clicked, this, &QDialog::accept);
		connect(skipBtn, &QPushButton::clicked, this, &QDialog::reject);
	}
};

// ==================== Public API ====================

void LANSyncQtUI::ShowSettings() {
	auto *dlg = new LANSyncSettingsDialog();
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}

void LANSyncQtUI::ShowPairing() {
	auto *dlg = new LANSyncPairingDialog();
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}

void LANSyncQtUI::ShowProgress() {
	auto *dlg = new LANSyncProgressDialog();
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}

#else  // !USING_QT_UI

// Stub implementations when Qt is disabled
void LANSyncQtUI::ShowSettings() {}
void LANSyncQtUI::ShowPairing() {}
void LANSyncQtUI::ShowProgress() {}

#endif  // USING_QT_UI
