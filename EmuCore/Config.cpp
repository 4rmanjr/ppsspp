// [PPSSPP-FORK] MultiCore: Per-core config management
// Saves/restores g_Config state when switching between PSP and GBA cores.
// Uses [GBA] section in ppsspp.ini for GBA overrides.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"

namespace EmuCore {

// Snapshot of key config values for PSP/GBA switching
struct ConfigSnapshot {
	int iInternalResolution;
	int iTexFiltering;
	bool bVSync;
	int iShowStatusFlags;
	bool bShowTouchControls;
	int iGameVolume;
};

static ConfigSnapshot savedPSPConfig;
static bool hasSavedPSPConfig = false;

const char *GetConfigSection(Type coreType) {
	switch (coreType) {
	case Type::GBA:  return "GBA";
	case Type::PSP:
	default:         return "PSP";
	}
}

void SaveCurrentConfig() {
	savedPSPConfig.iInternalResolution = g_Config.iInternalResolution;
	savedPSPConfig.iTexFiltering = g_Config.iTexFiltering;
	savedPSPConfig.bVSync = g_Config.bVSync;
	savedPSPConfig.iShowStatusFlags = g_Config.iShowStatusFlags;
	savedPSPConfig.bShowTouchControls = g_Config.bShowTouchControls;
	savedPSPConfig.iGameVolume = g_Config.iGameVolume;
	hasSavedPSPConfig = true;

	NOTICE_LOG(Log::System, "[CONFIG] Saved PSP config: res=%d filter=%d vsync=%d volume=%d",
		savedPSPConfig.iInternalResolution, savedPSPConfig.iTexFiltering,
		savedPSPConfig.bVSync, savedPSPConfig.iGameVolume);
}

void RestoreSavedConfig() {
	if (!hasSavedPSPConfig) return;

	g_Config.iInternalResolution = savedPSPConfig.iInternalResolution;
	g_Config.iTexFiltering = savedPSPConfig.iTexFiltering;
	g_Config.bVSync = savedPSPConfig.bVSync;
	g_Config.iShowStatusFlags = savedPSPConfig.iShowStatusFlags;
	g_Config.bShowTouchControls = savedPSPConfig.bShowTouchControls;
	g_Config.iGameVolume = savedPSPConfig.iGameVolume;
	hasSavedPSPConfig = false;

	NOTICE_LOG(Log::System, "[CONFIG] Restored PSP config");
}

void ApplyGBADefaults() {
	// GBA pixel art needs nearest-neighbor filtering
	g_Config.iTexFiltering = 0;
	g_Config.iInternalResolution = 0;  // Auto
	g_Config.iShowStatusFlags = 0;

	NOTICE_LOG(Log::System, "[CONFIG] Applied GBA defaults (nearest filter, auto res)");
}

// Load GBA-specific config overrides from ppsspp.ini [GBA] section
static void LoadGBAOverrides(IniFile &ini) {
	const Section *gbaSection = ini.GetSection("GBA");
	if (!gbaSection) return;

	NOTICE_LOG(Log::System, "[CONFIG] Loading GBA config from [GBA] section");
	int val;
	if (gbaSection->Get("iInternalResolution", &val)) g_Config.iInternalResolution = val;
	if (gbaSection->Get("iTexFiltering", &val))       g_Config.iTexFiltering = val;
	if (gbaSection->Get("bVSync", &val))               g_Config.bVSync = val != 0;
	if (gbaSection->Get("iShowStatusFlags", &val))     g_Config.iShowStatusFlags = val;
	if (gbaSection->Get("bShowTouchControls", &val))   g_Config.bShowTouchControls = val != 0;
	if (gbaSection->Get("iGameVolume", &val))          g_Config.iGameVolume = val;
}

// Save GBA-specific config overrides to ppsspp.ini [GBA] section
static void SaveGBAOverrides(IniFile &ini) {
	Section *gbaSection = ini.GetOrCreateSection("GBA");
	gbaSection->Set("iInternalResolution", g_Config.iInternalResolution);
	gbaSection->Set("iTexFiltering", g_Config.iTexFiltering);
	gbaSection->Set("bVSync", (int)g_Config.bVSync);
	gbaSection->Set("iShowStatusFlags", g_Config.iShowStatusFlags);
	gbaSection->Set("bShowTouchControls", (int)g_Config.bShowTouchControls);
	gbaSection->Set("iGameVolume", g_Config.iGameVolume);
	NOTICE_LOG(Log::System, "[CONFIG] Saved GBA config to [GBA] section");
}

void LoadConfig(Type coreType) {
	if (coreType != Type::GBA) return;

	NOTICE_LOG(Log::System, "[CONFIG] LoadConfig(GBA) — saving PSP config first");
	SaveCurrentConfig();

	Path iniPath = GetSysDirectory(DIRECTORY_SYSTEM) / "ppsspp.ini";
	NOTICE_LOG(Log::System, "[CONFIG] Looking for config at: %s", iniPath.c_str());

	IniFile ini;
	if (ini.Load(iniPath)) {
		NOTICE_LOG(Log::System, "[CONFIG] Config loaded, checking [GBA] section");
		LoadGBAOverrides(ini);
	} else {
		NOTICE_LOG(Log::System, "[CONFIG] No config file, applying GBA defaults");
		ApplyGBADefaults();
	}
}

void SaveConfig(Type coreType) {
	if (coreType != Type::GBA) return;

	Path iniPath = GetSysDirectory(DIRECTORY_SYSTEM) / "ppsspp.ini";
	NOTICE_LOG(Log::System, "[CONFIG] SaveConfig(GBA) — writing to %s", iniPath.c_str());

	// Ensure directory exists
	File::CreateFullPath(GetSysDirectory(DIRECTORY_SYSTEM));

	IniFile ini;
	if (!ini.Load(iniPath)) {
		NOTICE_LOG(Log::System, "[CONFIG] No existing config, creating new");
	}
	SaveGBAOverrides(ini);

	if (ini.Save(iniPath)) {
		NOTICE_LOG(Log::System, "[CONFIG] Wrote GBA config OK");
	} else {
		WARN_LOG(Log::System, "[CONFIG] Failed to save config to %s", iniPath.c_str());
	}

	NOTICE_LOG(Log::System, "[CONFIG] Restoring PSP config");
	RestoreSavedConfig();
}

std::string GetSavestateDir(Type coreType) {
	switch (coreType) {
	case Type::GBA: return (GetSysDirectory(DIRECTORY_SYSTEM) / "PPSSPP_STATE" / "GBA").ToString();
	case Type::PSP:
	default:        return (GetSysDirectory(DIRECTORY_SYSTEM) / "PPSSPP_STATE" / "PSP").ToString();
	}
}

}  // namespace EmuCore
