# PPSSPP Fork — Agent Instructions

Ini adalah fork PPSSPP dengan fitur kustom (LAN sync, dll) yang **harus tetap kompatibel** dengan upstream official [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp).

## Prioritas Utama

1. **Zero breaking change** — Tidak boleh menghapus, mengubah perilaku, atau merusak kode PPSSPP asli.
2. **Upstream compatibility** — Semua perubahan harus bisa di-merge dengan upstream tanpa konflik permanen.
3. **Modular** — Fitur kustom harus di file terpisah, tidak menyatu dengan codebase utama.
4. **Preserve existing behavior** — Jangan ubah cara kerja emulator inti.

## File Guidelines

Semua aturan detail ada di `docs/agents/`:

| File | Isi |
|------|-----|
| [fork-maintenance.md](docs/agents/fork-maintenance.md) | Strategi merge upstream, cara handle konflik |
| [lansync-development.md](docs/agents/lansync-development.md) | Aturan khusus LAN sync & fitur kustom |
| [multi-core-development.md](docs/agents/multi-core-development.md) | Aturan khusus multi-emulator (GBA, future cores) |
| [code-standards.md](docs/agents/code-standards.md) | Standar kode C++, platform, dsb |
| [feature-template.md](docs/agents/feature-template.md) | Panduan menambah fitur baru |

## Aturan Global

### 🔴 HARAM (Tidak Boleh Dilakukan)
- ❌ Menghapus, mengubah, atau merestruktur kode upstream yang sudah ada.
- ❌ Meletakkan file kustom di direktori inti emulator (`Core/`, `GPU/`, `HLE/`, `MIPS/`).
- ❌ Mengubah alur logika kode upstream via `#else` atau `#endif` di luar blok kustom.
- ❌ Refactor kode upstream untuk mengakomodasi kode kustom.

### 🟢 WAJIB (Harus Dilakukan)
- ✅ Kode kustom WAJIB di **file terpisah** di direktori non-inti.
- ✅ Setiap tambahan ke file upstream WAJIB:
   1. Dibungkus `#ifdef PPSSPP_<FITUR>` (flag spesifik fitur)
   2. Ditandai komentar `// [PPSSPP-FORK] NamaFitur: deskripsi`
   3. Hanya **menambah** baris baru (zero deletion)
- ✅ Build WAJIB diverifikasi dalam **DUA kondisi**:
   1. `-DPPSSPP_<FITUR>=ON` — fitur aktif, build sukses
   2. `-DPPSSPP_<FITUR>=OFF` — fitur nonaktif, build tetap sukses
- ✅ Saat conflict merge dengan upstream: **kode upstream yang menang** — kode kustom dipindah/diadjust, bukan sebaliknya.
- ✅ Setiap fitur baru WAJIB punya feature flag sendiri (`PPSSPP_<NAMA>`).
- ✅ Semua pengaturan per-fitur WAJIB terisolasi di section config terpisah, jangan campur dengan config PSP.
