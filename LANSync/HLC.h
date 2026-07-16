#pragma once
#include <cstdint>
#include <string>
#include <tuple>
#include <charconv>

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

    // [PPSSPP-FORK] SR7: parse with std::from_chars to avoid the
    // non-portable uint64_t* -> unsigned long long* type-punning UB that
    // the old sscanf cast exhibited on ILP64 platforms, and to actually
    // check for parse errors (return a zeroed HLC on malformed input).
    static HLC FromString(const std::string &s) {
        HLC h;
        auto colon = s.find(':');
        if (colon == std::string::npos) return h;
        uint64_t phys = 0;
        uint32_t logical = 0;
        auto r1 = std::from_chars(s.data(), s.data() + colon, phys);
        auto r2 = std::from_chars(s.data() + colon + 1, s.data() + s.size(), logical);
        if (r1.ec != std::errc() || r2.ec != std::errc()) return HLC{};
        h.physical = phys;
        h.logical = logical;
        return h;
    }
};
