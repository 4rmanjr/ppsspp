# GBA Audio Quality Fix — Design Spec

## Problem

Audio di GBA mode (`PPSSPP_MULTICORE=ON`) terdengar "kasar & cempreng" selama normal gameplay di PPSSPP fork. Masalah hanya terjadi di GBA core (via libmgba), **tidak** di PSP audio yang sudah excellent.

## Root Causes

1. **Last-sample padding** — Saat resampler menghasilkan < 735 pairs/frame, `GetMixedAudio()` mengisi sisa buffer dengan nilai sample terakhir, menciptakan DC step → harsh/tinny artifacts.
2. **Broken DC blocking filter** — Implementasi `dcCap_ = (left - outL) * 0.996f` hanya decay, bukan proper IIR high-pass. Tidak membuang DC offset dari GBA sound bias.
3. **Resampler state loss on frame-skip** — `ClearAudio()` membersihkan `resampleDest_` yang menghilangkan riwayat sinc filter → discontinuities.
4. **Small buffer** — `AUDIO_BUF_SIZE = 2048` rentan underrun.

## Scope

- Hanya file: `EmuCore/GBACore.cpp` dan `EmuCore/GBACore.h`
- Zero perubahan ke file upstream (Core/, GPU/, HLE/, MIPS/, UI/)
- Dual verification: `PPSSPP_MULTICORE=ON` ✅ + `OFF` ✅

## Design

### 1. Zero-Padding (GetMixedAudio:484-491)

Replace last-sample hold with zero-padding:

```cpp
// Before:
if (toCopy < (size_t)TARGET_PAIRS && toCopy > 0) {
    int32_t padL = buffer[(toCopy - 1) * 2];
    int32_t padR = buffer[(toCopy - 1) * 2 + 1];
    for (i = toCopy; i < (size_t)TARGET_PAIRS; i++) {
        buffer[i * 2] = padL;
        buffer[i * 2 + 1] = padR;
    }
}

// After:
if (toCopy < (size_t)TARGET_PAIRS) {
    for (i = toCopy; i < (size_t)TARGET_PAIRS; i++) {
        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;
    }
}
```

### 2. Proper DC Blocking Filter (GetMixedAudio:468-472)

SkyEmu-style first-order IIR high-pass:

```cpp
// Before:
float outL = left - dcCapL_;
float outR = right - dcCapR_;
dcCapL_ = (left - outL) * 0.996f;  // = dcCapL_ * 0.996f — just decays!
dcCapR_ = (right - outR) * 0.996f;

// After:
float outL = left - dcCapL_;
float outR = right - dcCapR_;

// Safety clamp (SkyEmu)
if (!(dcCapL_ < 2.0f && dcCapL_ > -2.0f)) dcCapL_ = 0.0f;
if (!(dcCapR_ < 2.0f && dcCapR_ > -2.0f)) dcCapR_ = 0.0f;

// Update capacitor: capacitor = (input - output) * 0.996
dcCapL_ = (left - outL) * 0.996f;
dcCapR_ = (right - outR) * 0.996f;
```

### 3. Preserve Resampler on Frame-Skip (ClearAudio:435-442)

```cpp
// Before:
mAudioBufferClear(src);
mAudioBufferClear(resampleDest_);
audioStereoPairs_ = 0;

// After:
mAudioBufferClear(src);  // Only clear source, keep resampler state
// Don't clear resampleDest_ — preserves sinc filter timestamp
// Don't reset audioStereoPairs_ — let GetMixedAudio handle it
```

### 4. Buffer Size (GBACore.h:82)

```
AUDIO_BUF_SIZE = 4096  (sebelumnya 2048)
```

## Testing

- **Platform**: SDL Linux (ppssppsdl)
- **ROM**: Breath of Fire (.gba)
- **Test scenarios**:
  1. Normal gameplay — no "cempreng"
  2. Fast-forward 2x/4x — no clicks/pops
  3. Save/Load state — clean audio resume
  4. Extended play (30+ min) — stable
  5. Dual build — `PPSSPP_MULTICORE=ON` + `OFF`

## Files Modified

| File | Changes | Risk |
|------|---------|------|
| `EmuCore/GBACore.cpp` | Lines 240-255, 435-442, 468-491 | Low |
| `EmuCore/GBACore.h` | Line 82 | None |

## References

- SkyEmu DC blocker: `src/gba.h:3674-3679` — `capacitor = (input - output) * 0.996`
- mGBA DC blocker: `src/gb/audio.c:482-486` — `cap = (sample << 16) - degraded * 65368`
- mGBA resampler: `src/util/audio-resampler.c` — streaming sinc, `timestamp` state preserved

## Status

✅ Design approved by user
⏳ Implementation plan (via writing-plans skill)
⏳ Implementation
⏳ Build verification (ON + OFF)
⏳ Live test with Breath of Fire
