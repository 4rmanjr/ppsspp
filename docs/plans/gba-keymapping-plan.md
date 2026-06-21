# Plan: GBA Key Mapping Terpisah (Zero Breaking Change)

## Tujuan
Mapping keyboard/controller untuk GBA tidak mengganggu mapping PSP,
dan tersimpan secara permanen di `ppsspp.ini` section `[GBA]`.

## Constraints
- ❌ Tidak boleh hapus/ubah `g_controllerMap` — PSP mapping harus utuh
- ❌ Tidak boleh ubah `ControlMapper` logic upstream
- ✅ Mapping GBA disimpan di `[GBA]` section
- ✅ Paling sederhana dan aman

---

## Pendekatan: Override Map + `PSPSKeysToGBA()` Extended

### Arsitektur

```
                            ┌────────────────────┐
Fisik Keyboard ────────────▶│  ControlMapper     │
   (Z, X, A, S, dll)       │  (PSP button space)│
                            └────────┬───────────┘
                                     ▼
                            ┌────────────────────┐
                            │  PSP button yang    │
                            │  dihasilkan:        │
                            │  Cross, Circle, dll │
                            └────────┬───────────┘
                                     ▼
                            ┌────────────────────┐
                            │  PSPSKeysToGBA()   │
                            │  di SetKeys()      │
                            └────────┬───────────┘
                                     ▼
                            ┌────────────────────┐
                            │  GBA button bitmask │
                            └────────────────────┘
```

### Masalah
- PSP button Cross → GBA A (OK)
- Tapi kalau user mau mapping Tombol `B` di keyboard → GBA B, 
  dia harus mengganti mapping PSP Circle yang ORANG lain
  mungkin ingin tetap Circle untuk PSP

### Solusi: GBA VIRTKEYs Baru (3 baris kode)

Buat VIRTKEY khusus GBA yang dipetakan langsung ke GBA buttons:

```cpp
// Di KeyMap.cpp — daftar VIRTKEY baru
{VIRTKEY_GBA_A, "GBA A"},
{VIRTKEY_GBA_B, "GBA B"},
{VIRTKEY_GBA_START, "GBA Start"},
{VIRTKEY_GBA_SELECT, "GBA Select"},
{VIRTKEY_GBA_L, "GBA L"},
{VIRTKEY_GBA_R, "GBA R"},
{VIRTKEY_GBA_UP, "GBA Up"},
{VIRTKEY_GBA_DOWN, "GBA Down"},
{VIRTKEY_GBA_LEFT, "GBA Left"},
{VIRTKEY_GBA_RIGHT, "GBA Right"},
```

Lalu di `PSPSKeysToGBA()`, tambah mapping VIRTKEY → GBA bit:

```cpp
// Di GBACore.cpp
if (pspKeys >= VIRTKEY_GBA_FIRST) {
    // Direct GBA VIRTKEY mapping — no PSP involvement
    return VirtKeyToGBABit(pspKeys);
}
```

### Cara Kerja

1. **Default**: VIRTKEY_GBA_A tidak punya mapping keyboard → user set manual
2. **User set**: `B` keyboard → VIRTKEY_GBA_B
3. **Saat GBA mode**: ControlMapper kirim VIRTKEY_GBA_B → PSPSKeysToGBA lihat ini GBA VIRTKEY → langsung return bit GBA_B

### Keuntungan
- ✅ **Zero change** ke `g_controllerMap` / `ControlMapper` upstream
- ✅ Mapping PSP tetap utuh (tidak diganggu)
- ✅ Mapping GBA bisa disimpan di `[GBA]` section via INI
- ✅ Bisa set default GBA mapping sendiri tanpa ganggu PSP

### Yang Diubah

| File | Perubahan |
|------|-----------|
| `Core/KeyMap.cpp` | +10 baris: daftar VIRTKEY_GBA_* |
| `EmuCore/GBACore.h` | +1: konstanta GBA VIRTKEY range |
| `EmuCore/GBACore.cpp` | +20 baris: handle GBA VIRTKEY di `PSPSKeysToGBA()` |
| `EmuCore/Config.cpp` | +10 baris: load/save GBA mapping di `[GBA]` |
| `UI/EmuScreen.cpp` | +3 baris: aktifkan mapping GBA saat mode GBA |

### Risk
- Sangat rendah — VIRTKEY baru di luar range PSP buttons
- Default mapping kosong → user harus set manual pertama kali
- Kompatibel dengan implementasi save state dan config existing

### Platform Compatibility
- ✅ **Linux SDL** — keyboard, gamepad via ControlMapper
- ✅ **Android** — touch layout + hardware keyboard via ControlMapper
  (VIRTKEY system sudah cross-platform, tidak ada platform-specific code)
- ✅ Mapping disimpan di INI file → bisa dibaca semua platform
- VIRTKEY baru akan muncul otomatis di Control Mapping Screen tanpa perubahan UI

---

## Alternatif (Lebih Kompleks, Tidak Direkomendasikan)

### A. Modifikasi ControlMapper
- Tambah `coreType_` ke ControlMapper
- Switch `g_controllerMap` ↔ `g_gbaControllerMap` based on mode
- **Risk:** Banyak kode upstream berubah → conflict saat merge

### B. Dual Mapping Layer
- Simpan GBA mapping di map terpisah
- ControlMapper cek dua map
- **Risk:** Logic ControlMapper jadi kompleks, perf turun

---

## Kesimpulan
Gunakan VIRTKEY baru (solusi paling sederhana dan aman).
Hanya tambah kode baru, tidak ubah kode upstream.
