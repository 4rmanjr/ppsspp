# Feature Template — Menambahkan Fitur Baru

Gunakan checklist ini setiap kali menambahkan fitur kustom baru.

## Checklist Wajib

- [ ] Fitur baru TIDAK mengubah/menghapus kode upstream yang sudah ada
- [ ] Fitur baru diimplementasikan di **file terpisah**, bukan di file inti PPSSPP
- [ ] Jika harus menyentuh file upstream:
  - Hanya **menambah** baris baru (jangan mengubah/menghapus baris existing)
  - Kode tambahan dibungkus `#ifdef PPSSPP_<NAMA_FITUR>`
  - Tambahkan komentar `// [PPSSPP-FORK] <NamaFitur>: <penjelasan>`
- [ ] Fitur baru punya **feature flag** sendiri (`#define PPSSPP_<NAMA_FITUR>`)
- [ ] Semua file baru punya header `[PPSSPP-FORK] <NamaFitur>`
- [ ] Build berhasil (`./b.sh --debug`) tanpa error/warning baru
- [ ] Fitur bisa dinonaktifkan dengan menghapus flag `#define` — dan build tetap berhasil

## Langkah Implementasi

1. Tentukan nama fitur dan flag preprocessor (`PPSSPP_<NAMA>`)
2. Buat file-file baru di direktori yang sesuai
3. Update CMakeLists.txt / build system
4. Update `docs/agents/fork-maintenance.md` — daftarkan flag baru di tabel
5. Update `docs/agents/lansync-development.md` — daftarkan direktori baru
6. Test build
