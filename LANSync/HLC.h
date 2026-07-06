#pragma once
#include <cstdint>
#include <string>
#include <tuple>

struct HLC {
    uint64_t physical = 0;
    uint32_t logical = 0;

    void Tick(uint64_t now) {
        if (now > physical) {
            physical = now;
            logical = 0;
        } else {
            logical++;
        }
    }

    void Merge(const HLC &other) {
        if (other.physical > physical) {
            physical = other.physical;
            logical = other.logical + 1;
        } else if (other.physical == physical) {
            logical = std::max(logical, other.logical) + 1;
        }
    }

    bool operator<(const HLC &o) const {
        return std::tie(physical, logical) < std::tie(o.physical, o.logical);
    }
    bool operator==(const HLC &o) const {
        return physical == o.physical && logical == o.logical;
    }
    bool ConflictsWith(const HLC &other) const {
        return !(*this < other) && !(other < *this) && !(*this == other);
    }

    std::string ToString() const {
        char buf[64];
        snprintf(buf, sizeof(buf), "%llu:%u", (unsigned long long)physical, logical);
        return std::string(buf);
    }

    static HLC FromString(const std::string &s) {
        HLC h;
        sscanf(s.c_str(), "%llu:%u", (unsigned long long *)&h.physical, &h.logical);
        return h;
    }
};
