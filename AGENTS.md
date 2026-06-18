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
| [code-standards.md](docs/agents/code-standards.md) | Standar kode C++, platform, dsb |
| [feature-template.md](docs/agents/feature-template.md) | Panduan menambah fitur baru |

## Aturan Global

- Boleh **menambah** kode ke file upstream, tapi **tidak boleh menghapus atau mengubah** kode asli.
- Semua kode kustom harus dibungkus `#ifdef PPSSPP_CUSTOM_FEATURES` (atau flag spesifik fitur).
- File baru diletakkan di direktori sesuai fungsinya (misal `SDL/LANSync.cpp`, `Common/Net/MDNS.cpp`), bukan di `Core/`, `GPU/`, atau direktori inti emulator.
- Saat conflict merge dengan upstream, kode upstream yang menang — kode kustom dipindah/diadjust.
