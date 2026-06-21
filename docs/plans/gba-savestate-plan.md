# Plan: GBA Save State — Zero Breaking Change
> **STATUS: ✅ SELESAI — All tasks implemented (F1/F3, pause menu, thumbnail, LAN sync)**

## Tujuan
Save/load state untuk GBA via F1/F3 dan pause menu.
File save state berada di direktori yang sama dengan PSP agar ter-sync oleh LAN sync.

## Constraint
- ✅ Pakai mekanisme yang sudah ada (`GBACore::SaveStateToFile`)
- ✅ Zero breaking change ke kode upstream
- ✅ File location kompatibel dengan LAN Save State Sync

---

## PSP Save State (Existing)

```
Trigger (F1/pause) → SaveState::SaveSlot(prefix, slot, callback)
  → GenerateSaveSlotPath(prefix, slot, "ppst")
  → <SAVESTATE_DIR>/<gameID>_<slot>.ppst
  → Async serialize via CChunkFileReader
  → OSD: "State saved (slot N)"
```

```
LAN Sync: scan *.ppst di <SAVESTATE_DIR>/ → sync by SHA256
```

---

## GBA Save State (Target)

### File Path

```
<SAVESTATE_DIR>/GBA_<sanitized_title>_<slot>.ppst
```

- **Direktori:** Sama dengan PSP (`DIRECTORY_SAVESTATE`) ✅
- **Extension:** `.ppst` — langsung terdeteksi LAN sync ✅
- **Prefix:** `GBA_` — tidak bentrok dengan PSP game ID ✅
- **Contoh:** `PSP/PPSSPP_STATE/GBA_Breath_of_Fire_0.ppst`

### Trigger Points

| Trigger | Dari | Handler |
|---------|------|---------|
| **F1** | VIRTKEY_SAVE_STATE → ProcessVKey | Panggil `GBACore::SaveStateToFile(slot)` |
| **F3** | VIRTKEY_LOAD_STATE → ProcessVKey | Panggil `GBACore::LoadStateFromFile(slot)` |
| **Pause Menu "Save State"** | `ScreenshotViewScreen::OnSaveState` | Cek `IsGBA()` → panggil GBACore |
| **Pause Menu "Load State"** | `ScreenshotViewScreen::OnLoadState` | Cek `IsGBA()` → panggil GBACore |
| **OSD** | Setelah save/load | `g_OSD.Show()` (sudah ada) |

### File Changes

| # | File | Perubahan | Risk |
|---|------|-----------|------|
| 1 | `EmuCore/GBACore.cpp` | Update `SaveStateToFile/LoadStateFromFile` — simpan ke `<SAVESTATE_DIR>/` tanpa subfolder `/GBA/` | Rendah |
| 2 | `UI/EmuScreen.cpp` | `ProcessQueuedVKeys()` sudah dipanggil ✅ — VIRTKEY handler sudah ada kode GBA ✅ | **Zero change** |
| 3 | `UI/PauseScreen.cpp` | `ScreenshotViewScreen::OnSaveState/OnLoadState` — tambah `#ifdef PPSSPP_MULTICORE` + `IsGBA()` check — redirect ke GBACore | Rendah |
| 4 | `Core/SaveState.cpp` | **Tidak disentuh** | **Zero change** |
| 5 | `Core/SaveStateLANSync.cpp` | **Tidak disentuh** — GBA `.ppst` files akan ter-scan otomatis | **Zero change** |

### File Format

GBA save state = raw binary dari `core_->saveState()` (mGBA internal state):
- Bukan PSP `CChunkFileReader` format
- Tapi LAN sync hanya compute SHA256 + sync byte-by-byte → format tidak masalah
- Ekstensi `.ppst` sama → LAN sync treat sebagai file biasa

### OSD Messages

Sudah ada di `EmuScreen.cpp` VIRTKEY handler:
- ✅ `"GBA state saved (slot N)"` — success
- ✅ `"GBA save state failed"` — failure
- ✅ `"No GBA save state in slot N"` — no file
- Hanya perlu ditambahkan di pause menu handler nanti

---

## Perubahan Detail

### 1. `EmuCore/GBACore.cpp` — Update path

```cpp
// Before:
Path dir = GetSysDirectory(DIRECTORY_SAVESTATE) / "GBA";
Path path = dir / filename;  // <SAVESTATE>/GBA/<prefix>_N.gbast

// After:
Path dir = GetSysDirectory(DIRECTORY_SAVESTATE);
Path path = dir / ("GBA_" + prefix + "_" + slot + ".ppst");
```

### 2. `UI/PauseScreen.cpp` — Redirect ke GBACore

```cpp
// Di ScreenshotViewScreen::OnSaveState / OnLoadState:
#ifdef PPSSPP_MULTICORE
if (g_emuScreen && g_emuScreen->IsGBA()) {
    auto *gba = static_cast<EmuCore::GBACore*>(g_emuScreen->activeCore_.get());
    gba->SaveStateToFile(slot);
    g_OSD.Show(...);
    return;
}
#endif
```

---

## Rollback Plan

Jika terjadi masalah:
1. Hapus file `.ppst` dengan prefix `GBA_` dari `<SAVESTATE_DIR>/`
2. Revert perubahan di `EmuCore/GBACore.cpp` dan `UI/PauseScreen.cpp`
3. PSP save state tidak terpengaruh sama sekali
