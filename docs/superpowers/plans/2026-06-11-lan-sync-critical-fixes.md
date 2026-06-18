# LAN Save State Sync — Security Fix & Error Hardening

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the real security hole (missing Bearer token validation on save endpoints) and add missing socket timeouts + error logging to prevent hung connections.

**Architecture:** All changes in `Core/SaveStateLANSync.cpp` and `Core/SaveStateLANSync.h`. Token validation added as a pre-route check in the server connection handler. Socket timeouts added to 4 client connection sites that lack them today. Error logging added at each failure point.

**Tech Stack:** C++17, POSIX sockets, PPSSPP codebase

**Validation summary (2026-06-11):**

| Bug # | Claim | Actual | Action |
|-------|-------|--------|--------|
| #4 | "4KB upload limit" | **False** — POST handler has its own streaming while loop, handles up to MAX_UPLOAD_SIZE (100MB). General handler reads first 3.2MB into buffer, POST handler reads rest. | Skip — not a bug |
| #5 | "Conflict resolution stubbed" | **False** — `ResolveConflict()` downloads .ppst + thumbnail, `ResolveAllConflicts()` iterates + clears queue. Fully implemented. | Skip — update BUGS.md |
| #6 | "No token validation" | **True** — zero validation on `/api/v1/saves/*` endpoints | **Fix now** |

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `Core/SaveStateLANSync.cpp` | All server handlers, client connections | Modify (add auth + timeouts) |
| `Core/SaveStateLANSync.h` | Manager header | No change needed |
| `BUGS.md` | Bug tracking | Modify (correct #4, #5, #6 status) |

---

### Task 1: Add Bearer Token Validation on Protected Endpoints (Bug #6)

**Files:**
- Modify: `Core/SaveStateLANSync.cpp:450-500` (server route handler, after body extraction block)

- [ ] **Step 1: Add token extraction + validation helpers**

Insert these two static helpers before `StartServer()` (before line 336, after `WriteHTTPResponse`):

```cpp
// Extracts Bearer token from HTTP Authorization header.
// Returns empty string if header missing or malformed.
static std::string ExtractBearerToken(const std::string &request) {
	const char *prefix = "Authorization: Bearer ";
	size_t pos = request.find(prefix);
	if (pos == std::string::npos)
		return "";
	pos += strlen(prefix);
	size_t end = request.find("\r\n", pos);
	if (end == std::string::npos)
		end = request.size();
	return request.substr(pos, end - pos);
}
```

- [ ] **Step 2: Insert auth check in server route handler**

Find line ~490 (after `body = request.substr(headerEnd + 4);` and before `// Route requests`). Insert:

```cpp
				// Validate Bearer token on protected endpoints
				{
					bool isProtected = (path == "/api/v1/saves/list" ||
					                    path.find("/api/v1/saves/") == 0);
					if (isProtected) {
						std::string authToken = ExtractBearerToken(request);
						bool authorized = false;
						{
							std::lock_guard<std::mutex> lock(peerMutex_);
							for (const auto &p : pairedPeers_) {
								if (p.token == authToken && !authToken.empty()) {
									authorized = true;
									break;
								}
							}
						}
						if (!authorized) {
							WARN_LOG(Log::System, "LANSync: unauthorized %s %s (token=%s...)",
							         method.c_str(), path.c_str(),
							         authToken.empty() ? "none" : authToken.substr(0, 8).c_str());
							WriteHTTPResponse(clientFd, 401,
							                  "{\"error\":\"unauthorized\"}");
							closesocket(clientFd);
							return;
						}
					}
				}
```

- [ ] **Step 3: Verify pairing endpoints are still unprotected**

Review the condition logic:
- `path == "/api/v1/saves/list"` → protected
- `path.find("/api/v1/saves/") == 0` → protected (covers GET/POST file operations)
- `/api/v1/pair`, `/api/v1/pair-request`, `/api/v1/pair-respond`, `/api/v1/pair-status`, `/api/v1/pair-verify`, `/api/v1/status` → NOT protected (correct)

- [ ] **Step 4: Verify `path` is parsed before auth check**

Confirm `path` is set before the auth block. The code at lines ~482-488 parses `method` and `path` from `request`. Auth block is inserted AFTER this parsing. Order: parse method+path → extract body → validate token → route dispatch. ✅ Correct.

- [ ] **Step 5: Commit**

```bash
git add Core/SaveStateLANSync.cpp
git commit -m "fix(lansync): validate Bearer token on /api/v1/saves/* endpoints (Bug #6)

Add ExtractBearerToken() helper. Check Authorization header against
paired peer tokens on GET/POST /api/v1/saves/*. Returns 401 for
unauthenticated requests. Pairing endpoints remain PIN-based.
Fixes security hole where any LAN device could access saves."
```

---

### Task 2: Add Socket Timeouts to All Client Connections

**Files:**
- Modify: `Core/SaveStateLANSync.cpp` — 4 locations

**Current timeout coverage:**

| Location | SO_RCVTIMEO | SO_SNDTIMEO | Line |
|----------|:---:|:---:|------|
| Server listen socket | ✅ 10s | ❌ | 384 |
| Server client socket | ✅ 10s | ❌ | 415 |
| downloadSave lambda | ✅ 30s | ❌ | 1168 |
| ResolveConflict | ✅ 30s | ❌ | 1496 |
| **uploadSave lambda** | ❌ | ❌ | ~1205 |
| **PairWithPeer** | ❌ | ❌ | ~685 |
| **AutoPairWithPeer** | ❌ | ❌ | ~780 |
| **DoSync main connect** | ❌ | ❌ | ~1000 |

- [ ] **Step 1: Add timeout to uploadSave lambda**

In the `uploadSave` lambda (around line 1205), after `int sock = socket(...)` and error check, before `struct sockaddr_in addr`:

```cpp
		int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) return false;
		struct timeval tv;
		tv.tv_sec = 30; tv.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
		struct sockaddr_in addr;
```

- [ ] **Step 2: Add timeout to PairWithPeer**

In `PairWithPeer` (around line 690), after `int sock = socket(...)` and error check:

```cpp
		int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) {
			if (callback) callback(false, "Socket error");
			return;
		}
		struct timeval tv;
		tv.tv_sec = 30; tv.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
```

- [ ] **Step 3: Add timeout to AutoPairWithPeer**

In `AutoPairWithPeer` (around line 780), same pattern — after `int sock = socket(...)` error check:

```cpp
		int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) { if (callback) callback(false, "Socket error"); return; }
		struct timeval tv;
		tv.tv_sec = 15; tv.tv_usec = 0;  // Shorter for auto-pair (user waiting)
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
```

Also add timeout to the poll loop sockets inside `AutoPairWithPeer` (~line 850):

```cpp
			int pollSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (pollSock < 0) continue;
			struct timeval pollTv;
			pollTv.tv_sec = 5; pollTv.tv_usec = 0;
			setsockopt(pollSock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&pollTv, sizeof(pollTv));
```

- [ ] **Step 4: Add timeout to DoSync main connection**

In `DoSync` (around line 1000), after the `int sock = socket(...)`:

```cpp
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) { fprintf(stderr, "DoSync: socket() failed\n"); result.success = false; return result; }
	struct timeval tv;
	tv.tv_sec = 30; tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
```

- [ ] **Step 5: Commit**

```bash
git add Core/SaveStateLANSync.cpp
git commit -m "fix(lansync): add socket timeouts to all client connections

Add SO_RCVTIMEO + SO_SNDTIMEO (30s) to uploadSave, PairWithPeer,
DoSync main connect. 15s for AutoPairWithPeer initial connect,
5s for poll loop sockets. Previously only downloadSave and
ResolveConflict had timeouts — connections could hang forever."
```

---

### Task 3: Add Error Logging at Connection Failure Points

**Files:**
- Modify: `Core/SaveStateLANSync.cpp` — multiple locations

- [ ] **Step 1: Log in uploadSave**

After the `connect()` failure block in `uploadSave` lambda:

```cpp
	bool ok = false;
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
		// ... existing code ...
	} else {
		WARN_LOG(Log::System, "LANSync: uploadSave connect failed %s_%d.%s -> %s:%d: %s",
		         gid.c_str(), sl, ext, peer.host.c_str(), peer.port, strerror(errno));
	}
	closesocket(sock);
```

- [ ] **Step 2: Log in downloadSave**

After the `connect()` failure in `downloadSave` lambda:

```cpp
	bool ok = false;
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
		// ... existing code ...
	} else {
		WARN_LOG(Log::System, "LANSync: downloadSave connect failed %s_%d.%s -> %s:%d: %s",
		         gid.c_str(), sl, ext, peer.host.c_str(), peer.port, strerror(errno));
	}
	closesocket(sock);
```

- [ ] **Step 3: Log in PairWithPeer**

After the `connect()` failure:

```cpp
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		closesocket(sock);
		WARN_LOG(Log::System, "LANSync: PairWithPeer connect failed %s:%d: %s",
		         host.c_str(), port, strerror(errno));
		if (callback) callback(false, "Connection refused");
		return;
	}
```

- [ ] **Step 4: Log in AutoPairWithPeer**

After the first connection failure:

```cpp
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		closesocket(sock);
		WARN_LOG(Log::System, "LANSync: AutoPairWithPeer connect failed %s:%d: %s",
		         host.c_str(), port, strerror(errno));
		if (callback) callback(false, "Connection refused");
		return;
	}
```

- [ ] **Step 5: Commit**

```bash
git add Core/SaveStateLANSync.cpp
git commit -m "fix(lansync): add WARN_LOG on all connection failures

Log host:port and errno on every connect() failure in uploadSave,
downloadSave, PairWithPeer, and AutoPairWithPeer. Previously
failed silently — impossible to debug network issues."
```

---

### Task 4: Update BUGS.md — Correct Bug Status

**Files:**
- Modify: `BUGS.md`

- [ ] **Step 1: Update Bug #4 — mark as NOT a bug**

Find `### #4: Server Hanya Baca 4KB Request` and replace status + fix note:

```markdown
### #4: Server Hanya Baca 4KB Request

**Status**: ✅ Not a bug (2026-06-11)

**Analysis**: The POST upload handler (lines 560-590) has its own `while (bytesRead < contentLength)`
streaming loop that reads all Content-Length bytes from the socket, independent of the general
handler's 200-iteration limit. The general handler reads the first ~3.2MB into a buffer, and
the POST handler reads the remaining bytes from the TCP stream. Large uploads work correctly.
Memory optimization (avoid 3.2MB double-buffer) is deferred as low-priority.
```

- [ ] **Step 2: Update Bug #5 — mark as already fixed**

Find `### #5: Conflict Resolution Stubbed` and replace:

```markdown
### #5: Conflict Resolution Stubbed

**Status**: ✅ Fixed (pre-2026-06-11, BUGS.md was stale)

**Analysis**: `ResolveConflict()` (line 1466) fully downloads .ppst + .jpg thumbnail from peer,
saves to local disk with atomic .tmp → rename. `ResolveAllConflicts()` (line 1574) iterates all
pending conflicts and calls ResolveConflict(), then clears the queue. KEEP_LOCAL returns early.
BUGS.md previously claimed these were stubs — they are fully implemented.
```

- [ ] **Step 3: Update Bug #6 — mark as fixed**

Find `### #6: Server Tidak Validasi Token` and replace status:

```markdown
### #6: Server Tidak Validasi Token

**Status**: ✅ Fixed (2026-06-11)

**Fix**: Added `ExtractBearerToken()` helper and pre-route auth check in server handler.
All `/api/v1/saves/*` endpoints now validate `Authorization: Bearer <token>` against
paired peer tokens. Returns 401 for unauthenticated requests. Pairing endpoints
remain unauthenticated (PIN-based).
```

- [ ] **Step 4: Update summary table**

Find the summary table at the bottom of BUGS.md and update:

```markdown
| # | Bug | Priority | Status |
|---|-----|----------|--------|
| 4 | 4KB request limit | High | ✅ Not a bug |
| 5 | Conflict resolution stub | High | ✅ Fixed (stale BUGS.md) |
| 6 | No server auth | Medium | ✅ Fixed |
```

- [ ] **Step 5: Commit**

```bash
git add BUGS.md
git commit -m "docs: correct Bug #4, #5, #6 status in BUGS.md

Bug #4: Not a bug — POST handler has proper streaming
Bug #5: Already fixed — BUGS.md was stale
Bug #6: Fixed — token validation added"
```

---

### Task 5: Build Verification

- [ ] **Step 1: Check compilation with existing CMake**

```bash
cd /home/armanjr/gitproject/ppsspp/build-sdl
cmake .. 2>&1 | tail -5
make -j$(nproc) 2>&1 | tail -30
```

- [ ] **Step 2: Verify no regressions**

```bash
git log --oneline -5
```

Expected: 4 commits (Token validation → Timeouts → Error logging → BUGS.md)

- [ ] **Step 3: Sanity check — grep for unprotected saves endpoints**

```bash
grep -c "unauthorized" Core/SaveStateLANSync.cpp
```

Expected: ≥ 1 (auth check exists)

```bash
grep -c "SO_RCVTIMEO" Core/SaveStateLANSync.cpp
```

Expected: ≥ 8 (was 5 before, added 3+ client connections)

---

## Self-Review

- [x] Bug #6: Token extracted from Authorization header, validated against paired peers, 401 on fail
- [x] Socket timeouts: uploadSave 30s, PairWithPeer 30s, AutoPairWithPeer 15s+5s poll, DoSync 30s
- [x] Error logging: WARN_LOG with host:port + errno on every connect() failure
- [x] BUGS.md: #4 "not a bug", #5 "was already fixed", #6 "fixed now"
- [x] No TODOs, placeholders, or vague "add error handling"
- [x] Pairing endpoints remain unprotected (correct — PIN-based security)
- [x] `path` variable parsed before auth check (order verified)
- [x] pairedPeers_ accessed under peerMutex_ lock for thread safety

**Bug #4 elimination rationale:**
The POST handler at line 560 has `while (bytesRead < contentLength) { recv(...) }` — this IS proper streaming.
The general handler's 200-iteration loop reads the first ~3.2MB into `request` (wasteful but not broken),
then the POST handler reads remaining bytes from the stream. TCP ordering guarantees correctness.
Memory optimization (reading directly into `allBody` vector, skipping the intermediate `request` string)
is a performance improvement, not a bug fix.

**Deferred:**
- Bug #9 (TLS wiring) — requires OpenSSL integration planning
- Bug #8 (UDPDiscovery GetPeers empty) — need to verify if function is actually called
- Bug #10 (Windows keystore persistence) — platform-specific, not critical for Linux/Android
- Memory optimization for large uploads (avoid 3.2MB double-buffer in general handler)
