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

---

## 🔷 Extensibility Architecture — Menambah Emulator Baru (N64, PS1, NDS, SNES, dll)

Setiap emulator baru WAJIB mengikuti arsitektur berikut. Tujuannya: **zero duplication**, **PSP parity konsisten**, dan **tiap core bisa di-add tanpa mengubah file core lain**.

### 🔷 Prinsip Dasar

1. **Core-agnostic classes** — Semua class baru WAJIB pakai prefix `Core` (misal `CoreDragDrop`), **bukan** `GBA`/`N64`/`PS1`. GBA-specific classes yang sudah ada akan di-refactor bertahap.
2. **Per-core definition** — Yang berbeda per core hanyalah **data** (button list, default layout, image IDs). Yang **sama** (renderer, popup, grid, drag-drop) harus generic.
3. **PSP parity WAJIB** — Setiap core baru WAJIB melewati Quality Gate #1 (Feature Parity) yang sama seperti GBA.
4. **Shared system buttons** — Fast-forward dan Pause dibuat OTOMATIS untuk semua non-PSP core.
5. **No binary routing** — `if (coreType_ != PSP) { ... }` **DILARANG** untuk kode baru. WAJIB pakai `switch(coreType_)`.

### 🔷 Arsitektur Component

```
EmuCore/
├── EmuCore.h                 → Type enum (PSP, GBA, N64, PS1, ...)
├── EmuCore.cpp               → DetectType() + Factory
├── Config.h                  → TouchConfig loader/saver (generic)
├── Config.cpp                → InitDefaultTouchConfigs() + CoreButtonRegistry
├── GBACore.h/.cpp            → GBA-specific (lifecycle, render, audio)
├── N64Core.h/.cpp            → (future) N64-specific
├── PS1Core.h/.cpp            → (future) PS1-specific
└── ...

UI/
├── CoreTouchLayoutScreen.cpp → Generic layout editor (CoreDragDrop, CoreLayoutView)
│                                Baca button map dari registry, bukan hardcoded switch
├── EmuScreen.cpp             → Core dispatch via switch(coreType_)
└── GamepadEmu.cpp            → CreateSystemTouchButtons() untuk semua core
```

### 🔷 Quality Gate #3 — PSP Parity WAJIB untuk Setiap Core Baru

**Setiap emulator baru** (N64, PS1, NDS, SNES, dll) WAJIB diperlakukan SAMA seperti GBA:

| Aturan | GBA | N64 (future) | PS1 (future) |
|--------|-----|-------------|-------------|
| Quality Gate #1 (Feature Parity) | ✅ | ✅ WAJIB | ✅ WAJIB |
| Quality Gate #2 (Code Review) | ✅ | ✅ WAJIB | ✅ WAJIB |
| Scope Definition | ✅ | ✅ WAJIB | ✅ WAJIB |
| Catat mismatch ke Knowledge Base | ✅ | ✅ WAJIB | ✅ WAJIB |
| Verifikasi PSP equivalent | ✅ | ✅ WAJIB | ✅ WAJIB |
| Build dual verification | ✅ | ✅ WAJIB | ✅ WAJIB |

Contoh: Kalau N64 punya equivalent PSP feature (misal analog stick PSP → analog stick N64), maka behavior-nya WAJIB match satu-per-satu.

### 🔷 CoreButtonRegistry — Definisi Tombol Per Core

GANTI hardcoded `case CTRL_*` switches dengan registry:

```cpp
// EmuCore/Config.h
struct CoreButtonDef {
    int keyCode;           // unique per core (e.g. N64_A, PS1_CROSS)
    const char *imageID;   // atlas image (e.g. "I_N64_A", "I_PS1_CROSS")
    const char *label;     // display name (e.g. "A", "Cross")
    BgType bgType;         // ROUND, RECT, SHOULDER, ANALOG, CUSTOM
    const char *bgImageID; // atlas image untuk background (e.g. "I_ROUND", "I_N64_C")
};

// Per-core registration
void RegisterButtonMap(EmuCore::Type type, std::span<const CoreButtonDef> buttons);
const CoreButtonDef *GetButtonDef(EmuCore::Type type, int keyCode);
std::span<const CoreButtonDef> GetButtonDefs(EmuCore::Type type);
```

**WAJIB:**
- [ ] Tiap core daftarkan button map di `InitDefaultTouchConfigs()`
- [ ] `GBALayoutView::CreateViews()` → `CoreLayoutView::CreateViews()` baca dari `GetButtonDefs(coreType_)`
- [ ] `GBATouchVisibilityPopup` → `CoreTouchVisibilityPopup` iterasi `GetButtonDefs(coreType_)`
- [ ] Tidak boleh ada `case CTRL_*` di class renderer/popup — semua data-driven

### 🔷 EmuScreen Core Dispatch — Wajib switch, bukan binary

**LARANG:**
```cpp
// ⛔ DILARANG — binary routing, tidak extensible
if (coreType_ != EmuCore::Type::PSP) {
    // GBA-specific (N64 gak bisa masuk sini)
}
```

**WAJIB:**
```cpp
// ✅ WAJIB — switch dispatch
switch (coreType_) {
#ifdef PPSSPP_GBA
case EmuCore::Type::GBA:
    CreateGBATouchLayout(bounds, orientation);
    break;
#endif
#ifdef PPSSPP_N64
case EmuCore::Type::N64:
    CreateN64TouchLayout(bounds, orientation);
    break;
#endif
// ...
default:
    root_ = CreatePadLayout(touch, bounds.w, bounds.h, &pauseTrigger_, &g_controlMapper);
    break;
}
```

**Shared system buttons** — Fast-forward + Pause dipanggil SETELAH dispatch, untuk SEMUA non-PSP core:
```cpp
if (coreType_ != EmuCore::Type::PSP) {
    CreateSystemTouchButtons(root_, bounds, deviceOrientation);
}
```

### 🔷 Generic Touch Classes — Wajib Core-agnostic

| Class Saat Ini (GBA-specific) | Class Baru (Generic) |
|------------------------------|---------------------|
| `GBADragDrop` | `CoreDragDrop` — baca button def dari registry, hitung scale dari `buttonDef.imageID` |
| `GBASnapGrid` | `CoreSnapGrid` — sudah generic (tidak ada GBA reference) ✅ |
| `GBALayoutView` | `CoreLayoutView` — `CreateViews()` iterasi `GetButtonDefs(coreType_)`, hardcoded `case CTRL_*` diganti lookup |
| `GBACheckBoxChoice` | `CoreCheckBoxChoice` — sudah generic ✅ (hanya wrapper) |
| `GBATouchVisibilityPopup` | `CoreTouchVisibilityPopup` — title dari `EmuCore::GetConfigSection(coreType_)`, button rows dari `GetButtonDefs(coreType_)` |

**Aturan:**
- [ ] Setiap class baru di `CoreTouchLayoutScreen.cpp` WAJIB pakai prefix `Core`, bukan nama core spesifik
- [ ] Semua data per-core (button def, image ID, label) WAJIB dari registry, bukan hardcoded
- [ ] Satu-satunya tempat hardcoded per-core adalah di `EmuCore::Config.cpp` (registrasi button map + default layout)

### 🔷 Directory & File Path Per Core — Wajib via Resolver

**LARANG:**
```cpp
// ⛔ DILARANG — hardcoded path
File::CreateFullPath(GetSysDirectory(DIRECTORY_SAVEDATA) / "GBA");
```

**WAJIB:**
```cpp
// ✅ WAJIB — resolver
std::string GetCoreSaveDir(EmuCore::Type type) {
    switch (type) {
    case EmuCore::Type::GBA: return "GBA";
    case EmuCore::Type::N64: return "N64";
    case EmuCore::Type::PS1: return "PS1";
    default: return "";
    }
}
```

Resolver ini WAJIB untuk:
- `DIRECTORY_SAVEDATA / <core>` — save files
- `DIRECTORY_SAVESTATE / <core>` — save states
- `g_gbaSavePrefix` → `GetCoreSavePrefix(type)`
- `[GBA ControlLayout]` → `[<Core> ControlLayout]` (via config section resolver)

### 🔷 Step-by-Step: Menambah Emulator Baru

Gunakan checklist ini SETIAP kali menambah core baru:

```
## Menambah Core Baru: <Nama Core>

### [Phase 0] Persiapan
- [ ] Tambah `PPSSPP_<CORE>` flag di CMakeLists.txt
- [ ] Tambah `EmuCore::Type::<CORE>` di EmuCore/EmuCore.h
- [ ] Tambah `DetectType()` + `Create()` di EmuCore/EmuCore.cpp
- [ ] Buat `<Core>Core.h/.cpp` (implementasi Core interface)
- [ ] Update `EmuCore/CMakeLists.txt`

### [Phase 1] Default Layout
- [ ] Daftarkan button map via RegisterButtonMap() di EmuCore/Config.cpp
- [ ] Definisikan default touch layout (positions + sizes)
- [ ] Isolasi config section: [<Core> ControlLayout]
- [ ] Update InitDefaultTouchConfigs()

### [Phase 2] EmuScreen Integration
- [ ] Tambah case di switch(coreType_) dispatch
- [ ] Buat Create<Core>TouchLayout() — panggil CoreLayoutView
- [ ] System buttons otomatis (CreateSystemTouchButtons)
- [ ] Update Is<Core>() pattern (constexpr fallback)
- [ ] Update Init/Update/Shutdown routing

### [Phase 3] Quality Gate #1 — PSP Parity
- [ ] Identifikasi PSP equivalent feature untuk setiap aspek
- [ ] Feature Parity Checklist (toggle all, save on exit, visibility icons, dll)
- [ ] Filter PSP-specific logic (jangan tiru analog deadzone kalau core-nya gak punya analog)
- [ ] Catat mismatch ke PSP Knowledge Base

### [Phase 4] Quality Gate #2 — Code Review
- [ ] Unified diff review (zero upstream change)
- [ ] Edge cases (bounds=0, div-by-zero, null config)
- [ ] Compile ON ✅ | OFF ✅
- [ ] [PPSSPP-FORK] markers — semua file baru
- [ ] Post-commit diff verification
```

### 🔷 Zero Duplication Rule

> **Setiap tambahan core baru:**
> ✅ Hanya tambah: `enum Type`, `DetectType`, `Factory`, `CoreFile`, `ButtonMap`, `DefaultLayout`
> ❌ TIDAK perlu ubah: generic classes (`CoreDragDrop`, `CoreLayoutView`, `CoreSnapGrid`, `CoreCheckBoxChoice`, `CoreTouchVisibilityPopup`)
> ❌ TIDAK perlu duplikasi: system buttons, popup rendering, grid drawing, drag-drop logic
> ❌ TIDAK perlu switch baru di: layout view, visibility popup, grid renderer

### 🔷 262 GBA References → Path to Generification

Saat ini ada ~262 reference GBA-specific di codebase. Target refactor:

| Phase | Target | File |
|-------|--------|------|
| 1 | Class rename | `GBADragDrop` → `CoreDragDrop` |
| 2 | Class rename | `GBALayoutView` → `CoreLayoutView` |
| 3 | Class rename | `GBATouchVisibilityPopup` → `CoreTouchVisibilityPopup` |
| 4 | Switch ke registry | Hardcoded `case CTRL_*` → `GetButtonDefs(coreType_)` |
| 5 | System buttons | Inline → `CreateSystemTouchButtons()` |
| 6 | Binary routing | `if (coreType_ != PSP)` → `switch(coreType_)` |
| 7 | Hardcoded paths | String literal → `GetCoreDirectory(type)` |
| 8 | GBA-prefixed methods | `AddGBATouchButtons` → `AddCoreTouchButtons` |

Setiap phase WAJIB:
- ✅ Build ON + OFF
- ✅ Zero upstream change
- ✅ PSP parity verified
