# PPSSPP Fork — Agent Instructions

Ini adalah fork PPSSPP dengan fitur kustom yang **harus tetap kompatibel** dengan upstream official [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp).

## Prioritas Utama

1. **Zero breaking change** — Tidak menghapus, mengubah, atau merusak kode PPSSPP asli.
2. **Upstream compatibility** — Semua perubahan harus bisa di-merge dengan upstream tanpa konflik permanen.
3. **Modular** — Fitur kustom di file terpisah, tidak menyatu dengan codebase utama.
4. **Preserve existing behavior** — Jangan ubah cara kerja emulator inti.

## File Guidelines

| File | Isi |
|------|-----|
| **`AGENTS.md`** (ini) | Prioritas + HARAM/WAJIB + navigasi ke aturan detail |
| [`docs/agents/quality-gates.md`](docs/agents/quality-gates.md) | **3 Quality Gates**: PSP Feature Parity, Code Review, PSP Parity untuk core baru |
| [`docs/agents/extensibility.md`](docs/agents/extensibility.md) | **Extensibility Architecture**: CoreButtonRegistry, generic classes, step-by-step tambah core baru |
| [`docs/agents/psp-knowledge-base.md`](docs/agents/psp-knowledge-base.md) | **Katalog mismatch** PSP-GBA yang sudah di-fix |
| [`docs/agents/code-standards.md`](docs/agents/code-standards.md) | Standar kode C++, naming, platform handling |
| [`docs/agents/multi-core-development.md`](docs/agents/multi-core-development.md) | Aturan multi-emulator (GBA, future cores) |
| [`docs/agents/feature-template.md`](docs/agents/feature-template.md) | Panduan menambah fitur baru |
| [`docs/agents/fork-maintenance.md`](docs/agents/fork-maintenance.md) | Strategi merge upstream, handle konflik |
| [`docs/agents/lansync-development.md`](docs/agents/lansync-development.md) | Aturan khusus LAN sync |
| [`docs/progress-gba-support.md`](docs/progress-gba-support.md) | Progress & status fitur GBA |

**WAJIB baca** `quality-gates.md` sebelum commit dan `extensibility.md` sebelum menambah core baru.

## Aturan Global

### 🔴 HARAM
- ❌ Menghapus, mengubah, atau merestruktur kode upstream yang sudah ada.
- ❌ Meletakkan file kustom di direktori inti (`Core/`, `GPU/`, `HLE/`, `MIPS/`).
- ❌ Mengubah alur logika upstream via `#else`/`#endif` di luar blok kustom.
- ❌ Refactor kode upstream untuk mengakomodasi kode kustom.

### 🟢 WAJIB
- ✅ Kode kustom di **file terpisah** di direktori non-inti.
- ✅ Setiap tambahan ke file upstream:
  1. Dibungkus `#ifdef PPSSPP_<FITUR>` (flag spesifik fitur)
  2. Ditandai `// [PPSSPP-FORK] NamaFitur: deskripsi`
  3. Hanya **menambah** baris baru (zero deletion)
- ✅ Build diverifikasi **DUA kondisi**: `-DPPSSPP_<FITUR>=ON` dan `=OFF` — keduanya sukses.
- ✅ Setiap fitur baru punya feature flag sendiri (`PPSSPP_<NAMA>`).
- ✅ Pengaturan per-fitur terisolasi di section config terpisah (jangan campur dengan PSP).

### 🟢 Build Flag Convention

| Flag | Scope | Note |
|------|-------|------|
| `PPSSPP_MULTICORE` | Top-level — enable semua multi-core | Wajib ON untuk GBA |
| `PPSSPP_GBA` | (future) | Saat ini masih pakai `PPSSPP_MULTICORE` |

- Semua file kustom di `UI/` yang pakai `#ifdef PPSSPP_MULTICORE`: WAJIB ada `// [PPSSPP-FORK]` marker + zero deletion + verifikasi `#ifndef` (PSP path) masih build.
- Jika suatu fitur perlu dipisah dari multi-core umbrella, buat flag baru.

## Patterns

### 🟢 IsFitur() Pattern — Kurangi #ifdef di Call Site

```cpp
// Header — di luar #ifdef
#ifdef PPSSPP_FITUR
    bool IsFitur() const { return flag_ != Default; }
#else
    static constexpr bool IsFitur() { return false; }
#endif

// Call site — tanpa #ifdef (compiler buang dead branch)
if (IsFitur()) { DoFiturStuff(); }
```

### 🟢 Helper Method Extraction

Jika logic kustom >5 baris di file upstream, extract ke helper:

```cpp
// Di file upstream (call site minimal, ≤3 baris)
#ifdef PPSSPP_FITUR
    if (IsFitur()) { UpdateFitur(); return; }
#endif

// Helper di file terpisah
void EmuScreen::UpdateFitur() { /* semua logic */ }
```

Convention: `Init<Fitur>()`, `Shutdown<Fitur>()`, `Update<Fitur>()`, `Render<Fitur>()`.

## Navigation — Kapan Baca File Lain

| Situasi | Baca |
|---------|------|
| Mau commit | [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #2 Code Review |
| Implementasi fitur baru (GBA) | [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #1 Feature Parity |
| Menambah emulator baru (N64, PS1, dll) | [`extensibility.md`](docs/agents/extensibility.md) + [`quality-gates.md`](docs/agents/quality-gates.md) — Gate #3 |
| Tidak yakin constructor/class/ImageID | [`quality-gates.md`](docs/agents/quality-gates.md) — Anti-Hallucination Rule |
| Melihat mismatch PSP yang sudah di-fix | [`psp-knowledge-base.md`](docs/agents/psp-knowledge-base.md) |
| Merge upstream | [`fork-maintenance.md`](docs/agents/fork-maintenance.md) |
