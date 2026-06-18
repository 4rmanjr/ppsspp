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

## Isolasi File

- Semua fitur kustom harus berada di **file terpisah**.
- Minimalisir sentuhan ke file upstream. Jika harus menyentuh:
  - Hanya **tambah** kode (baris baru), jangan hapus/ubah baris existing.
  - Gunakan `#ifdef PPSSPP_<FITUR>` (atau flag sesuai fitur) untuk membungkus tambahan.
  - Tambahkan komentar `// [PPSSPP-FORK] NamaFitur: penjelasan` pada setiap blok tambahan agar mudah dilacak.

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

Kode upstream boleh menggunakan `#ifdef PPSSPP_*` untuk blok tambahan, tapi tidak boleh ada `#else` atau `#endif` yang mengubah alur kode upstream.
