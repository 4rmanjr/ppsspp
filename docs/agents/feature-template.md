# Feature Template — Adding New Features

Use this checklist every time you add a new custom feature.

## 🔴 Mandatory Rules (Violation = Rollback)

1. ❌ **Forbidden** to delete, modify, or restructure upstream code
2. ❌ **Forbidden** to place custom files in `Core/`, `GPU/`, `HLE/`, `MIPS/`
3. ❌ **Forbidden** to refactor upstream to accommodate custom code
4. ❌ **Forbidden** to alter upstream flow via `#else`/`#endif`

## Required Checklist

### Isolation
- [ ] New feature does NOT modify/delete existing upstream code
- [ ] New feature implemented in **separate files**, not in PPSSPP core files
- [ ] New files placed in non-core directories (e.g., `EmuCore/`, `ext/`, `UI/`, `Common/Net/`, etc.)
- [ ] All new files have header `// [PPSSPP-FORK] <FeatureName>`

### Feature Flag
- [ ] New feature has its own **feature flag** (`PPSSPP_<NAME>`)
- [ ] Feature can be disabled with `cmake -DPPSSPP_<NAME>=OFF` — build still succeeds ✅

### If Touching Upstream Files (MAX 5 lines)
- [ ] Only **adds** new lines (does not modify/delete existing lines)
- [ ] Added code wrapped in `#ifdef PPSSPP_<NAME>`
- [ ] MUST have comment `// [PPSSPP-FORK] <FeatureName>: <explanation>`
- [ ] No `#else` that alters upstream flow

### Build Verification (REQUIRED Dual)
- [ ] Build with `-DPPSSPP_<NAME>=ON` — success without errors/warnings
- [ ] Build with `-DPPSSPP_<NAME>=OFF` — success without errors/warnings
- [ ] All new files added to CMakeLists.txt / build system

### Config & Settings
- [ ] Feature settings isolated in a separate config section (don't mix with PSP config)
- [ ] Default settings safe for all platforms

### Documentation
- [ ] Update `docs/agents/fork-maintenance.md` — register new flag in table
- [ ] Update `docs/agents/` — add/create feature-specific rules file if needed
- [ ] Update `AGENTS.md` — register new file in guidelines table
- [ ] Update design doc in `docs/superpowers/specs/` if architecture changes

## Implementation Steps

1. Choose feature name and preprocessor flag (`PPSSPP_<NAME>`)
2. Create new files in appropriate directories
3. Update CMakeLists.txt / build system
4. Update `docs/agents/fork-maintenance.md` — register new flag in table
5. Add/create feature-specific rules file in `docs/agents/`
6. Update `AGENTS.md` — register new file
7. Test build dual verification (ON + OFF)
8. Commit with message: `[feature/<name>] <description>`
