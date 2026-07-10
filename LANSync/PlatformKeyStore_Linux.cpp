#include "LANSync/PlatformKeyStore.h"
#include "LANSync/TLSTransport.h"
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
    std::string json = "{\"peerId\":\"" + JsonEscape(peer.peerId)
        + "\",\"deviceName\":\"" + JsonEscape(peer.deviceName)
        + "\",\"certPEM\":\"" + JsonEscape(peer.certPEM)
        + "\",\"lastIP\":\"" + JsonEscape(peer.lastIP)
        + "\",\"pairedAt\":" + std::to_string(peer.pairedAt) + "}\n";
    return File::WriteStringToFile(false, json, filePath);
}

std::vector<TrustedPeer> PlatformKeyStore::LoadPeers() {
    std::vector<TrustedPeer> peers;
    Path dir = StorageDir();

    std::vector<File::FileInfo> files;
    File::GetFilesInDir(dir, &files);

    for (const auto &file : files) {
        if (file.isDirectory)
            continue;
        // Check for .json extension
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
    auto peers = LoadPeers();
    for (const auto &p : peers) {
        if (p.certPEM.empty()) continue;
        if (TLSContext::GetFingerprintFromPEM(p.certPEM) == fingerprint)
            return true;
    }
    return false;
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
