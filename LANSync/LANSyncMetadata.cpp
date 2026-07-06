#include "LANSync/LANSyncMetadata.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include <cstdio>
#include <cstring>
#include <openssl/evp.h>

Path LANSyncMetadata::SidecarPath(const Path &ppstPath) {
    return Path(ppstPath.ToString() + ".sync.json");
}

bool LANSyncMetadata::Load(const Path &ppstPath, HLC &hlc, uint64_t &originalMtime, std::string &peerId) {
    Path sidecar = SidecarPath(ppstPath);
    if (!File::Exists(sidecar))
        return false;

    std::string data;
    if (!File::ReadBinaryFileToString(sidecar, &data))
        return false;

    // Simple JSON parsing (avoid full dependency)
    // Format: {"hlc":"1234567890:42","originalMtime":1712345678,"peerId":"a1b2c3..."}
    auto findField = [&](const std::string &key) -> std::string {
        auto pos = data.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = data.find(':', pos);
        if (pos == std::string::npos) return "";
        pos++; // skip ':'
        while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\t')) pos++;
        if (pos >= data.size()) return "";
        if (data[pos] == '"') {
            // string value
            pos++;
            std::string result;
            while (pos < data.size() && data[pos] != '"') {
                result += data[pos++];
            }
            return result;
        } else {
            // numeric value
            std::string result;
            while (pos < data.size() && (isdigit(data[pos]) || data[pos] == '-')) {
                result += data[pos++];
            }
            return result;
        }
    };

    std::string hlcStr = findField("hlc");
    if (!hlcStr.empty())
        hlc = HLC::FromString(hlcStr);

    std::string mtimeStr = findField("originalMtime");
    if (!mtimeStr.empty())
        originalMtime = (uint64_t)atoll(mtimeStr.c_str());

    peerId = findField("peerId");

    return true;
}

bool LANSyncMetadata::Save(const Path &ppstPath, const HLC &hlc, uint64_t originalMtime, const std::string &peerId) {
    Path sidecar = SidecarPath(ppstPath);
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{\"hlc\":\"%s\",\"originalMtime\":%llu,\"peerId\":\"%s\"}\n",
        hlc.ToString().c_str(),
        (unsigned long long)originalMtime,
        peerId.c_str());
    if (n < 0 || (size_t)n >= sizeof(buf))
        return false;
    return File::WriteStringToFile(false, std::string(buf, n), sidecar);
}

void LANSyncMetadata::Delete(const Path &ppstPath) {
    File::Delete(SidecarPath(ppstPath));
}

std::string LANSyncMetadata::ComputeChecksum(const Path &path) {
    FILE *f = File::OpenCFile(path, "rb");
    if (!f)
        return "";

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fclose(f);
        return "";
    }

    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        EVP_DigestUpdate(mdctx, buf, n);
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;
    EVP_DigestFinal_ex(mdctx, hash, &hashLen);
    EVP_MD_CTX_free(mdctx);
    fclose(f);

    // Convert to hex
    char hex[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < hashLen; i++) {
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    return std::string(hex);
}
