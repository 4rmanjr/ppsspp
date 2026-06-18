# AGENTS.md Design for PPSSPP Fork

**Date:** 2026-06-18
**Project:** PPSSPP fork (`feature/lan-sync`) with LAN sync custom features
**Upstream:** https://github.com/hrydgard/ppsspp

## Requirements

1. All feature additions must not break/delete/alter the upstream PPSSPP codebase.
2. The fork must remain compatible with both minor and major updates from the official PPSSPP repo.
3. Custom features must be modular and isolated from the main codebase.
4. Merge strategy: `git merge` (not rebase).
5. Future features should be easy to add without architectural changes.

## File Structure

```
ppsspp/
├── AGENTS.md                                    # Entry point (lightweight)
├── docs/agents/
│   ├── fork-maintenance.md                      # Upstream merge strategy
│   ├── lansync-development.md                   # LAN sync feature rules
│   ├── code-standards.md                        # C++ style/platform rules
│   └── feature-template.md                      # New feature checklist
```

## Key Design Decisions

### Isolation Mechanism
- Preprocessor flags (`#ifdef PPSSPP_<FEATURE>`) for all custom code
- Each feature has its own flag (e.g., `PPSSPP_LANSYNC`, `PPSSPP_MDNS`, etc.)
- Features can be disabled at compile time without breaking the build

### File Placement
- New files go to platform/function directories (SDL/, Windows/, Common/Net/, etc.)
- NOT placed in Core/, GPU/, or other emulator core directories
- Platform-specific code is separated per platform directory

### Upstream File Touching
- Only ADD lines, never modify/delete existing lines
- Added lines wrapped in `#ifdef PPSSPP_*` with comment markers
- Comment format: `// [PPSSPP-FORK] FeatureName: description`

### Merge Conflict Resolution
- Upstream code ALWAYS wins during conflicts
- Custom code is adjusted/moved around upstream changes
- If unresolvable, custom code can be temporarily disabled via `#ifdef`

### Build Validation
- `./b.sh --debug` must pass after any change
- No new warnings in upstream code sections

## Content Summary Per File

| File | Purpose |
|------|---------|
| AGENTS.md | Intro, priorities, file index, global rules |
| fork-maintenance.md | Merge strategy, conflict handling, feature flag table |
| lansync-development.md | Specific rules for LAN sync features, directory map |
| code-standards.md | C++17, naming, platform handling, commit messages |
| feature-template.md | Checklist & steps for adding new features |
