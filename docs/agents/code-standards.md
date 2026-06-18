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
- Class/fungsi baru: gunakan prefix `LANSync`, `MDNS`, `TLS` sesuai fitur.
- Jangan gunakan nama yang sama dengan class upstream untuk menghindari ambiguity.

## Build Validation
- Selalu test build setelah perubahan:
  - Linux/Mac: `./b.sh --debug`
  - Semua file baru harus masuk CMakeLists.txt atau build system yang sesuai.
  - Jangan sampai ada warning baru di kode upstream.

## Commit Messages
- Format: `[feature/<nama>] <pesan>`
  - Contoh: `[lansync] Fix peer discovery timeout`
- Untuk merge upstream: `chore: merge upstream/master`
