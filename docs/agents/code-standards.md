# Code Standards — C++ & Platform Guidelines

## Bahasa & Standar
- C++17 (ikuti standar yang sudah dipakai upstream PPSSPP).
- Ikuti code style file di sekitarnya — jangan reformat atau ubah indentasi kode upstream.

## File Header
Setiap file kustom baru WAJIB punya header:

```cpp
// [PPSSPP-FORK] <NamaFitur>
// <Deskripsi singkat>
// Jangan hapus, jangan ubah kode upstream.
```

File yang merupakan **tambahan ke file upstream** (1-5 baris hook) WAJIB punya:
```cpp
// [PPSSPP-FORK] <NamaFitur>: <penjelasan hook>
// Hanya tambahkan baris baru. Jangan hapus/mengubah baris upstream.
#ifdef PPSSPP_<FITUR>
// ... kode hook minimal ...
#endif
```

## Platform Handling
- Platform-specific code di direktori masing-masing (`SDL/`, `Windows/`, `macOS/`, `android/`).
- `Common/` hanya untuk kode cross-platform. Jika ada platform-specific block, gunakan `#ifdef`:

```cpp
// [PPSSPP-FORK] NamaFitur
#ifdef _WIN32
// kode Windows
#elif defined(__APPLE__)
// kode macOS
#else
// kode Linux/SDL
#endif
```

## Naming Convention
- Class/fungsi baru: gunakan prefix `LANSync`, `MDNS`, `TLS`, `GBA`, `EmuCore` sesuai fitur.
- Jangan gunakan nama yang sama dengan class upstream untuk menghindari ambiguity.
- Untuk multi-emulator: semua class inti di namespace `EmuCore` (contoh: `EmuCore::GBACore`, `EmuCore::PSPCore`).

## Build Validation — WAJIB Dual Verification

Setiap perubahan WAJIB di-verifikasi dalam **DUA kondisi**:

### 1. Dengan fitur ON
```bash
cmake -DPPSSPP_<FITUR>=ON .. && make -j$(nproc)
```
### 2. Dengan fitur OFF
```bash
cmake -DPPSSPP_<FITUR>=OFF .. && make -j$(nproc)
```

**Aturan:**
- Keduanya harus build sukses tanpa error.
- Tidak boleh ada warning baru di kode upstream di kedua kondisi.
- Semua file baru harus masuk CMakeLists.txt atau build system yang sesuai.
- Default: fitur **ON** untuk development, **OFF** harus tetap build.

## Commit Messages
- Format: `[feature/<nama>] <pesan>`
  - Contoh: `[lansync] Fix peer discovery timeout`
- Untuk merge upstream: `chore: merge upstream/master`
