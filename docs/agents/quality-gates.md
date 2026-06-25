# Quality Gates — PSP Parity & Code Review

> **Referenced by:** `AGENTS.md`
> **Applies to:** Semua fitur GBA dan emulator baru (N64, PS1, dll)
> **Tujuan:** Menjamin tidak ada gap behavior dengan PSP, tidak ada hallucination, dan kode clean.

---

## Daftar Isi

- [Gate #1: Feature Parity dengan PSP](#-quality-gate-1--feature-parity-dengan-psp)
- [Gate #2: Code Review Gate](#-quality-gate-2--code-review-gate)
- [Gate #3: PSP Parity Wajib untuk Setiap Core Baru](#-quality-gate-3--psp-parity-wajib-untuk-setiap-core-baru)
- [Supporting Rules](#supporting-rules)

---

## 🔵 Quality Gate #1 — Feature Parity dengan PSP

Setiap fitur GBA (atau core lain) yang memiliki equivalent PSP WAJIB mengikuti aturan ini.

### 1. Read PSP Reference (Wajib)

SEBELUM menulis kode, baca implementasi PSP yang equivalent:

```bash
grep -n "<nama_fungsi/class>" UI/<psp_file>.cpp | head -20
```

Pahami:
- Bagaimana PSP melakukan init, update, render
- Bagaimana PSP handle edge cases (null, invalid state, boundary)
- Bagaimana PSP handle user interaction (Touch, Click)

### 2. Feature Parity Checklist (Wajib)

Bandingkan behavior satu-per-satu dengan PSP. Contoh template:

| Aspek | PSP (`TouchControlVisibilityScreen`) | GBA (`GBATouchVisibilityPopup`) | Match? |
|-------|--------------------------------------|----------------------------------|--------|
| nextToggleAll_ init | `true` | `true` | ✅ |
| Toggle All scope | Semua button (game + system) | Semua button | ✅ |
| SetMinimumAlpha | Unconditional | Unconditional | ✅ |
| Pause disable logic | Via `System_GetPropertyBool` | Sama | ✅ |
| Save on exit | `onFinish()` (all paths) | `OnCompleted()` (all paths) | ✅ |

**Catat setiap gap sebagai task** — jangan skip meskipun kelihatan kecil.

### 3. Identifikasi PSP-Specific Logic (Wajib)

Tidak semua yang ada di PSP cocok untuk core lain. Filter:

| ❌ Jangan Ditiru | ✅ Boleh Ditiru |
|-----------------|-----------------|
| PSP-only buttons (Square, Triangle, Analog Stick) | Shared behavior (Toggle All, save on exit, visibility toggles) |
| Analog deadzone, pressure sensitivity | Shared config (`TouchControlConfig`: Fast-forward, Pause) |

### 4. Catat Mismatch ke PSP Knowledge Base

Setiap kali menemukan perbedaan behavior yang tidak disengaja (bug), catat di [`psp-knowledge-base.md`](psp-knowledge-base.md):

```
# File: UI/CoreTouchLayoutScreen.cpp
# Bug: nextToggleAll_ mulai false, PSP mulai true
# Fix: false → true
# Tanggal: 2026-06-25
```

---

## 🟠 Quality Gate #2 — Code Review Gate

WAJIB dijalankan sebelum commit.

### Pre-Commit Checklist

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

### Post-Commit Diff Verification

```bash
git diff HEAD~1 -- UI/ UI/EmuCore/ EmuCore/
# Pastikan tidak ada file upstream yang berubah
```

---

## 🔷 Quality Gate #3 — PSP Parity WAJIB untuk Setiap Core Baru

Setiap emulator baru (N64, PS1, NDS, SNES, dll) WAJIB diperlakukan SAMA seperti GBA:

| Aturan | GBA | N64 (future) | PS1 (future) |
|--------|-----|-------------|-------------|
| Quality Gate #1 (Feature Parity) | ✅ | ✅ WAJIB | ✅ WAJIB |
| Quality Gate #2 (Code Review) | ✅ | ✅ WAJIB | ✅ WAJIB |
| Scope Definition | ✅ | ✅ WAJIB | ✅ WAJIB |
| Catat mismatch ke Knowledge Base | ✅ | ✅ WAJIB | ✅ WAJIB |
| Verifikasi PSP equivalent | ✅ | ✅ WAJIB | ✅ WAJIB |
| Build dual verification | ✅ | ✅ WAJIB | ✅ WAJIB |

**Contoh:** Kalau N64 punya equivalent PSP feature (misal analog stick), maka behavior-nya WAJIB match satu-per-satu. Kalau PS1 punya L1/L2/R1/R2, samakan dengan PSP L/R trigger behavior.

---

## Supporting Rules

### 🔵 Scope Definition (Wajib Sebelum Implementasi)

Sebelum menulis kode, tulis definisi scope:

```
## Scope: <Nama Fitur>

### Apa yang akan dibuat
- [ ] Button Pause di GBA game screen
- [ ] Fast-forward toggle di visibility popup

### PSP equivalent
- UI/GamepadEmu.cpp::CreatePadLayout() — Pause button
- UI/TouchControlVisibilityScreen — system button toggles

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

| Jika menulis... | WAJIB cek... |
|----------------|--------------|
| `new ClassName(...)` | Constructor di file `.h` via `grep` / `read` |
| `ImageID("...")` | Apakah ID itu terdaftar di atlas texture |
| `#include "..."` | Pastikan file exists |
| `System_*` / `GetI18NCategory` / `screenManager()` | Return type dari header |

Kalau tidak bisa diverifikasi karena file terlalu besar, **tanyakan ke user**. Jangan tebak.
