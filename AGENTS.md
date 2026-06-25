# PPSSPP Fork — Agent Instructions

Ini adalah fork PPSSPP dengan fitur kustom (LAN sync, dll) yang **harus tetap kompatibel** dengan upstream official [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp).

## Prioritas Utama

1. **Zero breaking change** — Tidak boleh menghapus, mengubah perilaku, atau merusak kode PPSSPP asli.
2. **Upstream compatibility** — Semua perubahan harus bisa di-merge dengan upstream tanpa konflik permanen.
3. **Modular** — Fitur kustom harus di file terpisah, tidak menyatu dengan codebase utama.
4. **Preserve existing behavior** — Jangan ubah cara kerja emulator inti.
5. **PSP Feature Parity** — Fitur GBA (dan core lain) WAJIB punya behavior yang setara dengan PSP jika ada equivalent-nya.
6. **Zero Hallucination** — Semua nama class, function, enum, constructor signature, dan ImageID WAJIB diverifikasi ke codebase sebelum digunakan.

## File Guidelines

Semua aturan detail ada di `docs/agents/`:

| File | Isi |
|------|-----|
| [fork-maintenance.md](docs/agents/fork-maintenance.md) | Strategi merge upstream, cara handle konflik |
| [lansync-development.md](docs/agents/lansync-development.md) | Aturan khusus LAN sync & fitur kustom |
| [multi-core-development.md](docs/agents/multi-core-development.md) | Aturan khusus multi-emulator (GBA, future cores) |
| [code-standards.md](docs/agents/code-standards.md) | Standar kode C++, platform, dsb |
| [feature-template.md](docs/agents/feature-template.md) | Panduan menambah fitur baru |
| [psp-knowledge-base.md](docs/agents/psp-knowledge-base.md) | Katalog mismatch PSP-GBA yang sudah ditemukan & di-fix |
| [progress-gba-support.md](docs/progress-gba-support.md) | Progress & status fitur GBA support |

## Aturan Global

### 🔴 HARAM (Tidak Boleh Dilakukan)
- ❌ Menghapus, mengubah, atau merestruktur kode upstream yang sudah ada.
- ❌ Meletakkan file kustom di direktori inti emulator (`Core/`, `GPU/`, `HLE/`, `MIPS/`).
- ❌ Mengubah alur logika kode upstream via `#else` atau `#endif` di luar blok kustom.
- ❌ Refactor kode upstream untuk mengakomodasi kode kustom.
- ❌ Menebak-nebak (hallucinate) nama class, constructor, enum, atau ImageID — WAJIB cek ke file asli.

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
- ✅ **Verify against codebase** — Setiap penggunaan class/function/enum/ImageID yang belum dikenal, WAJIB di-`grep`/`read` dulu dari file asli untuk memastikan signature, parameter type, dan behavior-nya.

### 🟢 Feature Parity dengan PSP (Quality Gate #1)

Setiap fitur GBA (atau core lain) yang memiliki equivalent PSP WAJIB mengikuti aturan ini:

#### 1. Read PSP Reference (Wajib)
SEBELUM menulis kode GBA, baca implementasi PSP yang equivalent:
```bash
grep -n "<nama_fungsi/class>" UI/<psp_file>.cpp | head -20
```
Pahami:
- Bagaimana PSP melakukan init, update, render
- Bagaimana PSP handle edge cases (null, invalid state, boundary)
- Bagaimana PSP handle user interaction (Touch, Click)

#### 2. Feature Parity Checklist (Wajib)
Bandingkan behavior satu-per-satu dengan PSP. Contoh template:

| Aspek | PSP (`TouchControlVisibilityScreen`) | GBA (`GBATouchVisibilityPopup`) | Match? |
|-------|--------------------------------------|----------------------------------|--------|
| nextToggleAll_ init | `true` | Harus `true` | ✅ |
| Toggle All scope | Semua button (game + system) | Harus semua button | ✅ |
| SetMinimumAlpha | Unconditional | Harus unconditional | ✅ |
| Pause disable logic | Via `System_GetPropertyBool` | Sama | ✅ |
| Save on exit | `onFinish()` (all paths) | `OnCompleted()` (all paths) | ✅ |

**Catat setiap gap sebagai task** — jangan skip meskipun kelihatan kecil.

#### 3. Identifikasi PSP-Specific Logic (Wajib)
Tidak semua yang ada di PSP cocok untuk GBA. Filter:
- ❌ PSP-only buttons: Square, Triangle, Analog Stick — **JANGAN** ditambahkan ke GBA
- ❌ PSP-only behavior: analog deadzone, pressure sensitivity — **JANGAN** ditiru
- ✅ Shared behavior: Toggle All, save on exit, visibility toggles
- ✅ Shared config: `TouchControlConfig` (Fast-forward, Pause di `g_Config.GetTouchControlsConfig()`)

#### 4. Catat Mismatch ke PSP Knowledge Base
Setiap kali menemukan perbedaan behavior yang tidak disengaja (bug), catat:
```
# File: UI/CoreTouchLayoutScreen.cpp
# Bug: nextToggleAll_ mulai false, PSP mulai true
# Fix: false → true
# Tanggal: 2026-06-25
```

### 🔵 Scope Definition (Wajib Sebelum Implementasi)

Sebelum menulis kode, tulis definisi scope:

```md
## Scope: <Nama Fitur>

### Apa yang akan dibuat
- [ ] Button Pause di GBA game screen
- [ ] Fast-forward toggle di visibility popup

### PSP equivalent
- `UI/GamepadEmu.cpp::CreatePadLayout()` — Pause button
- `UI/TouchControlVisibilityScreen` — system button toggles

### Batasan (Apa yang TIDAK dibuat)
- ❌ Tidak membuat PSP-only buttons (Square, Triangle, Analog)
- ❌ Tidak membuat settings screen baru (pakai existing)

### Edge cases yang harus di-handle
- [ ] Device tanpa back button → Pause button minimal alpha
- [ ] TouchControlConfig belum di-init → InitPadLayout
```

### 🟠 Anti-Hallucination Rule

> **JANGAN PERNAH** menebak-nebak API tanpa verifikasi.

Setiap kali menulis:
- `new ClassName(...)` → cek constructor di file `.h`
- `ImageID("...")` → cek apakah ID itu terdaftar di atlas
- `#include "..."` → pastikan file exists
- `System_*` / `GetI18NCategory` / `screenManager()` → cek return type

gunakan `grep -rn "ClassName" UI/ | head -10` atau `read` file header sebelum pakai.

Kalau tidak bisa diverifikasi karena file terlalu besar, tanyakan ke user. Jangan tebak.

### 🟠 Code Review Gate (Quality Gate #2)

Sebelum commit, jalankan checklist ini:

#### Pre-Commit Checklist
```
[ ] 1. Unified diff review — tidak ada perubahan upstream
[ ] 2. Edge cases:
     - Null/empty bounds → w=0, h=0
     - Division by zero → image->w == 0 check
     - Config belum di-init → fallback values
     - Platform-specific → System_GetPropertyBool check
[ ] 3. PSP reference — semua behavior sudah match?
[ ] 4. Compile — fitur ON ✅ | fitur OFF ✅
[ ] 5. Naming convention — [PPSSPP-FORK] marker ada?
[ ] 6. IsFitur() pattern — call site tanpa #ifdef?
[ ] 7. Helper method extraction — logic >5 baris dipisah?
```

#### Post-Commit Diff Verification
```bash
git diff HEAD~1 -- UI/ UI/EmuCore/ EmuCore/
# Pastikan tidak ada file upstream yang berubah
```

### 🟢 IsFitur() Pattern — Kurangi #ifdef di Call Site
Gunakan constexpr helper untuk menggantikan `#ifdef` di call site:
```cpp
// Header — di luar #ifdef
#ifdef PPSSPP_FITUR
    bool IsFitur() const { return flag_ != Default; }
#else
    static constexpr bool IsFitur() { return false; }
#endif

// Call site — tanpa #ifdef
if (IsFitur()) {
    DoFiturStuff();
}
```
Compiler otomatis eliminasi dead branch saat fitur OFF.

### 🟢 Helper Method Extraction — Pisahkan Logic dari File Upstream
Jika logic fitur kustom >5 baris di file upstream, extract ke helper:
```cpp
// Di file upstream (call site minimal)
#ifdef PPSSPP_FITUR
    if (IsFitur()) { UpdateFitur(); return; }
#endif

// Implementasi helper di file terpisah (atau dibungkus #ifdef sekali)
void EmuScreen::UpdateFitur() {
    // ... semua logic fitur di sini
}
```
Convention naming: `Init<Fitur>()`, `Shutdown<Fitur>()`, `Update<Fitur>()`, `Render<Fitur>()`.

### 🟢 PSP Knowledge Base — Dokumentasi Mismatch

Catat setiap mismatch yang ditemukan antara implementasi GBA dan PSP di sini.
Tujuannya: agent lain tidak mengulangi bug yang sama.

```
# ======================================================
# PSP Knowledge Base — GBA Implementation Mismatches
# ======================================================

# --- nextToggleAll_ initial value ---
# File: UI/CoreTouchLayoutScreen.cpp -> GBATouchVisibilityPopup
# Bug: nextToggleAll_ mulai false (GBA), harus true (PSP)
# Akibat: Toggle All pertama malah SEMBUNYIKAN semua button
# Fix: false → true (komit c989dbf886)
# Pelajaran: PSP value WAJIB dicek sebelum inisialisasi

# --- Toggle All tidak cover system buttons ---
# File: UI/CoreTouchLayoutScreen.cpp -> GBATouchVisibilityPopup
# Bug: Toggle All hanya toggle game buttons (A/B/Select/etc),
#      Fast-forward dan Pause tidak kena. PSP toggle SEMUA.
# Fix: extend handler untuk juga toggle system buttons
# Pelajaran: jangan asumsi PSP Toggle All scope sama dengan GBA —
#      selalu verifikasi scope PSP dulu

# --- SetMinimumAlpha conditional ---
# File: UI/EmuScreen.cpp -> GBA Pause button
# Bug: SetMinimumAlpha(0.1f) hanya dipanggil pada device
#      tanpa back button. PSP panggil UNCONDITIONAL.
# Fix: unconditional (code review fix)
# Pelajaran: verify PSP code, jangan tebak kondisinya

# --- System buttons tidak dibuat ---
# File: UI/EmuScreen.cpp -> GBA CreateViews
# Bug: Fast-forward dan Pause button tidak dibuat di GBA mode,
#      padahal PSP CreatePadLayout selalu menyediakannya
# Fix: tambah create untuk Pause + Fast-forward bool button
# Pelajaran: mapping fitur PSP->GBA harus lengkap, bukan hanya
#      core buttons (A/B/Select/Start/L/R/D-Pad)
```

### 🟢 Build Flag Convention

| Flag | Scope | Note |
|------|-------|------|
| `PPSSPP_MULTICORE` | Top-level — enable semua multi-core | Wajib ON untuk GBA |
| `PPSSPP_GBA` | (future) | Saat ini masih pakai `PPSSPP_MULTICORE` |

Aturan:
- Semua file kustom di `UI/` yang pakai `#ifdef PPSSPP_MULTICORE`:
  - WAJIB ada `// [PPSSPP-FORK]` marker
  - WAJIB zero deletion
  - WAJIB verifikasi `#ifndef PPSSPP_MULTICORE` (PSP path) masih build
- Jika suatu fitur perlu dipisah dari multi-core umbrella, buat flag baru
