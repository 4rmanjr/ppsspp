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

### Diagram Alur Audio (contoh GBA)

```
┌─────────────────────────────────────────────────────────────┐
│  mGBA (libmgba) — EMULASI GBA HARDWARE                      │
│  ┌──────────────────┐     ┌──────────────────┐              │
│  │ GBA Sound Chip    │     │ mAudioBuffer     │              │
│  │ (FIFO, DMA,       │────▶│ int16 stereo     │              │
│  │  Square/Wave/Noise)│     │ 32768 Hz (native)│              │
│  └──────────────────┘     └────────┬─────────┘              │
└───────────────────────────────────┼──────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────┐
│  GBACore (EmuCore) — KONVERSI FORMAT                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ GetMixedAudio()                                       │   │
│  │ 1. Resample: 32768 Hz → 44100 Hz (linear interp)     │   │
│  │ 2. Convert:  int16 → int32 (shift left 16)           │   │
│  │                                                       │   │
│  │ Catatan:                                               │   │
│  │ - TIDAK padding silence (sebab slow-mo)               │   │
│  │ - Method spesifik GBA, bukan di interface Core         │   │
│  └──────────────────────┬────────────────────────────────┘   │
└─────────────────────────┼────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  PPSSPP Audio Backend — KODE UPSTREAM, TIDAK DISENTUH       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ System_AudioPushSamples(int32_t*, stereoPairs, vol)   │   │
│  │                                                       │   │
│  │ Ini fungsi BAWAAN PPSSPP. Zero modifikasi.            │   │
│  │ EmuScreen hanya:                                      │   │
│  │   gba->GetMixedAudio(buf, &pairs);                    │   │
│  │   System_AudioPushSamples(buf, pairs, 1.0f);          │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Diagram Alur Video (contoh GBA)

```
┌─────────────────────────────────────────────────────────────┐
│  mGBA (libmgba)                                             │
│  ┌──────────────────┐     ┌──────────────────┐              │
│  │ GBA LCD/PPU       │     │ rawVideoBuffer_   │              │
│  │ Emulation         │────▶│ mColor (XBGR8)    │              │
│  │ (240×160 px)      │     │ 240×160           │              │
│  └──────────────────┘     └────────┬─────────┘              │
└───────────────────────────────────┼──────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────┐
│  GBACore (EmuCore) — KONVERSI + RENDER                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ RunFrame():                                           │   │
│  │ 1. core_->runFrame() → mGBA renders ke rawVideoBuffer  │   │
│  │ 2. XBGR8 → RGBA8888 conversion                       │   │
│  │                                                       │   │
│  │ Render(DrawContext*):                                  │   │
│  │ 1. Lazy init pipeline + texture (first call)          │   │
│  │ 2. Upload RGBA8888 ke Thin3D texture                  │   │
│  │ 3. Draw fullscreen quad (3:2 aspect, letterbox)       │   │
│  └──────────────────────┬────────────────────────────────┘   │
└─────────────────────────┼────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  PPSSPP GPU Backend — KODE UPSTREAM, TIDAK DISENTUH         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Thin3D (OpenGL/Vulkan)                               │   │
│  │                                                       │   │
│  │ EmuScreen hanya:                                      │   │
│  │   activeCore_->Render(draw);       // GBA render      │   │
│  │   renderUI();                       // touch + FPS     │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Aturan Ketat

### 1. Setiap core baru WAJIB mengikuti EmuCore interface

Semua emulator core WAJIB mengimplementasi interface `EmuCore`:

```cpp
namespace Draw { class DrawContext; }

class Core {
public:
    virtual ~Core() = default;
    virtual Type GetType() const = 0;

    // Lifecycle
    virtual bool LoadROM(const Path &path) = 0;
    virtual void RunFrame() = 0;
    virtual void Reset() = 0;

    // Rendering — DrawContext disediakan oleh EmuScreen
    virtual void Render(Draw::DrawContext *draw) = 0;

    // Device lifecycle — release/recreate GPU resources
    virtual void DeviceLost() {}
    virtual void DeviceRestored(Draw::DrawContext *draw) {}

    // Audio
    virtual int GetAudioSampleRate() const = 0;
    virtual void GetAudioSamples(int16_t *buffer, size_t *samples) = 0;

    // Input
    virtual void SetKeys(uint32_t keys) = 0;
    virtual uint32_t GetKeys() const = 0;

    // Savestate
    virtual size_t GetStateSize() const = 0;
    virtual bool SaveState(void *buffer) = 0;
    virtual bool LoadState(const void *buffer) = 0;

    // Game info
    virtual void GetGameInfo(std::string &title, std::string &id) const = 0;
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

### 8. GPU Resource Lifecycle

Setiap core yang punya GPU resources WAJIB:
- Implement `DeviceLost()` + `DeviceRestored()` — release/recreate resources.
- **Lazy init**: Resources dibuat di `Render()` pertama, BUKAN di constructor.
- `DeviceRestored()` tidak perlu recreate langsung — cukup set flag, recreate otomatis di `Render()` berikutnya.
- Contoh: GBACore membuat pipeline, texture, sampler state di `Render()` pertama.

### 9. Audio Integration — Resample Rule

PPSSPP mixer berjalan di **44100 Hz**. Jika native sample rate core berbeda, WAJIB resample:

```cpp
// GBA: mGBA native = 32768 Hz, PPSSPP mixer = 44100 Hz
static constexpr int GBA_NATIVE_RATE = 32768;
static constexpr int TARGET_RATE = 44100;
```

Aturan:
- ✅ Tiap frame: nativePairs = nativeRate / fps, targetPairs = targetRate / fps.
- ✅ Resample via **linear interpolation** nativePairs → targetPairs.
- ✅ int16 → int32 (shift left 16) setelah resample.
- ❌ **Jangan padding silence** — itu bikin audio slow-mo/distorted.
- ✅ Method spesifik core (bukan interface): `GetMixedAudio(int32_t*, size_t*)`.

### 10. Save State Path Convention

Semua save state kustom WAJIB mengikuti:

| Aturan | Contoh GBA |
|--------|------------|
| Extension | `.gbast` |
| Directory | `DIRECTORY_SAVESTATE / GBA /` |
| Filename | `<sanitized_title>_<slot>.gbast` |
| Sanitasi | alphanumeric + underscore, max 32 chars |
| Dedup | tambah game code prefix jika tersedia |

Implementasi:
- Gunakan `Core::SaveState(void*)` + `File::WriteDataToFile()`.
- Gunakan `Core::LoadState(const void*)` + `File::ReadBinaryFileToString()`.
- Return `bool` — caller (EmuScreen) handle OSD messages.
- Logging via `Common/Log.h` dengan prefix `[GBA]`.
