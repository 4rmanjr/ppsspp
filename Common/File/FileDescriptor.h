#pragma once

#include <cstring>
#include <string>
#include <string_view>

namespace fd_util {

// Returns true if the fd became ready, false if it didn't or
// if there was another error.
bool WaitUntilReady(int fd, double timeout, bool for_write = false);

void SetNonBlocking(int fd, bool non_blocking);

std::string GetLocalIP(int sock);

// Connect to host:port with a timeout. Uses getaddrinfo() for DNS resolution
// (supports IPv4/IPv6) and non-blocking connect + select() for timeout.
// Returns connected fd (in blocking mode) on success, -1 on failure.
int ConnectWithTimeout(const char *host, int port, int timeoutSec);

}  // fd_util
