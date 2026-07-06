// Android implementation uses file-based storage for now.
// Android Keystore via JNI can be added later.
#include "LANSync/PlatformKeyStore.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/Path.h"
#include <cstdio>
#include <cstdlib>

namespace LANSync {

Path PlatformKeyStore::StorageDir() {
    Path dir = GetSysDirectory(DIRECTORY_SAVESTATE) / "sync_peers";
    File::CreateDir(dir);
    return dir;
}

bool PlatformKeyStore::SavePeer(const TrustedPeer &peer) {
    Path filePath = StorageDir() / (peer.peerId + ".json");
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "{\"peerId\":\"%s\",\"deviceName\":\"%s\",\"certPEM\":\"%s\",\"lastIP\":\"%s\",\"pairedAt\":%llu}\n",
        peer.peerId.c_str(),
        peer.deviceName.c_str(),
        peer.certPEM.c_str(),
        peer.lastIP.c_str(),
        (unsigned long long)peer.pairedAt);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return false;
    return File::WriteStringToFile(false, std::string(buf, n), filePath);
}

std::vector<TrustedPeer> PlatformKeyStore::LoadPeers() {
    std::vector<TrustedPeer> peers;
    Path dir = StorageDir();

    std::vector<File::FileInfo> files;
    File::GetFilesInDir(dir, &files);

    for (const auto &file : files) {
        if (file.isDirectory)
            continue;
        std::string name = file.name;
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json")
            continue;

        Path filePath = dir / file.name;
        std::string data;
        if (!File::ReadBinaryFileToString(filePath, &data))
            continue;

        TrustedPeer peer;
        auto extractStr = [&](const std::string &key) -> std::string {
            auto pos = data.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = data.find(':', pos);
            if (pos == std::string::npos) return "";
            pos++;
            while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\t')) pos++;
            if (pos >= data.size() || data[pos] != '"') return "";
            pos++;
            std::string val;
            while (pos < data.size() && data[pos] != '"') {
                if (data[pos] == '\\' && pos + 1 < data.size()) {
                    pos++;
                    if (data[pos] == 'n') val += '\n';
                    else if (data[pos] == 't') val += '\t';
                    else if (data[pos] == '"') val += '"';
                    else if (data[pos] == '\\') val += '\\';
                    else { val += '\\'; val += data[pos]; }
                } else {
                    val += data[pos];
                }
                pos++;
            }
            return val;
        };
        auto extractInt = [&](const std::string &key) -> uint64_t {
            auto pos = data.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0;
            pos = data.find(':', pos);
            if (pos == std::string::npos) return 0;
            pos++;
            while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\t')) pos++;
            return (uint64_t)atoll(data.c_str() + pos);
        };

        peer.peerId = extractStr("peerId");
        if (peer.peerId.empty()) continue;
        peer.deviceName = extractStr("deviceName");
        peer.certPEM = extractStr("certPEM");
        peer.lastIP = extractStr("lastIP");
        peer.pairedAt = extractInt("pairedAt");

        peers.push_back(peer);
    }
    return peers;
}

bool PlatformKeyStore::IsTrusted(const std::string &fingerprint) {
    Path filePath = StorageDir() / (fingerprint + ".json");
    return File::Exists(filePath);
}

const TrustedPeer *PlatformKeyStore::FindPeer(const std::string &peerId) {
    static TrustedPeer s_cachedPeer;
    auto peers = LoadPeers();
    for (const auto &p : peers) {
        if (p.peerId == peerId) {
            s_cachedPeer = p;
            return &s_cachedPeer;
        }
    }
    return nullptr;
}

bool PlatformKeyStore::RemovePeer(const std::string &peerId) {
    Path filePath = StorageDir() / (peerId + ".json");
    return File::Delete(filePath);
}

}  // namespace LANSync
