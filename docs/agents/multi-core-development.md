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

## Aturan Ketat

### 1. Setiap core baru WAJIB mengikuti EmuCore interface

Semua emulator core WAJIB mengimplementasi interface `EmuCore`:

```cpp
class EmuCore {
public:
    virtual ~EmuCore() = default;
    virtual Type GetType() const = 0;
    virtual bool LoadROM(const Path &path) = 0;
    virtual void RunFrame() = 0;
    virtual void Render() = 0;
    virtual void SetKeys(uint32_t keys) = 0;
    // ... audio, savestate, dll
};
```

**DILARANG** hardcode core baru di luar pattern ini.

### 2. Dilarang menyentuh Core/, GPU/, HLE/, MIPS/

Kode kustom emulator **DILARANG** ditempatkan di direktori inti emulator.

| ✅ Boleh | ❌ Tidak Boleh |
|----------|----------------|
| `EmuCore/GBACore.cpp` | `Core/GBA.cpp` |
| `UI/TouchLayoutGBA.cpp` | `GPU/GBARender.cpp` |
| `ext/libmgba/` | `HLE/sceGBA.cpp` |

### 3. File extension → Core mapping

| Extension | Core |
|-----------|------|
| `.iso`, `.cso`, `.chd`, `.pbp`, `.elf` | PSP (existing) |
| `.gba` | GBA |
| `.gb`, `.gbc` | GB/GBC (via mGBA) |

Mapping didaftarkan di `EmuCore::DetectType()`.
Jika extension tidak dikenal → fallback ke PSP (existing behavior).

### 4. Settings auto-switch

Saat user memilih game:

```
PSP game (.iso)  → Config section [PSP]    (existing)
GBA game (.gba)  → Config section [GBA]    (baru)
```

Wajib:
- Setiap core punya **section config sendiri** — jangan satukan dengan PSP config.
- Touch layout berganti otomatis sesuai core active.
- Display settings (filter, aspect ratio) berganti otomatis.

### 5. Dual build verification

Setiap perubahan WAJIB di-test dalam 2 kondisi:

```bash
# Multi-core ON (GBA aktif)
cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc)

# Multi-core OFF (PSP only, seperti PPSSPP asli)
cmake -PPSSPP_MULTICORE=OFF .. && make -j$(nproc)
```

Keduanya harus build sukses. Jika OFF gagal, berarti ada kode kustom yang bocor ke upstream — **fix segera**.

### 6. Touch layout per core

- GBA touch layout: A, B, D-Pad, L, R, Start, Select
- File di `UI/TouchLayoutGBA.h/.cpp`
- EmuScreen memilih layout berdasarkan `coreType_` aktif

### 7. External library integration

Library emulator (mGBA, dll) WAJIB:
- Ditempatkan di `ext/` sebagai git submodule
- Dibuild sebagai static library (`LIBMGBA_ONLY`)
- Tidak boleh mengubah build system upstream di luar blok `#ifdef PPSSPP_MULTICORE`
