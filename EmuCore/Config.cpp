// [PPSSPP-FORK] MultiCore: Per-core config management
// Saves/restores g_Config state when switching between PSP and GBA cores.
// Uses [GBA] section in ppsspp.ini for GBA overrides.
// Jangan hapus, jangan ubah kode upstream.

#include "EmuCore/Config.h"
#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Util/PathUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"

namespace EmuCore {

// Per-core touch config arrays
CoreTouchConfig g_coreTouchLandscape[(int)Type::COUNT];
CoreTouchConfig g_coreTouchPortrait[(int)Type::COUNT];

const char *GetTouchConfigSection(Type coreType, bool portrait) {
	switch (coreType) {
	case Type::GBA:
		return portrait ? "GBA ControlLayoutPortrait" : "GBA ControlLayout";
	case Type::PSP:
	default:
		return portrait ? "ControlLayoutPortrait" : "ControlLayout";
	}
}

// Default GBA touch layout — matches TouchLayoutGBA::GetLayout()
void FillDefaultGBATouchLayout(CoreTouchConfig &cfg) {
	cfg.Clear();
	// D-Pad
	cfg.Add(CTRL_UP,       0.05f, 0.35f, 0.08f, 0.08f, "\xe2\x96\xb2");  // ▲
	cfg.Add(CTRL_DOWN,     0.05f, 0.50f, 0.08f, 0.08f, "\xe2\x96\xbc");  // ▼
	cfg.Add(CTRL_LEFT,     0.00f, 0.43f, 0.07f, 0.08f, "\xe2\x97\x80");  // ◀
	cfg.Add(CTRL_RIGHT,    0.10f, 0.43f, 0.07f, 0.08f, "\xe2\x96\xb6");  // ▶
	// A & B buttons (GBA layout: A right, B left-down)
	cfg.Add(CTRL_CROSS,    0.82f, 0.47f, 0.09f, 0.09f, "A");
	cfg.Add(CTRL_CIRCLE,   0.73f, 0.38f, 0.09f, 0.09f, "B");
	// L & R
	cfg.Add(CTRL_LTRIGGER, 0.15f, 0.02f, 0.10f, 0.06f, "L");
	cfg.Add(CTRL_RTRIGGER, 0.75f, 0.02f, 0.10f, 0.06f, "R");
	// Start & Select
	cfg.Add(CTRL_SELECT,   0.40f, 0.12f, 0.08f, 0.05f, "Select");
	cfg.Add(CTRL_START,    0.52f, 0.12f, 0.08f, 0.05f, "Start");
}

void InitDefaultTouchConfigs() {
	// Clear all core touch configs first (except PSP — uses its own system)
	for (int i = 0; i < (int)Type::COUNT; i++) {
		g_coreTouchLandscape[i].Clear();
		g_coreTouchPortrait[i].Clear();
	}
	// GBA — both orientations
	FillDefaultGBATouchLayout(g_coreTouchLandscape[(int)Type::GBA]);
	// Portrait: swap positions for tall screen
	{
		auto &cfg = g_coreTouchPortrait[(int)Type::GBA];
		cfg.Clear();
		cfg.Add(CTRL_UP,       0.05f, 0.50f, 0.10f, 0.08f, "\xe2\x96\xb2");
		cfg.Add(CTRL_DOWN,     0.05f, 0.68f, 0.10f, 0.08f, "\xe2\x96\xbc");
		cfg.Add(CTRL_LEFT,     0.00f, 0.59f, 0.07f, 0.08f, "\xe2\x97\x80");
		cfg.Add(CTRL_RIGHT,    0.13f, 0.59f, 0.07f, 0.08f, "\xe2\x96\xb6");
		cfg.Add(CTRL_CROSS,    0.80f, 0.62f, 0.11f, 0.10f, "A");
		cfg.Add(CTRL_CIRCLE,   0.68f, 0.52f, 0.11f, 0.10f, "B");
		cfg.Add(CTRL_LTRIGGER, 0.10f, 0.02f, 0.12f, 0.06f, "L");
		cfg.Add(CTRL_RTRIGGER, 0.78f, 0.02f, 0.12f, 0.06f, "R");
		cfg.Add(CTRL_SELECT,   0.35f, 0.25f, 0.12f, 0.06f, "Select");
		cfg.Add(CTRL_START,    0.53f, 0.25f, 0.12f, 0.06f, "Start");
	}
	// PSP — leave empty, uses existing TouchControlConfig system
}

void LoadTouchConfig(Type coreType) {
	Path iniPath = GetSysDirectory(DIRECTORY_SYSTEM) / "ppsspp.ini";
	IniFile ini;
	if (!ini.Load(iniPath)) {
		// No config file — use defaults
		InitDefaultTouchConfigs();
		return;
	}

	const char *sectionName = GetTouchConfigSection(coreType, false);
	const Section *sec = ini.GetSection(sectionName);
	if (!sec) {
		// Section missing — use defaults
		InitDefaultTouchConfigs();
		return;
	}

	CoreTouchConfig &cfg = GetTouchConfigMutable(coreType, false);
	cfg.Clear();
	for (int i = 0; i < MAX_TOUCH_BUTTONS; i++) {
		char key[32];
		snprintf(key, sizeof(key), "btn%d", i);
		std::string val;
		if (!sec->Get(key, &val)) break;
		// Format: keyCode,x,y,w,h,label,visible
		int kc = 0;
		float x = 0, y = 0, w = 0, h = 0;
		char label[16] = "";
		int vis = 1;
		if (sscanf(val.c_str(), "%d,%f,%f,%f,%f,%15[^,],%d", &kc, &x, &y, &w, &h, label, &vis) >= 6) {
			cfg.Add(kc, x, y, w, h, label, vis != 0);
		}
	}

	// Load portrait too
	const char *sectionNameP = GetTouchConfigSection(coreType, true);
	const Section *secP = ini.GetSection(sectionNameP);
	CoreTouchConfig &cfgP = GetTouchConfigMutable(coreType, true);
	cfgP.Clear();
	if (secP) {
		for (int i = 0; i < MAX_TOUCH_BUTTONS; i++) {
			char key[32];
			snprintf(key, sizeof(key), "btn%d", i);
			std::string val;
			if (!secP->Get(key, &val)) break;
			int kc = 0;
			float x = 0, y = 0, w = 0, h = 0;
			char label[16] = "";
			int vis = 1;
			if (sscanf(val.c_str(), "%d,%f,%f,%f,%f,%15[^,],%d", &kc, &x, &y, &w, &h, label, &vis) >= 6) {
				cfgP.Add(kc, x, y, w, h, label, vis != 0);
			}
		}
	}

	NOTICE_LOG(Log::System, "[CONFIG] Loaded touch config for %s (%d landscape, %d portrait buttons)",
		GetConfigSection(coreType), cfg.count, cfgP.count);
}

void SaveTouchConfig(Type coreType) {
	Path iniPath = GetSysDirectory(DIRECTORY_SYSTEM) / "ppsspp.ini";
	File::CreateFullPath(GetSysDirectory(DIRECTORY_SYSTEM));
	IniFile ini;
	ini.Load(iniPath);

	// Save landscape
	{
		Section *sec = ini.GetOrCreateSection(GetTouchConfigSection(coreType, false));
		const CoreTouchConfig &cfg = GetTouchConfig(coreType, false);
		for (int i = 0; i < cfg.count; i++) {
			char key[32], val[128];
			snprintf(key, sizeof(key), "btn%d", i);
			snprintf(val, sizeof(val), "%d,%f,%f,%f,%f,%s,%d",
				cfg.buttons[i].keyCode,
				cfg.buttons[i].x, cfg.buttons[i].y,
				cfg.buttons[i].w, cfg.buttons[i].h,
				cfg.buttons[i].label,
				cfg.buttons[i].visible ? 1 : 0);
			sec->Set(key, val);
		}
	}

	// Save portrait
	{
		Section *sec = ini.GetOrCreateSection(GetTouchConfigSection(coreType, true));
		const CoreTouchConfig &cfg = GetTouchConfig(coreType, true);
		for (int i = 0; i < cfg.count; i++) {
			char key[32], val[128];
			snprintf(key, sizeof(key), "btn%d", i);
			snprintf(val, sizeof(val), "%d,%f,%f,%f,%f,%s,%d",
				cfg.buttons[i].keyCode,
				cfg.buttons[i].x, cfg.buttons[i].y,
				cfg.buttons[i].w, cfg.buttons[i].h,
				cfg.buttons[i].label,
				cfg.buttons[i].visible ? 1 : 0);
			sec->Set(key, val);
		}
	}

	ini.Save(iniPath);
	NOTICE_LOG(Log::System, "[CONFIG] Saved touch config for %s", GetConfigSection(coreType));
}

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
