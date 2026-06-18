# Fork Maintenance — Upstream Merge Strategy

## Merge Strategy

Gunakan `git merge` (bukan rebase) untuk mengambil perubahan dari upstream.

```bash
git fetch upstream
git merge upstream/master
```

## Saat Terjadi Conflict

1. **Kode upstream SELALU menang.** Jangan pernah mengubah kode asli PPSSPP untuk mengakomodasi kode kustom.
2. Kode kustom yang conflict dipindahkan atau disesuaikan, bukan kode upstream.
3. Jika conflict terjadi di area yang sulit, file kustom bisa dinonaktifkan sementara dengan `#ifdef` dan diff disimpan sebagai patch terpisah.

## Isolasi File — Aturan Ketat

- Semua fitur kustom WAJIB berada di **file terpisah** di direktori non-inti.
- File kustom **DILARANG** ditaruh di `Core/`, `GPU/`, `HLE/`, `MIPS/`, atau direktori inti emulator lainnya.

### Jika Terpaksa Menyentuh File Upstream

Ini hanya diperbolehkan untuk **hook minimal** (1-5 baris). Jika lebih dari itu, desain ulang pendekatannya.

**WAJIB:**
1. Hanya **tambah** baris baru — **jangan hapus, ubah, atau pindahkan** baris existing.
2. Kode tambahan WAJIB dibungkus `#ifdef PPSSPP_<FITUR>`.
3. WAJIB ada komentar `// [PPSSPP-FORK] NamaFitur: deskripsi` di setiap blok.
4. Blok kustom WAJIB berada di **lokasi yang jelas** (dekat dengan kode yang relevan), bukan terselip di tengah logika.

**Build verification setelah touching upstream:**
- `cmake -DPPSSPP_<FITUR>=ON .. && make -j$(nproc)` ✅
- `cmake -DPPSSPP_<FITUR>=OFF .. && make -j$(nproc)` ✅

## Update Periodik

- Merge upstream secara berkala (setelah rilis upstream / saat ada patch penting).
- Jangan biarkan fork tertinggal terlalu jauh dari upstream — semakin jauh, semakin besar risiko conflict.

## Feature Flag

Setiap fitur kustom punya flag preprocessor sendiri:

| Flag | Fitur |
|------|-------|
| `PPSSPP_LANSYNC` | LAN sync core |
| `PPSSPP_MDNS` | MDNS discovery |
| `PPSSPP_TLS` | TLS server/komunikasi aman |
| `PPSSPP_UDP_DISCOVERY` | UDP peer discovery |
| `PPSSPP_QR` | QR code pairing |
| `PPSSPP_PLATFORM_KEYSTORE` | Platform key store |
| `PPSSPP_MULTICORE` | Multi-emulator (GBA, future cores) |

Kode upstream boleh menggunakan `#ifdef PPSSPP_*` untuk blok tambahan, tapi tidak boleh ada `#else` atau `#endif` yang mengubah alur kode upstream.
