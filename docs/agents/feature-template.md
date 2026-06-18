# Feature Template — Menambahkan Fitur Baru

Gunakan checklist ini setiap kali menambahkan fitur kustom baru.

## 🔴 Aturan Wajib (Pelanggaran = Rollback)

1. ❌ **Dilarang** menghapus, mengubah, atau merestruktur kode upstream
2. ❌ **Dilarang** meletakkan file kustom di `Core/`, `GPU/`, `HLE/`, `MIPS/`
3. ❌ **Dilarang** refactor upstream untuk akomodasi kode kustom
4. ❌ **Dilarang** mengubah alur upstream via `#else`/`#endif`

## Checklist Wajib

### Isolasi
- [ ] Fitur baru TIDAK mengubah/menghapus kode upstream yang sudah ada
- [ ] Fitur baru diimplementasikan di **file terpisah**, bukan di file inti PPSSPP
- [ ] File baru diletakkan di direktori non-inti (contoh: `EmuCore/`, `ext/`, `UI/`, `Common/Net/`, dll)
- [ ] Semua file baru punya header `// [PPSSPP-FORK] <NamaFitur>`

### Feature Flag
- [ ] Fitur baru punya **feature flag** sendiri (`PPSSPP_<NAMA>`)
- [ ] Fitur bisa dinonaktifkan dengan `cmake -DPPSSPP_<NAMA>=OFF` — dan build tetap berhasil ✅

### Jika Terpaksa Menyentuh File Upstream (MAX 5 baris)
- [ ] Hanya **menambah** baris baru (jangan mengubah/menghapus baris existing)
- [ ] Kode tambahan dibungkus `#ifdef PPSSPP_<NAMA>`
- [ ] WAJIB ada komentar `// [PPSSPP-FORK] <NamaFitur>: <penjelasan>`
- [ ] Tidak ada `#else` yang mengubah alur upstream

### Build Verification (WAJIB Dual)
- [ ] Build dengan `-DPPSSPP_<NAMA>=ON` — sukses tanpa error/warning
- [ ] Build dengan `-DPPSSPP_<NAMA>=OFF` — sukses tanpa error/warning
- [ ] Semua file baru sudah masuk CMakeLists.txt / build system

### Config & Settings
- [ ] Pengaturan fitur terisolasi di section config terpisah (jangan campur dengan PSP config)
- [ ] Default pengaturan aman untuk semua platform

### Dokumentasi
- [ ] Update `docs/agents/fork-maintenance.md` — daftarkan flag baru di tabel
- [ ] Update `docs/agents/` — tambah/buat file aturan khusus fitur jika perlu
- [ ] Update `AGENTS.md` — daftarkan file baru di tabel guidelines
- [ ] Update design doc di `docs/superpowers/specs/` jika ada perubahan arsitektur

## Langkah Implementasi

1. Tentukan nama fitur dan flag preprocessor (`PPSSPP_<NAMA>`)
2. Buat file-file baru di direktori yang sesuai
3. Update CMakeLists.txt / build system
4. Update `docs/agents/fork-maintenance.md` — daftarkan flag baru di tabel
5. Tambah/buat file aturan khusus fitur di `docs/agents/`
6. Update `AGENTS.md` — daftarkan file baru
7. Test build dual verification (ON + OFF)
8. Commit dengan pesan: `[feature/<nama>] <deskripsi>`
