# STATUS.md — PPSSPP-LANSYNC

> Status proyek, issue yang masih **OPEN**, dan known limitations.
> Untuk riwayat bug/issue yang sudah **FIXED**, lihat [BUGS.md](BUGS.md).

---

## Rules & Constraints

**1. Zero upstream deletion** — Jangan hapus/komentari kode upstream. Di belakang `#ifdef PPSSPP_LANSYNC` diizinkan ubah/replace/hapus yang di-gate.

**2. Additive changes** — Saat tidak bisa 100% additive, `#ifdef PPSSPP_LANSYNC` boleh replace/hapus selama `#else` restore kode original.

**3. Commit style** — Subject: `type(lansync): deskripsi singkat`. Footer wajib: `[PPSSPP-FORK]` (1 baris kosong sebelum footer).

**4. Commit prefix wajib:** feat, fix, refactor, test, docs, ci, build, hotfix, revert, config, style, wip, perf, exp, chore, update, init, remove, etc.

**5. Perubahan kode upstream** — Harus dibungkus `#ifdef PPSSPP_LANSYNC` / `#endif` di satu file. Satu exception: `UI/OnScreenDisplay.h` + `.cpp` — fungsi baru di akhir file tanpa `#ifdef PPSSPP_LANSYNC` (inline, bisa di-skip saat build normal).

**6. Build tanpa PPSSPP_LANSYNC** — Semua perubahan harus **compile & link clean** tanpa flag `PPSSPP_LANSYNC` (Windows MSVC + Linux Clang/GCC, normal/debug/asan). Tidak boleh ada error, warning, atau linker error. CI compile di Windows MSVC wajib jalan (100% clean).

---

## Done

| Fokus | Session | Status |
|---|---|---|
| TLS dual-ctx + TOFU | 2026-07-10 | **FIXED** |
| Issues #11, #12, #14 | 2026-07-10 | **VERIFIED** |
| CR1–CR4 (TLS review) | 2026-07-10 | **FIXED** |
| Issue #7 pairing enforcement | 2026-07-13 | **FIXED** |
| Issue #8 tie-break | 2026-07-13 | **FIXED** |
| Issue #9 size limit | 2026-07-13 | **FIXED** |
| Issue #10 parse helper | 2026-07-13 | **FIXED** |
| Issue #13 self-detection via peerId | 2026-07-13 | **FIXED** |
| Android NSD permission auto-restart | 2026-07-14 | **FIXED** |
| Android 13 NEARBY_WIFI_DEVICES | 2026-07-14 | **FIXED** |
| SR2 JSON injection | 2026-07-14 | **FIXED** |
| SR1 false positive validated | 2026-07-14 | **FALSE POSITIVE** |
| SR4 currentSSL_ race → ConnectionCtx | 2026-07-16 | **FIXED** |
| SR3 confirmPin_ race | 2026-07-16 | **FIXED** |
| SR5–SR9 fixes | 2026-07-16 | **FIXED** |
| CMake `USE_TSAN` option | 2026-07-16 | **FIXED** |
| TSAN verification (16 warnings upstream, 0 in LANSync) | 2026-07-17 | **VERIFIED** |
| Smoke test 44/44 | 2026-07-17 | **VERIFIED** |
| TD1–TD4 fixes | 2026-07-13 | **FIXED** |
| TD6–TD7 permission hint | 2026-07-17 | **VERIFIED** |

---

## Remaining

| # | Severity | Issue | Status |
|---|----------|-------|--------|
| TD5 | Med | HLC conflict resolution | **OPEN** — preparatory done (hlcPhysical/hlcLogical on wire), actual resolution logic not implemented |

---

## Known Limitations

**1. OpenSSL Required**
- Code uses `SHA256`, `BIO_*`, `SSL_CTX_*`, `X509_*` directly.
- No portable alternative — OpenSSL ships on Linux, Android, macOS, Steam Deck, Flatpak. Windows needs DLL in `PPSSPP/`.
- **Mitigation:** `PPSSPP_USE_INTERNAL_TLS` conditional, clean build fallback, OpenBSD LibreSSL port possible.

**2. Linux SDL — Close Button**
- Linux SDL: close button (`WM_DELETE_WINDOW`) **bypasses** `RequestExitApp`/`ILOG("Quitting")` → process terminates immediately without graceful shutdown sequence.
- Android Back button: works correctly (graceful shutdown). Linux: broken.

**3. Firewall Requirements**
- Android 9+: TCP port 51967 (`PPSSPP_SERVER_PORT`) must be open.
- Linux (Avahi): UDP 5353 + TCP 5353, TCP 51967.
- Firewall blocks = no discovery; connection errors 403/500/502 ≠ firewall.
- Troubleshooting: `nc -zv <device-ip> 51967`.

---

## Session Log

### Session 2026-07-17 (part 3)
- **Code review findings documented:**
  - **TD6/TD7 log-spam:** `n->T(statusError)` logs "Missing translation" for non-permission errors (TD5/TD9/TD10). Deferred — prefix-guard `rfind("LANSyncPerm",0)==0` proposed.
  - **SR4 fail-closed decision:** `IsPeerTrusted("")` returns `false` (403) for unpaired devices in degraded path, vs old `!ssl → true` (serve). User decision: keep fail-closed. Document only, no code change.
  - **Popup reachability limitation:** Pre-existing — UI only reachable from SDL main screen, not from in-game.
