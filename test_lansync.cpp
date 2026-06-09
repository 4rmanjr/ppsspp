// Standalone test for LAN Sync core components
// Compile: g++ -std=c++17 -I. -o test_lansync test_lansync.cpp Common/Data/HLC.cpp
// Run: ./test_lansync

#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
#include <cstdarg>
#include <inttypes.h>

// Minimal stubs (avoid linking entire PPSSPP build system)
enum class Log { System = 0, NUMBER_OF_LOGS = 42 };
enum class LogLevel : int { LERROR = 2, LWARNING = 3, LINFO = 4, LDEBUG = 5 };
struct LogChannel { LogLevel level = LogLevel::LINFO; bool enabled = true; };
LogChannel g_log[42];
bool *g_bLogEnabledSetting = nullptr;
bool GenericLogEnabled(Log, LogLevel) { return true; }
void GenericLog(Log, LogLevel, const char*, int, const char*, ...) {}

// StringFromFormat (needed by HLC.cpp)
std::string StringFromFormat(const char *fmt, ...) {
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	return std::string(buf);
}

#include "Common/Data/HLC.h"

int main() {
	printf("=== LAN Sync Core Test ===\n\n");

	// [1] HLC increment
	printf("[1] HLC Increment: ");
	HLC h1(1000000, 0, "device-a");
	HLC h2 = h1.Increment("device-a");
	assert(h2 > h1);
	printf("OK\n");

	// [2] Conflict detection — both modified independently
	printf("[2] Conflict: ");
	HLC shared(1000, 0, "shared");
	// Simulate two independent saves from same parent
	// Different parents = both modified independently since last sync
	HLC local_parent(1000, 0, "device-a");
	HLC remote_parent(1000, 1, "device-b");
	HLC local_save(2000, 0, "device-a");
	HLC remote_save(2000, 0, "device-b");
	ConflictResult r = DetectConflict(local_save, local_parent,
	                                  remote_save, remote_parent);
	assert(r.conflict == true);
	printf("OK (reason: %s)\n", r.reason.c_str());

	// [3] No conflict
	printf("[3] Linear: ");
	HLC origin(1000, 0, "a");
	HLC newer = origin.Increment("a");
	r = DetectConflict(newer, origin, origin, HLC());
	assert(!r.conflict);
	printf("OK\n");

	// [4] HLC merge
	printf("[4] Merge: ");
	HLC m1(1000, 0, "a"), m2(2000, 1, "b");
	HLC merged = m1.Merge(m2, "c");
	assert(merged.wallTime == 2000);
	printf("OK\n");

	// [5] Serialization
	printf("[5] Serialize: ");
	HLC original(1234567890123456LL, 5, "my-device");
	HLC parsed = HLC::FromString(original.ToString());
	assert(parsed.wallTime == original.wallTime);
	assert(parsed.logical == original.logical);
	HLC fromJson = HLC::FromJSON(original.ToJSON());
	assert(fromJson.wallTime == original.wallTime);
	printf("OK\n");

	printf("\n=== ALL 5 TESTS PASSED ===\n");
	return 0;
}
