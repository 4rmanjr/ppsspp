#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include "LANSync/LANSyncProtocol.h"
#include "LANSync/LANSyncDiscovery.h"
#include "Common/UI/Screen.h"
#include "Common/File/Path.h"

namespace LANSync {

class LANSyncServer;
class LANSyncClient;
class LANSyncDiscovery;
class PairingManager;
class TLSContext;

class SaveStateLANSync {
public:
  SaveStateLANSync();
  ~SaveStateLANSync();

  bool Initialize();
  void Shutdown();
  void Pause();
  void Resume();

  bool StartServer();
  void StopServer();
  bool StartDiscovery();
  void StopDiscovery();

  void SyncWithPeer(const DiscoveredPeer &peer);
  void SyncWithAllPeers();
  void CancelSync();

  using ProgressCallback = std::function<void(const SyncProgress &progress)>;
  void SetProgressCallback(ProgressCallback cb);

  using DiscoveryCallback = std::function<void(const LANSync::DiscoveryEvent &event)>;
  void SetDiscoveryCallback(DiscoveryCallback cb);

  void SetScreenManager(ScreenManager *sm);

  bool IsInitialized() const { return initialized_; }
  bool IsSyncing() const;

  LANSyncDiscovery *Discovery() const { return discovery_.get(); }
  PairingManager *Pairing() const { return pairing_.get(); }
  TLSContext *GetTLSContext() const { return tlsCtx_.get(); }

private:
  std::string HandleListSaveStates(const std::string &method, const std::string &path, const std::string &body);
  std::string HandleGetSaveState(const std::string &method, const std::string &path, const std::string &body);
  std::string HandlePutSaveState(const std::string &method, const std::string &path, const std::string &body);

  void DoSyncWithPeer(const DiscoveredPeer &peer);

  void ResolveConflict(LANSyncClient &client, const std::string &key,
                       const SaveFileEntry &remoteEntry, const SaveFileEntry &localEntry,
                       const std::string &peerId);

  void UpdateProgress(SyncProgress::Status status, const std::string &currentFile = "",
                      int total = 0, int completed = 0, const std::string &error = "");

  std::vector<SaveFileEntry> GetLocalSaveFiles() const;
  std::vector<SaveFileEntry> ParseSaveFileList(const std::string &json) const;
  std::string ExtractJsonField(const std::string &json, const std::string &key) const;

  std::string GetDeviceId() const;

  std::unique_ptr<TLSContext> tlsCtx_;
  std::unique_ptr<LANSyncServer> server_;
  std::shared_ptr<LANSyncDiscovery> discovery_;
  std::unique_ptr<PairingManager> pairing_;

  std::atomic<bool> initialized_{false};
  std::atomic<bool> syncing_{false};
  std::mutex syncMutex_;

  ProgressCallback progressCb_;
  DiscoveryCallback discoveryCb_;
  mutable std::mutex cbMutex_;

  Path stateDir_;

  std::thread autoSyncThread_;
  std::atomic<bool> autoSyncRunning_{false};
  void AutoSyncLoop();
};

}  // namespace LANSync
