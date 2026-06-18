# LAN Sync Development — Aturan Khusus

## Cakupan Fitur

Fitur kustom yang ada saat ini:
- **LAN Sync Core** — sinkronisasi save state antar device via jaringan lokal
- **MDNS Discovery** — menemukan peer otomatis di jaringan lokal
- **TLS Server** — komunikasi terenkripsi antar peer
- **UDP Discovery** — alternatif discovery protocol
- **QR Code Pairing** — koneksi via scan QR
- **Platform Key Store** — penyimpanan credential aman per platform

## Arsitektur Modular

Semua fitur LAN sync tersebar di direktori non-inti:

| Direktori | Konten |
|-----------|--------|
| `Common/Net/` | MDNS, UDP Discovery, TLS, Key Store (cross-platform) |
| `Core/` | Config, SaveState LANSync (file baru, bukan modifikasi) |
| `SDL/` | Implementasi LAN sync untuk SDL/Linux |
| `Windows/` | Implementasi LAN sync untuk Windows |
| `macOS/` | Implementasi LAN sync untuk macOS |
| `UI/` | LAN Peer List Screen, Settings (file UI baru) |

## Aturan Tambahan

1. **Jangan sentuh `Core/SaveState.cpp` untuk logic baru.** Semua logic sync ada di `SaveStateLANSync.cpp`. File upstream hanya boleh kena hook minimal.
2. **Platform-specific code** dipisah per platform (`SDL/`, `Windows/`, `macOS/`), bukan dicampur di `Common/`.
3. **Setiap fitur baru** harus punya flag `#ifdef` sendiri (lihat `fork-maintenance.md`).
4. **Test** — setiap perubahan harus bisa di-build dengan `./b.sh --debug` tanpa error.
5. **End-to-end test** tersedia di `test_e2e_lansync.cpp` dan `test_e2e_full.cpp` — jalankan sebelum submit perubahan.
