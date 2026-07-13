#pragma once

#include <string>
#include <cstdint>
#include "Common/Data/Format/JSONReader.h"

namespace LANSync {

inline std::string JsonGetString(const std::string &json, const std::string &key) {
    json::JsonReader reader(json.data(), json.size());
    if (!reader.ok()) return {};
    json::JsonGet obj = reader.root();
    if (!obj) return {};
    std::string val;
    obj.getString(key.c_str(), &val);
    return val;
}

inline int64_t JsonGetInt64(const std::string &json, const std::string &key, int64_t defaultVal = 0) {
    json::JsonReader reader(json.data(), json.size());
    if (!reader.ok()) return defaultVal;
    json::JsonGet obj = reader.root();
    if (!obj) return defaultVal;
    const JsonNode *node = obj.get(key.c_str(), JSON_NUMBER);
    if (!node) return defaultVal;
    return (int64_t)node->value.toNumber();
}

} // namespace LANSync
