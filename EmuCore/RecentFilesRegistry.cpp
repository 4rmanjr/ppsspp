// [PPSSPP-FORK] MultiCore: Recent files registry implementation.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/RecentFilesRegistry.h"
#include "Core/Util/RecentFiles.h"

namespace EmuCore {

RecentFilesRegistry &RecentFilesRegistry::Get() {
	static RecentFilesRegistry instance;
	return instance;
}

void RecentFilesRegistry::Register(const RecentFilesEntry &entry) {
	// Avoid duplicate registration for the same coreType.
	for (const auto &e : entries_) {
		if (e.coreType == entry.coreType)
			return;
	}
	entries_.push_back(entry);
}

const RecentFilesEntry *RecentFilesRegistry::FindByType(int coreType) const {
	for (const auto &e : entries_) {
		if (e.coreType == coreType)
			return &e;
	}
	return nullptr;
}

const RecentFilesEntry *RecentFilesRegistry::FindBySpecialPath(const std::string &path) const {
	for (const auto &e : entries_) {
		if (("!" + e.specialPath) == path)
			return &e;
	}
	return nullptr;
}

RecentFilesManager *RecentFilesRegistry::GetManager(int coreType) const {
	const RecentFilesEntry *entry = FindByType(coreType);
	return entry ? entry->manager : nullptr;
}

const RecentFilesEntry *RecentFilesRegistry::FindByExtension(const std::string &ext) const {
	for (const auto &e : entries_) {
		if (!e.extensions.empty()) {
			// Check if ext (e.g., ".gba") is in the extensions list (e.g., ".gba:.gb:.gbc")
			if (e.extensions.find(ext) != std::string::npos)
				return &e;
		}
	}
	return nullptr;
}

void RecentFilesRegistry::ClearAll() {
	for (auto &e : entries_) {
		if (e.manager)
			e.manager->Clear();
	}
}

} // namespace EmuCore
