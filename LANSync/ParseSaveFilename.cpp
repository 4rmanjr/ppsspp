#include "LANSync/ParseSaveFilename.h"

#include <cstdlib>

namespace LANSync {

bool ParseSaveFilename(const std::string &base, std::string &gameId, int &slot) {
	size_t underscore = base.rfind('_');
	if (underscore == std::string::npos) return false;

	std::string parsedGameId = base.substr(0, underscore);
	if (parsedGameId.empty()) return false;

	const char *slotStart = base.c_str() + underscore + 1;
	char *end = nullptr;
	long val = std::strtol(slotStart, &end, 10);
	if (end == slotStart || *end != '\0') return false;
	if (val < 0 || val > 999) return false;

	gameId = std::move(parsedGameId);
	slot = static_cast<int>(val);
	return true;
}

}  // namespace LANSync
