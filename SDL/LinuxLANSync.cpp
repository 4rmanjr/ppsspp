// PPSSPP Project - LAN Save State Sync
// Linux platform backend implementation

#include "ppsspp_config.h"

#if (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC)) && !PPSSPP_PLATFORM(ANDROID)

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <thread>

#include <qrencode.h>

#include "SDL/LinuxLANSync.h"

#include "Common/Net/PlatformKeyStore.h"
#include "Common/Net/MDNS.h"
#include "Common/Net/UDPDiscovery.h"
#include "Common/Net/TLSServer.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Core/SaveStateLANSync.h"

LinuxLANSync &GetLinuxLANSync() {
	static LinuxLANSync instance;
	return instance;
}

bool LinuxLANSync::Init() {
	INFO_LOG(Log::System, "LinuxLANSync: initializing platform subsystems");

	// Init secure key storage
	PlatformKeyStore::Init();

	// Init core sync manager
	SaveStateLANSync::Instance().Init();

	return true;
}

void LinuxLANSync::Shutdown() {
	Disable();
	SaveStateLANSync::Instance().Shutdown();
	PlatformKeyStore::Shutdown();
}

bool LinuxLANSync::Enable(const std::string &deviceName) {
	if (enabled_) return true;

	auto &core = SaveStateLANSync::Instance();
	core.SetDeviceInfo(deviceName, "Linux");

	// Start discovery (mDNS browser + UDP listener)
	core.StartDiscovery();

	// Start server (mDNS announcer + UDP broadcaster)
	if (!core.StartServer()) {
		WARN_LOG(Log::System, "LinuxLANSync: failed to start server");
		core.StopDiscovery();
		return false;
	}

	enabled_ = true;
	INFO_LOG(Log::System, "LinuxLANSync: enabled, listening on port %d", core.GetServerPort());
	return true;
}

void LinuxLANSync::Disable() {
	if (!enabled_) return;

	auto &core = SaveStateLANSync::Instance();
	core.StopServer();
	core.StopDiscovery();

	enabled_ = false;
	INFO_LOG(Log::System, "LinuxLANSync: disabled");
}

std::vector<uint8_t> LinuxLANSync::GenerateQRCode(const std::string &payload) {
	// Generate QR code using libqrencode
	QRcode *qr = QRcode_encodeString(payload.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
	if (!qr) {
		WARN_LOG(Log::System, "QR: failed to encode payload");
		return {};
	}

	int size = qr->width;
	int scale = 8;  // 8x scale for readability
	int imgSize = size * scale;
	
	// Create BMP image (black & white)
	int rowSize = ((imgSize + 31) / 32) * 4;  // BMP rows are 4-byte aligned
	int dataSize = rowSize * imgSize;
	int fileSize = 62 + dataSize;  // BMP header (14) + DIB header (40) + data
	
	std::vector<uint8_t> bmp(fileSize, 0xFF);  // White background
	
	// BMP Header
	bmp[0] = 'B'; bmp[1] = 'M';
	*(uint32_t*)&bmp[2] = fileSize;
	*(uint32_t*)&bmp[10] = 62;  // Offset to pixel data
	
	// DIB Header (BITMAPINFOHEADER)
	*(uint32_t*)&bmp[14] = 40;  // Header size
	*(int32_t*)&bmp[18] = imgSize;  // Width
	*(int32_t*)&bmp[22] = imgSize;  // Height
	*(uint16_t*)&bmp[26] = 1;   // Planes
	*(uint16_t*)&bmp[28] = 1;   // Bits per pixel
	*(uint32_t*)&bmp[34] = dataSize;  // Image size
	
	// Pixel data
	uint8_t *pixels = bmp.data() + 62;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			bool black = (qr->data[y * size + x] & 0x01) != 0;
			if (black) {
				for (int sy = 0; sy < scale; sy++) {
					for (int sx = 0; sx < scale; sx++) {
						int px = x * scale + sx;
						int py = y * scale + sy;
						int byteIdx = py * rowSize + (px / 8);
						int bitIdx = 7 - (px % 8);
						pixels[byteIdx] &= ~(1 << bitIdx);  // Set bit to 0 = black
					}
				}
			}
		}
	}
	
	QRcode_free(qr);
	return bmp;
}

std::vector<std::string> LinuxLANSync::GetLocalIPs() const {
	std::vector<std::string> ips;

#if !PPSSPP_PLATFORM(SWITCH)
	struct ifaddrs *ifaddr, *ifa;
	if (getifaddrs(&ifaddr) == 0) {
		for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr) continue;
			if (ifa->ifa_addr->sa_family != AF_INET) continue;

			// Skip loopback
			if (strcmp(ifa->ifa_name, "lo") == 0) continue;

			char buf[INET_ADDRSTRLEN];
			struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
			inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));

			// Only add non-zero, non-127 IPs
			const char *c = buf;
			bool isLoopback = (strncmp(c, "127.", 4) == 0);
			bool isZero = (strcmp(c, "0.0.0.0") == 0);

			if (!isLoopback && !isZero) {
				ips.push_back(buf);
			}
		}
		freeifaddrs(ifaddr);
	}
#endif

	return ips;
}

#endif  // (LINUX || MAC) && !ANDROID
