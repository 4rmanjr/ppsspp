# GBA Audio Quality Improvement — Implementation Plan (v3)

## Problem

Audio GBA di PPSSPP fork:
1. **Kresek-kresek seperti speaker rusak** (main complaint)
2. Volume pelan dibanding PSP audio
3. Click setelah fast-forward

## Root Causes (Prioritas)

### 🔴 PRIORITAS 1: LOW_WATER Mismatch — Resampler Starvation (Penyebab Kresek)

**Lokasi:** `EmuCore/GBACore.cpp` — RunFrame drain code (baris ~250)

Constructor meng-upgrade sinc width dari 8 → 16:
```cpp
mInterpolatorSincInit(&r->sinc, 8192, 16);
r->lowWaterMark = r->sinc.width;   // = 16
r->highWaterMark = r->sinc.width;  // = 16
```

TAPI drain code di RunFrame pakai hardcoded `LOW_WATER = 8`:
```cpp
static constexpr size_t LOW_WATER = 8;  // ❌ BUG!
if (remaining > LOW_WATER + 4) {
    size_t toDrain = remaining - LOW_WATER;
```

**Akibat:** Drain menyisakan 8 sample, tapi resampler butuh 16 (`highWaterMark`) → guard di `mAudioResamplerProcess` selalu true:
```c
if (timestamp + highWaterMark >= available) break;  // → no output
```

Resampler **starvation** → `audioStereoPairs_ = 0` → GetMixedAudio return buffer kosong → **crackle tiap frame**.

### 🟡 PRIORITAS 2: Shared dcCap State — GetMixedAudio vs GetRawAudio

Kedua fungsi pakai `dcCapL_`/`dcCapR_` yang **sama** dengan formula **berbeda**:
- GetMixedAudio: `dcCap += (left - dcCap) * 0.004f` (EMA tracker)
- GetRawAudio: `dcCap = (left - out) * 0.996f` (self-decay)

State corruption jika keduanya dipanggil bergantian → DC filter tidak efektif.

### 🟢 PRIORITAS 3: Stale Resampler Timestamp di ClearAudio

`ClearAudio()` clear source buffer tapi tidak reset `resampler_->timestamp` (≈8). Setelah frame skip, 8 sample pertama source baru dilewati → click.

### ⚪ PRIORITAS 4: Sample Rate Change

`mAudioResamplerSetSource` dipanggil tiap frame dengan rate dari `core_->audioSampleRate`. Jika SOUNDBIAS berubah, `timestep` berubah → phase discontinuity minimal.

## Files Modified

| File | Changes |
|------|---------|
| `EmuCore/GBACore.cpp` | RunFrame (LOW_WATER), ClearAudio (timestamp), GetRawAudio/GetMixedAudio (dcCap) |
| `EmuCore/GBACore.h` | + dcCapRawL_/R_ |

## Implementation

### Step 1 (🔴): Fix LOW_WATER — Gunakan lowWaterMark dari Resampler

**File:** `EmuCore/GBACore.cpp` — RunFrame, ganti `LOW_WATER = 8`:

```cpp
// Sebelumnya (BUG):
size_t remaining = mAudioBufferAvailable(src);
static constexpr size_t LOW_WATER = 8;
if (remaining > LOW_WATER + 4) {
    size_t toDrain = remaining - LOW_WATER;
    int16_t drainBuf[64];
    while (toDrain > 0) {
        size_t chunk = toDrain > 64 ? 64 : toDrain;
        mAudioBufferRead(src, drainBuf, chunk);
        toDrain -= chunk;
    }
}

// Setelah (FIXED):
size_t remaining = mAudioBufferAvailable(src);
auto *r = static_cast<struct mAudioResampler*>(resampler_);
size_t lowWater = (size_t)r->lowWaterMark;  // = sinc.width = 16
if (remaining > lowWater + 4) {
    size_t toDrain = remaining - lowWater;
    int16_t drainBuf[64];
    while (toDrain > 0) {
        size_t chunk = toDrain > 64 ? 64 : toDrain;
        mAudioBufferRead(src, drainBuf, chunk);
        toDrain -= chunk;
    }
}
```

### Step 2 (🟡): Pisah dcCap State

**File:** `EmuCore/GBACore.h` — tambah variabel:

```cpp
float dcCapL_ = 0.0f;        // GetMixedAudio (EMA tracker)
float dcCapR_ = 0.0f;
float dcCapRawL_ = 0.0f;     // GetRawAudio (self-decay capacitor)
float dcCapRawR_ = 0.0f;
```

**File:** `EmuCore/GBACore.cpp` — GetRawAudio:

```cpp
// Ganti dcCapL_ → dcCapRawL_:
float outL = left - dcCapRawL_;
float outR = right - dcCapRawR_;
dcCapRawL_ = (left - outL) * 0.996f;
dcCapRawR_ = (right - outR) * 0.996f;
```

### Step 3 (🟢): Reset Timestamp di ClearAudio

**File:** `EmuCore/GBACore.cpp` — method `ClearAudio()`:

```cpp
void GBACore::ClearAudio() {
    struct mAudioBuffer *src = core_->getAudioBuffer(core_);
    if (src) {
        mAudioBufferClear(src);
    }
    mAudioBufferClear(static_cast<struct mAudioBuffer*>(resampleDest_));
    // Reset timestamp agar tidak stale setelah frame skip
    auto *r = static_cast<struct mAudioResampler*>(resampler_);
    r->timestamp = 0.0;
    audioStereoPairs_ = 0;
}
```

### Step 4 (⚪): Rate Change — Cukup SetSource

Tidak perlu perubahan. `mAudioResamplerSetSource` sudah mengupdate `sourceRate`, dan `timestep = sourceRate / destRate` dihitung ulang setiap kali `mAudioResamplerProcess` dipanggil. Tidak ada state corruption.

## Testing

1. Build verification:
   ```bash
   cmake -DPPSSPP_MULTICORE=ON .. && make -j$(nproc)
   cmake -DPPSSPP_MULTICORE=OFF .. && make -j$(nproc)
   ```

2. Audio tests (with GBA ROM):
   - Normal gameplay — **tidak ada kresek-kresek**
   - Fast-forward 2x/4x — no clicks/pops
   - Save/Load state — audio stabil
   - Extended play (30+ min) — no regression

3. Verify: zero changes to upstream files.

## Status

⏳ Not started
