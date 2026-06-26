# Fork Maintenance — Upstream Merge Strategy

## Merge Strategy

Use `git merge` (not rebase) to pull changes from upstream.

```bash
git fetch upstream
git merge upstream/master
```

## When a Conflict Occurs

1. **Upstream code ALWAYS wins.** Never modify original PPSSPP code to accommodate custom code.
2. Custom code that conflicts is moved or adjusted, not upstream code.
3. If a conflict occurs in a difficult area, the custom file can be temporarily disabled with `#ifdef` and the diff saved as a separate patch.

## File Isolation — Strict Rules

- All custom features MUST be in **separate files** in non-core directories.
- Custom files are **FORBIDDEN** in `Core/`, `GPU/`, `HLE/`, `MIPS/`, or any core emulator directory.

### If Touching Upstream Files

This is only allowed for **minimal hooks** (1-5 lines). If more, redesign the approach.

**REQUIRED:**
1. Only **add** new lines — **do not delete, change, or move** existing lines.
2. Added code MUST be wrapped in `#ifdef PPSSPP_<FEATURE>`.
3. MUST have comment `// [PPSSPP-FORK] FeatureName: description` on every block.
4. Custom blocks MUST be in **clear locations** (near relevant code), not hidden in the middle of logic.

**Build verification after touching upstream:**
- `cmake -DPPSSPP_<FEATURE>=ON .. && make -j$(nproc)` ✅
- `cmake -DPPSSPP_<FEATURE>=OFF .. && make -j$(nproc)` ✅

## Periodic Updates

- Merge upstream regularly (after upstream releases / when important patches land).
- Don't let the fork fall too far behind upstream — the further behind, the higher the conflict risk.

## Feature Flags

Every custom feature has its own preprocessor flag:

| Flag | Feature |
|------|---------|
| `PPSSPP_LANSYNC` | LAN sync core |
| `PPSSPP_MDNS` | MDNS discovery |
| `PPSSPP_TLS` | TLS server/secure communication |
| `PPSSPP_UDP_DISCOVERY` | UDP peer discovery |
| `PPSSPP_QR` | QR code pairing |
| `PPSSPP_PLATFORM_KEYSTORE` | Platform key store |
| `PPSSPP_MULTICORE` | Multi-emulator (GBA, future cores) |

Upstream code may use `#ifdef PPSSPP_*` for additional blocks, but there must be no `#else` or `#endif` that alters upstream code flow.
