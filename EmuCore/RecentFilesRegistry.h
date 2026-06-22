// [PPSSPP-FORK] MultiCore: Recent files registry for grouping by emulator core.
// Central registry so adding a new core = one Register() call.
// Jangan hapus, jangan ubah kode upstream.

#pragma once

#include <string>
#include <vector>

// Forward declare RecentFilesManager — defined in Core/Util/RecentFiles.h
class RecentFilesManager;

namespace EmuCore {

// Per-core recent files entry. Add new cores by registering one of these.
struct RecentFilesEntry {
	// Core type identifier (EmuCore::Type enum value as int, for switch compatibility).
	int coreType = -1;

	// Display name used in Recent tab header. Example: "PSP", "GBA".
	std::string displayName;

	// INI section name. Example: "Recent" (PSP), "GBA Recent" (GBA).
	std::string iniSection;

	// GameBrowser special path identifier. Example: "!RECENT", "!RECENT_GBA".
	std::string specialPath;

	// Pointer to the RecentFilesManager instance for this core.
	RecentFilesManager *manager = nullptr;

	// Optional file extension filter: return true to KEEP the file.
	// PSP uses this to exclude .gba/.gb/.gbc files from its section.
	bool (*filter)(const std::string &path) = nullptr;

	// Extension list for DetectType (e.g., ".gba:.gb:.gbc").
	std::string extensions;
};

// Singleton registry of all emulator core recent file entries.
// Use Register() on startup, then iterate GetAll() for load/save/render.
class RecentFilesRegistry {
public:
	static RecentFilesRegistry &Get();

	void Register(const RecentFilesEntry &entry);

	const std::vector<RecentFilesEntry> &GetAll() const { return entries_; }

	const RecentFilesEntry *FindByType(int coreType) const;
	const RecentFilesEntry *FindBySpecialPath(const std::string &path) const;
	RecentFilesManager *GetManager(int coreType) const;
	const RecentFilesEntry *FindByExtension(const std::string &ext) const;

	void ClearAll();

private:
	RecentFilesRegistry() = default;
	std::vector<RecentFilesEntry> entries_;
};

} // namespace EmuCore
