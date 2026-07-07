#include "ppsspp_config.h"

#include <errno.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileDescriptor.h"
#include "Common/Log.h"

namespace fd_util {

bool WaitUntilReady(int fd, double timeout, bool for_write) {
	struct timeval tv;
	tv.tv_sec = (long)floor(timeout);
	tv.tv_usec = (long)((timeout - floor(timeout)) * 1000000.0);

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	// First argument to select is the highest socket in the set + 1.
	int rval;
	if (for_write) {
		rval = select(fd + 1, nullptr, &fds, nullptr, &tv);
	} else {
		rval = select(fd + 1, &fds, nullptr, nullptr, &tv);
	}

	if (rval < 0) {
		// Error calling select.
		return false;
	} else if (rval == 0) {
		// Timeout.
		return false;
	} else {
		// Socket is ready.
		return true;
	}
}

void SetNonBlocking(int sock, bool non_blocking) {
#ifndef _WIN32
	int opts = fcntl(sock, F_GETFL);
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		ERROR_LOG(Log::IO, "Error getting socket status while changing nonblocking status");
	}
	if (non_blocking) {
		opts = (opts | O_NONBLOCK);
	} else {
		opts = (opts & ~O_NONBLOCK);
	}

	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(F_SETFL)");
		ERROR_LOG(Log::IO, "Error setting socket nonblocking status");
	}
#else
	u_long val = non_blocking ? 1 : 0;
	if (ioctlsocket(sock, FIONBIO, &val) != 0) {
		ERROR_LOG(Log::IO, "Error setting socket nonblocking status");
	}
#endif
}

int ConnectWithTimeout(const char *host, int port, int timeoutSec) {
	if (!host || port <= 0 || timeoutSec <= 0)
		return -1;

	std::string portStr = std::to_string(port);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *res = nullptr;
	int err = getaddrinfo(host, portStr.c_str(), &hints, &res);
	if (err != 0 || !res) {
		return -1;
	}

	int fd = -1;
	for (struct addrinfo *rp = res; rp != nullptr; rp = rp->ai_next) {
		fd = (int)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;

		SetNonBlocking(fd, true);

		int connResult = connect(fd, rp->ai_addr, (int)rp->ai_addrlen);
		if (connResult == 0) {
			break;
		}

		if (!connectInProgress(socket_errno)) {
			closesocket(fd);
			fd = -1;
			continue;
		}

		// Wait for connection with select()
		fd_set writefds;
		FD_ZERO(&writefds);
		FD_SET(fd, &writefds);

		struct timeval tv;
		tv.tv_sec = timeoutSec;
		tv.tv_usec = 0;

		int selResult;
		do {
			FD_ZERO(&writefds);
			FD_SET(fd, &writefds);
			tv.tv_sec = timeoutSec;
			tv.tv_usec = 0;
			selResult = select(fd + 1, nullptr, &writefds, nullptr, &tv);
		} while (selResult < 0 && socket_errno == EINTR);

		if (selResult <= 0) {
			closesocket(fd);
			fd = -1;
			continue;
		}

		// Verify connection succeeded
		int soError = 0;
		socklen_t soLen = sizeof(soError);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) < 0 || soError != 0) {
			closesocket(fd);
			fd = -1;
			continue;
		}

		break;
	}

	freeaddrinfo(res);

	if (fd >= 0) {
		SetNonBlocking(fd, false);
	}

	return fd;
}

std::string GetLocalIP(int sock) {
	union {
		struct sockaddr sa;
		struct sockaddr_in ipv4;
#if !PPSSPP_PLATFORM(SWITCH)
		struct sockaddr_in6 ipv6;
#endif
	} server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	socklen_t len = sizeof(server_addr);
	int retval = getsockname(sock, (struct sockaddr *)&server_addr, &len);
	if (retval == 0) {
		char temp[64]{};

		// We clear the port below for WSAAddressToStringA.
		void *addr = nullptr;
#if !PPSSPP_PLATFORM(SWITCH)
		if (server_addr.sa.sa_family == AF_INET6) {
			server_addr.ipv6.sin6_port = 0;
			addr = &server_addr.ipv6.sin6_addr;
		}
#endif
		if (addr == nullptr) {
			server_addr.ipv4.sin_port = 0;
			addr = &server_addr.ipv4.sin_addr;
		}
#ifdef _WIN32
		wchar_t wtemp[sizeof(temp)];
		DWORD len = (DWORD)sizeof(temp);
		// Windows XP doesn't support inet_ntop.
		HRESULT result = WSAAddressToStringW((struct sockaddr *)&server_addr, sizeof(server_addr), nullptr, wtemp, &len);
		if (result == 0) {
			return ConvertWStringToUTF8(wtemp);
		} else {
			return "";
		}
#else
		const char *result = inet_ntop(server_addr.sa.sa_family, addr, temp, sizeof(temp));
		if (result) {
			return result;
		} else {
			return "";
		}
#endif
	} else {
		WARN_LOG(Log::IO, "GetLocalIP: getsockname failed with error %d (%s)", retval, strerror(retval));
		return "";
	}
}

}  // fd_util
