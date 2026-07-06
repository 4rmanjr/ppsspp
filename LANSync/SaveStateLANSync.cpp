#include "LANSync/SaveStateLANSync.h"
#include "LANSync/LANSyncServer.h"
#include "LANSync/LANSyncClient.h"
#include "LANSync/LANSyncDiscovery.h"
#include "LANSync/LANSyncMetadata.h"
#include "LANSync/LANSyncConfig.h"
#include "LANSync/LANSyncPairing.h"
#include "LANSync/TLSTransport.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <thread>
#include <algorithm>
#include <cstdlib>

namespace LANSync {

SaveStateLANSync::SaveStateLANSync() {
  stateDir_ = GetSysDirectory(DIRECTORY_SAVESTATE);
}

SaveStateLANSync::~SaveStateLANSync() {
  Shutdown();
}

bool SaveStateLANSync::Initialize() {
  if (initialized_.exchange(true)) return true;

  tlsCtx_ = std::make_unique<TLSContext>();
  tlsCtx_->InitClient();
  tlsCtx_->InitServer();

  server_ = std::make_unique<LANSyncServer>();
  discovery_ = std::make_unique<LANSyncDiscovery>();
  pairing_ = std::make_unique<PairingManager>(tlsCtx_.get());

  return true;
}

void SaveStateLANSync::Shutdown() {
  StopServer();
  StopDiscovery();
  CancelSync();

  pairing_.reset();
  discovery_.reset();
  server_.reset();
  tlsCtx_.reset();

  initialized_ = false;
}

bool SaveStateLANSync::StartServer() {
  if (!initialized_) return false;
  if (server_->IsRunning()) return true;

  LANSyncConfigInfo config;
  config.Load();
  int port = config.iPort;

  server_->RegisterHandler("/states",
      [this](const std::string &method, const std::string &path, const std::string &body) -> std::string {
        if (path == "/states" || path == "/states/") {
          return HandleListSaveStates(method, path, body);
        }
        if (method == "GET") {
          return HandleGetSaveState(method, path, body);
        } else if (method == "PUT") {
          return HandlePutSaveState(method, path, body);
        }
        return "{\"error\":\"method_not_allowed\"}";
      });

  pairing_->RegisterHandlers(server_.get());

  return server_->Start(port, tlsCtx_.get());
}

void SaveStateLANSync::StopServer() {
  if (server_) {
    server_->Stop();
    server_->ClearHandlers();
  }
}

bool SaveStateLANSync::StartDiscovery() {
  if (!initialized_) return false;
  if (discovery_->IsRunning()) return true;

  LANSyncConfigInfo config;
  config.Load();

  discovery_->SetDeviceName(config.GetDeviceName());

  return discovery_->Start(nullptr);
}

void SaveStateLANSync::StopDiscovery() {
  if (discovery_) {
    discovery_->Stop();
  }
}

bool SaveStateLANSync::IsSyncing() const {
  return syncing_.load();
}

void SaveStateLANSync::SetProgressCallback(ProgressCallback cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  progressCb_ = std::move(cb);
}

void SaveStateLANSync::UpdateProgress(SyncProgress::Status status, const std::string &currentFile,
                                       int total, int completed, const std::string &error) {
  SyncProgress progress;
  progress.status = status;
  progress.currentFile = currentFile;
  progress.totalFiles = total;
  progress.completedFiles = completed;
  progress.errorMessage = error;

  std::lock_guard<std::mutex> lock(cbMutex_);
  if (progressCb_) {
    progressCb_(progress);
  }
}

// --- HTTP Handlers ---

std::string SaveStateLANSync::HandleListSaveStates(const std::string &method, const std::string &path, const std::string &body) {
  (void)path;
  (void)body;
  if (method != "GET") {
    return "{\"error\":\"method_not_allowed\"}";
  }

  std::vector<File::FileInfo> files;
  if (!File::GetFilesInDir(stateDir_, &files, ".ppst")) {
    return "[]";
  }

  std::string json = "[";
  bool first = true;

  for (const auto &file : files) {
    std::string name = file.name;
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) continue;
    std::string base = name.substr(0, dot);

    size_t underscore = base.rfind('_');
    if (underscore == std::string::npos) continue;

    std::string gameId = base.substr(0, underscore);
    int slot = std::atoi(base.substr(underscore + 1).c_str());

    Path fullPath = stateDir_ / name;

    HLC hlc;
    uint64_t mtime = 0;
    std::string peerId;
    if (!LANSyncMetadata::Load(fullPath, hlc, mtime, peerId)) {
      File::GetModifTimeT(fullPath, reinterpret_cast<time_t *>(&mtime));
    }

    std::string checksum = LANSyncMetadata::ComputeChecksum(fullPath);

    if (!first) json += ",";
    first = false;

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"gameId\":\"%s\",\"slot\":%d,\"checksum\":\"%s\",\"mtime\":%llu,\"size\":%lld}",
        gameId.c_str(), slot, checksum.c_str(),
        (unsigned long long)mtime, (long long)file.size);
    json += buf;
  }

  json += "]";
  return json;
}

std::string SaveStateLANSync::HandleGetSaveState(const std::string &method, const std::string &path, const std::string &body) {
  (void)body;
  if (method != "GET") {
    return "{\"error\":\"method_not_allowed\"}";
  }

  std::string subpath = path.substr(8);
  size_t slash = subpath.find('/');
  if (slash == std::string::npos) {
    return "{\"error\":\"invalid_path\"}";
  }

  std::string gameId = subpath.substr(0, slash);
  std::string slotStr = subpath.substr(slash + 1);
  int slot = std::atoi(slotStr.c_str());

  Path savePath = stateDir_ / (gameId + "_" + std::to_string(slot) + ".ppst");
  if (!File::Exists(savePath)) {
    return "{\"error\":\"not_found\"}";
  }

  std::string data;
  if (!File::ReadBinaryFileToString(savePath, &data)) {
    return "{\"error\":\"read_failed\"}";
  }

  return data;
}

std::string SaveStateLANSync::HandlePutSaveState(const std::string &method, const std::string &path, const std::string &body) {
  if (method != "PUT") {
    return "{\"error\":\"method_not_allowed\"}";
  }

  size_t queryStart = path.find('?');
  std::string pathOnly = (queryStart != std::string::npos) ? path.substr(0, queryStart) : path;
  std::string query = (queryStart != std::string::npos) ? path.substr(queryStart + 1) : "";

  std::string subpath = pathOnly.substr(8);
  size_t slash = subpath.find('/');
  if (slash == std::string::npos) {
    return "{\"error\":\"invalid_path\"}";
  }

  std::string gameId = subpath.substr(0, slash);
  std::string slotStr = subpath.substr(slash + 1);
  int slot = std::atoi(slotStr.c_str());

  std::string hlcStr;
  std::string peerId;
  size_t qpos = 0;
  while (qpos < query.size()) {
    size_t amp = query.find('&', qpos);
    std::string param = query.substr(qpos, amp - qpos);
    size_t eq = param.find('=');
    if (eq != std::string::npos) {
      std::string key = param.substr(0, eq);
      std::string val = param.substr(eq + 1);
      if (key == "hlc") hlcStr = val;
      else if (key == "peerId") peerId = val;
    }
    if (amp == std::string::npos) break;
    qpos = amp + 1;
  }

  if (hlcStr.empty()) {
    return "{\"error\":\"missing_hlc\"}";
  }

  HLC hlc = HLC::FromString(hlcStr);

  Path savePath = stateDir_ / (gameId + "_" + std::to_string(slot) + ".ppst");
  Path tmpPath(savePath.ToString() + ".tmp");

  FILE *f = File::OpenCFile(tmpPath, "wb");
  if (!f) {
    return "{\"error\":\"write_failed\"}";
  }
  size_t written = fwrite(body.data(), 1, body.size(), f);
  fclose(f);

  if (written != body.size()) {
    File::Delete(tmpPath);
    return "{\"error\":\"write_incomplete\"}";
  }

  if (!File::Exists(stateDir_)) {
    File::CreateDir(stateDir_);
  }

  if (!File::Rename(tmpPath, savePath)) {
    File::Delete(tmpPath);
    return "{\"error\":\"rename_failed\"}";
  }

  time_t now;
  time(&now);

  LANSyncMetadata::Save(savePath, hlc, (uint64_t)now, peerId);

  std::string checksum = LANSyncMetadata::ComputeChecksum(savePath);
  return "{\"success\":true,\"checksum\":\"" + checksum + "\"}";
}

// --- Sync Operations ---

void SaveStateLANSync::SyncWithPeer(const DiscoveredPeer &peer) {
  if (!initialized_ || syncing_.exchange(true)) return;

  UpdateProgress(SyncProgress::SYNCING, peer.deviceName, 0, 0);

  std::thread([this, peer]() {
    DoSyncWithPeer(peer);
    syncing_ = false;
  }).detach();
}

void SaveStateLANSync::SyncWithAllPeers() {
  if (!initialized_ || !discovery_) return;

  std::vector<DiscoveredPeer> peers = discovery_->GetPeers();
  for (const auto &peer : peers) {
    SyncWithPeer(peer);
  }
}

void SaveStateLANSync::CancelSync() {
  syncing_ = false;
  {
    std::lock_guard<std::mutex> lock(syncMutex_);
  }
  UpdateProgress(SyncProgress::IDLE, "", 0, 0);
}

void SaveStateLANSync::DoSyncWithPeer(const DiscoveredPeer &peer) {
  LANSyncClient client(tlsCtx_.get());
  if (!client.Connect(peer.host, peer.port)) {
    UpdateProgress(SyncProgress::ERROR, peer.deviceName, 0, 0, "Failed to connect");
    return;
  }

  HTTPResponse resp = client.Get("/states");
  if (resp.statusCode != 200) {
    UpdateProgress(SyncProgress::ERROR, peer.deviceName, 0, 0,
        "Failed to list remote states (HTTP " + std::to_string(resp.statusCode) + ")");
    return;
  }

  std::vector<SaveFileEntry> remoteFiles = ParseSaveFileList(resp.body);
  std::vector<SaveFileEntry> localFiles = GetLocalSaveFiles();

  std::map<std::string, SaveFileEntry> remoteMap;
  for (auto &f : remoteFiles) {
    remoteMap[f.gameId + "_" + std::to_string(f.slot)] = f;
  }

  std::map<std::string, SaveFileEntry> localMap;
  for (auto &f : localFiles) {
    localMap[f.gameId + "_" + std::to_string(f.slot)] = f;
  }

  std::set<std::string> allKeys;
  for (auto &[k, v] : remoteMap) allKeys.insert(k);
  for (auto &[k, v] : localMap) allKeys.insert(k);

  int total = (int)allKeys.size();
  int completed = 0;
  UpdateProgress(SyncProgress::SYNCING, peer.deviceName, total, completed);

  for (const auto &key : allKeys) {
    if (!syncing_) break;

    bool hasRemote = remoteMap.find(key) != remoteMap.end();
    bool hasLocal = localMap.find(key) != localMap.end();

    UpdateProgress(SyncProgress::SYNCING, key, total, completed);

    if (hasRemote && !hasLocal) {
      Path localPath = stateDir_ / (key + ".ppst");
      if (client.DownloadFile("/states/" + remoteMap[key].gameId + "/" + std::to_string(remoteMap[key].slot),
                              localPath)) {
        time_t now;
        time(&now);
        HLC hlc;
        hlc.Tick((uint64_t)now);
        LANSyncMetadata::Save(localPath, hlc, remoteMap[key].mtime, peer.peerId);
      }
    } else if (!hasRemote && hasLocal) {
      Path localPath = stateDir_ / (key + ".ppst");
      HLC hlc;
      uint64_t mtime = 0;
      std::string localPeerId;
      if (LANSyncMetadata::Load(localPath, hlc, mtime, localPeerId)) {
        time_t now;
        time(&now);
        hlc.Tick((uint64_t)now);
      } else {
        time_t now;
        time(&now);
        hlc.Tick((uint64_t)now);
      }

      std::string url = "/states/" + localMap[key].gameId + "/" + std::to_string(localMap[key].slot)
          + "?hlc=" + hlc.ToString() + "&peerId=" + GetDeviceId();
      client.UploadFile(url, localPath);
    } else if (hasRemote && hasLocal) {
      ResolveConflict(client, key, remoteMap[key], localMap[key], peer.peerId);
    }

    completed++;
    UpdateProgress(SyncProgress::SYNCING, key, total, completed);
  }

  if (!syncing_) {
    UpdateProgress(SyncProgress::IDLE, "", 0, 0);
  } else {
    UpdateProgress(SyncProgress::COMPLETED, peer.deviceName, total, completed);
  }
}

void SaveStateLANSync::ResolveConflict(LANSyncClient &client, const std::string &key,
                                        const SaveFileEntry &remoteEntry, const SaveFileEntry &localEntry,
                                        const std::string &peerId) {
  Path localPath = stateDir_ / (key + ".ppst");

  if (remoteEntry.mtime > localEntry.mtime) {
    Path conflictPath(localPath.ToString() + ".conflict");
    if (!File::Rename(localPath, conflictPath)) return;

    if (!client.DownloadFile("/states/" + remoteEntry.gameId + "/" + std::to_string(remoteEntry.slot),
                             localPath)) {
      File::Rename(conflictPath, localPath);
      return;
    }

    time_t now;
    time(&now);
    HLC hlc;
    hlc.Tick((uint64_t)now);
    LANSyncMetadata::Save(localPath, hlc, remoteEntry.mtime, peerId);
  } else if (localEntry.mtime > remoteEntry.mtime) {
    HLC hlc;
    uint64_t mtime = 0;
    std::string localPeerId;
    if (LANSyncMetadata::Load(localPath, hlc, mtime, localPeerId)) {
      time_t now;
      time(&now);
      hlc.Tick((uint64_t)now);
    } else {
      time_t now;
      time(&now);
      hlc.Tick((uint64_t)now);
    }
    std::string url = "/states/" + localEntry.gameId + "/" + std::to_string(localEntry.slot)
        + "?hlc=" + hlc.ToString() + "&peerId=" + GetDeviceId();
    client.UploadFile(url, localPath);
  }
}

// --- Helpers ---

std::vector<SaveFileEntry> SaveStateLANSync::GetLocalSaveFiles() const {
  std::vector<SaveFileEntry> result;

  std::vector<File::FileInfo> files;
  if (!File::GetFilesInDir(stateDir_, &files, ".ppst")) {
    return result;
  }

  for (const auto &file : files) {
    std::string name = file.name;
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) continue;
    std::string base = name.substr(0, dot);

    size_t underscore = base.rfind('_');
    if (underscore == std::string::npos) continue;

    std::string gameId = base.substr(0, underscore);
    int slot = std::atoi(base.substr(underscore + 1).c_str());

    Path fullPath = stateDir_ / name;

    HLC hlc;
    uint64_t mtime = 0;
    std::string peerId;
    if (!LANSyncMetadata::Load(fullPath, hlc, mtime, peerId)) {
      File::GetModifTimeT(fullPath, reinterpret_cast<time_t *>(&mtime));
    }

    SaveFileEntry entry;
    entry.gameId = gameId;
    entry.slot = slot;
    entry.checksum = LANSyncMetadata::ComputeChecksum(fullPath);
    entry.mtime = mtime;
    entry.size = file.size;
    result.push_back(entry);
  }

  return result;
}

std::vector<SaveFileEntry> SaveStateLANSync::ParseSaveFileList(const std::string &json) const {
  std::vector<SaveFileEntry> result;

  size_t pos = 0;
  while (true) {
    size_t objStart = json.find('{', pos);
    if (objStart == std::string::npos) break;
    size_t objEnd = json.find('}', objStart);
    if (objEnd == std::string::npos) break;

    std::string obj = json.substr(objStart, objEnd - objStart + 1);

    SaveFileEntry entry;
    entry.gameId = ExtractJsonField(obj, "gameId");
    entry.slot = std::atoi(ExtractJsonField(obj, "slot").c_str());
    entry.checksum = ExtractJsonField(obj, "checksum");
    std::string mtimeStr = ExtractJsonField(obj, "mtime");
    if (!mtimeStr.empty()) entry.mtime = (uint64_t)std::atoll(mtimeStr.c_str());
    std::string sizeStr = ExtractJsonField(obj, "size");
    if (!sizeStr.empty()) entry.size = std::atoll(sizeStr.c_str());

    result.push_back(entry);
    pos = objEnd + 1;
  }

  return result;
}

std::string SaveStateLANSync::ExtractJsonField(const std::string &json, const std::string &key) const {
  auto keyStr = "\"" + key + "\"";
  auto pos = json.find(keyStr);
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size()) return "";
  if (json[pos] == '"') {
    pos++;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
      val += json[pos++];
    }
    return val;
  }
  std::string val;
  while (pos < json.size() && (isdigit((unsigned char)json[pos]) || json[pos] == '-' || json[pos] == '.')) {
    val += json[pos++];
  }
  return val;
}

std::string SaveStateLANSync::GetDeviceId() const {
  std::string mac = g_Config.sMACAddress;
  if (mac.size() >= 4) {
    return "PPSSPP-" + mac.substr(mac.size() - 4);
  }
  return "PPSSPP-Unknown";
}

}  // namespace LANSync
