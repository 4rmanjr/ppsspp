# Code Review Learning Log

Track bug patterns found during code reviews for continuous improvement.

## Code Review Learning Log

| Date | Bug Summary | Pattern Type | P-Level | File:Line | Skill Updated |
|------|-------------|-------------|---------|-----------|---------------|
| 2026-07-03 | pairingPin_ race between UI and server threads | Threading | P2 | SaveStateLANSync.cpp:699,787 | no (existing rule) |
| 2026-07-03 | pendingRequestCounter_ non-atomic increment | Threading | P2 | SaveStateLANSync.cpp:1867 | no (existing rule) |
| 2026-07-03 | syncStatus_ set before syncProgress_ initialized | Threading | P2 | SaveStateLANSync.cpp:1167-1176 | no (existing rule) |
| 2026-07-03 | Blocking destructor JoinAllThreads without cancellation | Lifecycle | P2 | SaveStateLANSync.cpp:173-176 | yes (Step 5) |
| 2026-07-03 | g_LANSyncConfig vs g_Config.lanSync dual state | Config/State | P2 | LANSyncConfig.cpp:7 | yes (Step 5) |
| 2026-07-03 | TLS certs generated but AcceptTLS never called | Security | P2 | SaveStateLANSync.cpp:354-671 | yes (Step 5) |
| 2026-07-03 | DownloadSave buffer unbounded growth | Memory Safety | P4 | SaveStateLANSync.cpp:1082-1087 | yes (Step 5) |
| 2026-07-03 | SaveConfig() from background threads | Config/State | P4 | SaveStateLANSync.cpp:814,994+ | no (existing rule covers loops only) |
| 2026-07-03 | SDLLANSyncUI new'd but never deleted | Memory Safety | P4 | EmuScreen.cpp:1915 | no (existing rule) |
| 2026-07-03 | slot from HTTP atoi() without validation | Input Validation | P4 | SaveStateLANSync.cpp:653-654 | no (new pattern) |
| 2026-07-03 | fprintf(stderr) instead of INFO_LOG | Logging | P4 | SaveStateLANSync.cpp:1161+ | no (existing rule) |
| 2026-07-03 | Missing [PPSSPP-FORK] markers on upstream hooks | AGENTS.md | P5 | SaveState.cpp, EmuScreen.cpp+ | no (existing rule) |
| 2026-07-03 | Custom files in Core/ directory | AGENTS.md | P5 | Core/SaveStateLANSync.* | no (existing rule) |
| 2026-07-03 | PPSSPP_LANSYNC feature flag not implemented | AGENTS.md | P5 | N/A | no (existing rule) |
