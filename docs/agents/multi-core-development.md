# Multi-Core Development — Aturan Khusus Multi-Emulator

## Arsitektur

Semua emulator core (selain PSP) menggunakan **EmuCore abstraction layer**:

```
EmuCore/            → Folder baru, tidak sentuh upstream
├── EmuCore.h       → Interface abstrak untuk semua core
├── EmuCore.cpp     → Factory: Create() + DetectType()
├── PSPCore.h/.cpp  → Wrapper PSP (delegasi ke sistem existing)
├── GBACore.h/.cpp  → GBA via libmgba
└── CMakeLists.txt
```

## Adding a New Emulator Core — Panduan Langkah demi Langkah

### Step 0: Naming Convention

| Aspek | Convention | Contoh (Core X) |
|-------|------------|-----------------|
| Core ID | `UPPER` | `N64`, `PS1` |
| Core Name | `PascalCase` | `N64`, `PS1` |
| Feature flag | `PPSSPP_<ID>` | `PPSSPP_N64` |
| Type enum | `EmuCore::Type::<ID>` | `EmuCore::Type::N64` |
| File prefix | `<CoreName>Core` | `N64Core` |
| Namespace | `EmuCore` | `EmuCore` |
| Config section | `[<CoreName>]` | `[N64]` |
| Recent section | `[<CoreName> Recent]` | `[N64 Recent]` |
| VIRTKEY prefix | `VIRTKEY_<ID>_` | `VIRTKEY_N64_` |
| Log prefix | `[<ID>]` | `[N64]` |
| Save state ext | `.<coreabbv>st` | `.n64st` |
| Save state dir | `DIRECTORY_SAVESTATE / <CoreName> /` | `SAVESTATE/N64/` |
| Dev comment | `// [PPSSPP-FORK] <CoreName>:` | `// [PPSSPP-FORK] N64:` |

> **PENTING:** Konsistensi naming memudahkan verifikasi otomatis (grep, script, CI).

### Step 1: Tambah enum Type

**File:** `EmuCore/EmuCore.h`

```cpp
namespace EmuCore {

enum class Type {
    PSP,
    GBA,
    N64,        // ← tambah di sini
};

}  // namespace EmuCore
```

### Step 2: Buat file core

**File:** `EmuCore/<CoreName>Core.h`
**File:** `EmuCore/<CoreName>Core.cpp`

WAJIB mengikuti interface `EmuCore::Core`:

```cpp
// [PPSSPP-FORK] <CoreName>: <deskripsi singkat>
// <Detail>
#pragma once

#include "EmuCore/EmuCore.h"

namespace EmuCore {

class <CoreName>Core : public Core {
public:
    <CoreName>Core();
    ~<CoreName>Core() override;

    Type GetType() const override { return Type::<CoreName>; }

    // --- Lifecycle ---
    bool LoadROM(const Path &path) override;
    void RunFrame() override;
    void Reset() override;

    // --- Rendering ---
    void Render(Draw::DrawContext *draw) override;
    void DeviceLost() override;
    void DeviceRestored(Draw::DrawContext *draw) override;

    // --- Audio ---
    int GetAudioSampleRate() const override;
    void GetAudioSamples(int16_t *buffer, size_t *samples) override;

    // --- Input ---
    void SetKeys(uint32_t keys) override;
    uint32_t GetKeys() const override;

    // --- Savestate ---
    size_t GetStateSize() const override;
    bool SaveState(void *buffer) override;
    bool LoadState(const void *buffer) override;

    // --- Game Info ---
    void GetGameInfo(std::string &title, std::string &id) const override;

    // --- <CoreName>-specific (opsional) ---
    // void GetMixedAudio(int32_t *buffer, size_t *stereoPairs);
    // bool SaveStateToFile(int slot);
    // bool LoadStateFromFile(int slot);

private:
    // ...
};

}  // namespace EmuCore
```

### Step 3: Daftarkan di DetectType + Factory

**File:** `EmuCore/EmuCore.cpp`

```cpp
#include "EmuCore/<CoreName>Core.h"

Type DetectType(const Path &romPath) {
    // ...
#ifdef PPSSPP_<CORENAME>
    if (ext == ".n64" || ext == ".z64") {
        return Type::<CoreName>;
    }
#endif
    // ...
}

std::unique_ptr<Core> Create(const Path &romPath) {
    switch (type) {
#ifdef PPSSPP_<CORENAME>
    case Type::<CoreName>:
        return std::make_unique<<CoreName>Core>();
#endif
    // ...
    }
}
```

### Step 4: Tambah feature flag di CMake

**File:** `EmuCore/CMakeLists.txt`

```cmake
option(PPSSPP_MULTICORE "Enable multi-emulator support" ON)

# --- Existing ---
if(PPSSPP_MULTICORE)
    add_library(EmuCore STATIC
        EmuCore.cpp
        PSPCore.cpp
        GBACore.cpp
        Config.cpp
        RecentFilesRegistry.cpp
    )
    target_include_directories(EmuCore PUBLIC ${CMAKE_SOURCE_DIR}/ext/libmgba/include)
    target_link_libraries(EmuCore PUBLIC mgba)
endif()

# --- Tambah untuk core baru ---
# Jika core baru butuh external library (libn64, dll):
# if(PPSSPP_MULTICORE)
#     add_subdirectory(${CMAKE_SOURCE_DIR}/ext/lib<corename> ${CMAKE_BINARY_DIR}/ext/lib<corename>)
#     target_sources(EmuCore PRIVATE <CoreName>Core.cpp)
#     target_include_directories(EmuCore PUBLIC ${CMAKE_SOURCE_DIR}/ext/lib<corename>/include)
#     target_link_libraries(EmuCore PUBLIC <corename>)
# endif()
```

### Step 5: Daftarkan di RecentFilesRegistry

**File:** `UI/NativeApp.cpp`

```cpp
// [PPSSPP-FORK] <CoreName>: register recent files grouping
#ifdef PPSSPP_MULTICORE
auto &reg = EmuCore::RecentFilesRegistry::Get();
reg.Register(EmuCore::RecentFilesEntry{
    (int)EmuCore::Type::<CoreName>,
    "<CoreName>",                    // displayName
    "<CoreName> Recent",             // iniSection → [<CoreName> Recent] di ppsspp.ini
    "RECENT_<CORENAME>",             // specialPath
    &g_recentFiles<CoreName>,        // RecentFilesManager global
    nullptr,
    ".<ext1>:.<ext2>",               // extensions
});
#endif
```

Lalu di function init:

```cpp
// [PPSSPP-FORK] <CoreName>: init recent files
void Init<CoreName>() {
    auto path = GetSysDirectory(DIRECTORY_SAVESTATE);
    g_recentFiles<CoreName>.SetCompatPath(path);
}
```

### Step 6: Tambah config section

**File:** `Core/Config.h` — tambah struct config:

```cpp
// [PPSSPP-FORK] <CoreName>: settings
struct <CoreName>DisplayConfig {
    // ...
};
struct <CoreName>AudioConfig {
    // ...
};
extern <CoreName>DisplayConfig g_<lower>DisplayConfig;
extern <CoreName>AudioConfig g_<lower>AudioConfig;
```

**File:** `Core/Config.cpp` — load/save di section `[<CoreName>]`:

```cpp
// [PPSSPP-FORK] <CoreName>: load settings
g_<lower>DisplayConfig.someField = section->Get("someField", defaultVal);
```

### Step 7: Integrasi di EmuScreen

**File:** `UI/EmuScreen.h` — tambah helper:

```cpp
// [PPSSPP-FORK] <CoreName>: helpers
#ifdef PPSSPP_<CORENAME>
bool Is<CoreName>() const;
void Init<CoreName>();
void Shutdown<CoreName>();
void Update<CoreName>();
void Render<CoreName>(Draw::DrawContext *draw);
#endif
```

**File:** `UI/EmuScreen.cpp` — routing:

```cpp
// Di constructor — init core
// Di destructor — shutdown core
// Di update loop — run frame + audio
// Di render — draw
```

Gunakan `Is<CoreName>()` pattern biar tanpa `#ifdef` di call site:

```cpp
// EmuScreen.h — constexpr fallback
#ifdef PPSSPP_<CORENAME>
    bool Is<CoreName>() const { return coreType_ == EmuCore::Type::<CoreName>; }
#else
    static constexpr bool Is<CoreName>() { return false; }
#endif

// Call site — compiler hapus branch mati
if (Is<CoreName>()) {
    Do<CoreName>Stuff();
}
```

### Step 8: Per-Core Touch Layout (On-Screen Controls)

Setiap core yang butuh on-screen touch buttons WAJIB mendaftarkan default layout-nya.

**Mekanisme:**

1. Definisikan default buttons di `EmuCore::Config.cpp` lewat `FillDefault<CoreName>TouchLayout()`
2. Panggil di `EmuCore::InitDefaultTouchConfigs()`
3. `EmuScreen::AddGBATouchButtons()` (generic) otomatis baca dari config

**Config Storage:**

```ini
; PSP (existing — zero break)
[ControlLayout]
button0=0,0.82,0.47,0.09,0.09,18

; GBA (baru)
[GBA ControlLayout]
button0=0,0.82,0.47,0.09,0.09,A
button1=1,0.73,0.38,0.09,0.09,B
...

; Core baru
[<CoreName> ControlLayout]
button0=<keyCode>,<x>,<y>,<w>,<h>,<label>
...
```

**Format baris:** `keyCode,x,y,w,h,label,visible`

**Registrasi default layout di `EmuCore/Config.cpp`:**

```cpp
void InitDefaultTouchConfigs() {
    // GBA — landscape
    auto &gbaLand = g_coreTouchLandscape[(int)Type::GBA];
    gbaLand.Add(CTRL_CROSS,  0.82f, 0.47f, 0.09f, 0.09f, "A");
    gbaLand.Add(CTRL_CIRCLE, 0.73f, 0.38f, 0.09f, 0.09f, "B");
    
    // <CoreName> — landscape
    auto &nLand = g_coreTouchLandscape[(int)Type::<CORENAME>];
    nLand.Add(...);
}
```

**Customize lewat Settings:**

Button "Customize On-Screen Controls" → `CoreTouchLayoutScreen(gamePath, Type::<CORENAME>)`
Screen ini otomatis baca/save dari section config yang sesuai.

**Files:** `EmuCore/Config.h`, `EmuCore/Config.cpp`, `UI/CoreTouchLayoutScreen.h/.cpp`

### Step 9: File opsional (jika dibutuhkan)

| Komponen | File | Kapan Dibutuhkan |
|----------|------|------------------|
| Touch layout (legacy) | `UI/TouchLayout<CoreName>.h/.cpp` | **DEPRECATED** — gunakan per-core config |
| Settings screen | `UI/<CoreName>SettingsScreen.h/.cpp` | Core punya settings sendiri |
| Android audio | `android/jni/AndroidAudio.cpp` | Audio routing beda platform |
| Test | `test_<corename>_core.cpp` | Minimal load + run test |

---

## Standar Kode Penulisan

### 1. File Structure Convention

Setiap core WAJIB punya struktur file yang sama:

```
EmuCore/
├── <CoreName>Core.h      → class declaration + public interface
├── <CoreName>Core.cpp    → implementation
```

Urutan method di header & implementation HARUS SAMA:

```cpp
class <CoreName>Core : public Core {
public:
    // 1. Constructor/destructor
    // 2. Type
    // 3. Lifecycle (LoadROM, RunFrame, Reset)
    // 4. Rendering (Render, DeviceLost, DeviceRestored)
    // 5. Audio (GetAudioSampleRate, GetAudioSamples)
    // 6. Input (SetKeys, GetKeys)
    // 7. Savestate (GetStateSize, SaveState, LoadState)
    // 8. Game Info
    // 9. <CoreName>-specific methods
private:
    // Member variables — grouped by concern:
    // Video
    // Audio
    // Input
    // State
};
```

### 2. Comment Convention

Semua kode fork WAJIB pakai marker:

```cpp
// [PPSSPP-FORK] <CoreName>: <deskripsi>
// <Detail opsional — 1 baris atau multi baris>
```

Aturan:
- Baris pertama: `// [PPSSPP-FORK] <CoreName>: <kalimat imperative>`
- Baris berikutnya: `// <detail>`
- Bahasa: **English** untuk marker, Indonesia boleh untuk note internal
- Marker di SETIAP file kustom (header + implementation)
- Marker di SETIAP blok `#ifdef` di file upstream

### 3. #ifdef Convention

```cpp
#ifdef PPSSPP_<CORENAME>
    // Kode spesifik core
    // WAJIB: komentar [PPSSPP-FORK] <CoreName>: ...
#endif
```

Aturan:
- **JANGAN** letakkan `#else` dengan kode upstream di dalamnya
- **JANGAN** ubah indentasi kode upstream untuk accommodate `#ifdef`
- Blok `#ifdef` hanya **menambah**, tidak **mengubah** kode sekitar
- Setiap `#ifdef` WAJIB bisa di-flip OFF dan build tetap sukses

### 4. Method Extraction Pattern

Jika logic kustom > 5 baris di file upstream, extract ke helper:

```cpp
// Di file upstream — minimal, cukup 3 baris
#ifdef PPSSPP_<CORENAME>
    if (Is<CoreName>()) {
        Update<CoreName>();
        return;
    }
#endif

// Implementasi helper — semua logic di sini
// [PPSSPP-FORK] <CoreName>: <deskripsi>
void EmuScreen::Update<CoreName>() {
    // ... semua logic
}
```

Convention naming helper:
| Pattern | Contoh |
|---------|--------|
| `Init<CoreName>()` | `InitGBA()` |
| `Shutdown<CoreName>()` | `ShutdownGBA()` |
| `Update<CoreName>()` | `UpdateGBA()` |
| `Render<CoreName>()` | `RenderGBA()` |

### 5. Is<CoreName>() Pattern

```cpp
// Header — constexpr fallback untuk compiler hapus dead branch
#ifdef PPSSPP_<CORENAME>
    bool Is<CoreName>() const;
#else
    static constexpr bool Is<CoreName>() { return false; }
#endif

// Implementation
#ifdef PPSSPP_<CORENAME>
bool EmuScreen::Is<CoreName>() const {
    return coreType_ == EmuCore::Type::<CoreName>;
}
#endif
```

> **Kenapa?** Call site tanpa `#ifdef` = lebih mudah dibaca, lebih sedikit typo.
> Compiler otomatis buang `if (false)` branch.

### 6. Config Isolation

Setiap core WAJIB punya section config sendiri:

```ini
; ppsspp.ini
[PSP]
renderMode=1
frameskip=0

[GBA]
texFiltering=0
aspectRatio=0

[<CoreName>]
setting1=value1
setting2=value2
```

**DILARANG:** Satukan config core berbeda dalam satu section.
**WAJIB:** Setiap core load/save dari section masing-masing.

### 7. Feature Flag Convention

| Flag | Scope | Status |
|------|-------|--------|
| `PPSSPP_MULTICORE` | Global — enable all multi-core | ✅ Ada |
| `PPSSPP_GBA` | GBA-specific (future: pisah dari MULTICORE) | ⏳ Belum |
| `PPSSPP_<CORENAME>` | Per-core baru | ✅ Harus ada |

Aturan:
- Setiap core baru WAJIB punya flag sendiri: `PPSSPP_<CORENAME>`
- Flag didefinisikan di `EmuCore/CMakeLists.txt`
- Build WAJIB diverifikasi ON dan OFF

---

## Integration Points Map — Semua File yang Perlu Disentuh

Berikut adalah **complete map** semua file yang perlu diubah saat menambah core baru.
Gunakan sebagai checklist.

### Wajib Diubah (core files)

| # | File | Perubahan |
|---|------|-----------|
| 1 | `EmuCore/EmuCore.h` | Tambah enum `Type::<CoreName>` |
| 2 | `EmuCore/EmuCore.cpp` | Tambah di `DetectType()` + `Create()` |
| 3 | `EmuCore/<CoreName>Core.h` | **BARU** — class declaration |
| 4 | `EmuCore/<CoreName>Core.cpp` | **BARU** — implementation |
| 5 | `EmuCore/CMakeLists.txt` | Tambah source file |
| 6 | `Core/Config.h` | Tambah struct config + extern vars |
| 7 | `Core/Config.cpp` | Tambah load/save section config |
| 8 | `UI/NativeApp.cpp` | Register RecentFilesRegistry + Init |
| 9 | `EmuCore/Config.h` | Tambah default touch buttons di `InitDefaultTouchConfigs()` |
| 10 | `EmuCore/Config.cpp` | Tambah `FillDefault<CoreName>TouchLayout()` |

### Wajib Diubah (UI integration)

| # | File | Perubahan |
|---|------|-----------|
| 9 | `UI/EmuScreen.h` | Tambah helper methods + Is<CoreName>() |
| 10 | `UI/EmuScreen.cpp` | Init, shutdown, update, render routing |
| 11 | `UI/EmuScreen.cpp` | Input handling + save/load routing |
| 12 | `UI/MainScreen.cpp` | Recent tab grouping (auto via registry) |

### Opsional

| # | File | Kapan |
|---|------|-------|
| 13 | `UI/TouchLayout<CoreName>.h/.cpp` | **BARU** — jika butuh touch controls |
| 14 | `UI/<CoreName>SettingsScreen.h/.cpp` | **BARU** — jika butuh settings screen |
| 15 | `UI/PauseScreen.cpp` | Jika save/load beda dari default |
| 16 | `Core/KeyMap.h/.cpp` | Jika ada VIRTKEY khusus |
| 17 | `Util/RecentFiles.h/.cpp` | Jika butuh recent list terpisah |
| 18 | `ext/lib<corename>/` | Submodule library emulator |
| 19 | `test_<corename>_core.cpp` | **BARU** — minimal test |
| 20 | `CMakeLists.txt` (root) | Link library + target |

### Android-Specific

| # | File | Kapan |
|---|------|-------|
| 21 | `android/jni/Android.mk` | Tambah source (jika pakai ndk-build) |
| 22 | `android/jni/AndroidAudio.cpp` | Jika audio routing berbeda |
| 23 | `AndroidManifest.xml` | Intent filter untuk file extension |
| 24 | `PpssppActivity.java` | Handle open file intent |

---

## Common Patterns — Code Examples

### Factory Pattern (EmuCore.cpp)

```cpp
std::unique_ptr<Core> Create(const Path &romPath) {
    Type type = DetectType(romPath);

    switch (type) {
#ifdef PPSSPP_MULTICORE
    case Type::GBA:
        return std::make_unique<GBACore>();
#endif
#ifdef PPSSPP_<CORENAME>
    case Type::<CoreName>:
        return std::make_unique<<CoreName>Core>();
#endif
    case Type::PSP:
    default:
        return std::make_unique<PSPCore>();
    }
}
```

### Audio Output Pattern (EmuScreen.cpp)

```cpp
// [PPSSPP-FORK] <CoreName>: audio push
if (Is<CoreName>()) {
    size_t stereoPairs = TARGET_PAIRS;
    int32_t audioBuf[TARGET_PAIRS * 2];
    static_cast<<CoreName>Core*>(activeCore_.get())->GetMixedAudio(audioBuf, &stereoPairs);
    System_AudioPushSamples(audioBuf, stereoPairs, 1.0f);
    return;
}
```

### Save State Pattern (PauseScreen.cpp)

```cpp
// [PPSSPP-FORK] <CoreName>: file-based save
if (Is<CoreName>()) {
    static_cast<<CoreName>Core*>(g_GBACore)->SaveStateToFile(slot);
    return;
}
```

### Touch Layout Pattern

```cpp
// [PPSSPP-FORK] <CoreName>: touch layout
if (Is<CoreName>()) {
    CreateTouchButtons<CoreName>(root);
    return;
}
```

---

## Verification Checklist

Sebelum merge, WAJIB verifikasi:

- [ ] `-DPPSSPP_<CORENAME>=ON` build sukses
- [ ] `-DPPSSPP_<CORENAME>=OFF` build sukses (tidak ada kode bocor)
- [ ] Semua file kustom punya `[PPSSPP-FORK] <CoreName>: ` marker
- [ ] Config terisolasi di section sendiri (`[<CoreName>]`)
- [ ] Recent files di section sendiri (`[<CoreName> Recent]`)
- [ ] VIRTKEY prefix berbeda dari core lain
- [ ] `EmuCore/EmuCore.h` — enum `Type::<CoreName>` terdaftar
- [ ] `EmuCore/EmuCore.cpp` — DetectType + Factory routing benar
- [ ] `EmuScreen::Is<CoreName>()` — constexpr fallback untuk OFF build
- [ ] DeviceLost + DeviceRestored diimplementasi (jika pakai GPU)
- [ ] Lazy init GPU resources (di Render() pertama, bukan constructor)
- [ ] Save state path tidak konflik dengan core lain
