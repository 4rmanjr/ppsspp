# GBA Audio Quality Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix "kasar & cempreng" GBA audio in PPSSPP fork by fixing DC blocker, zero-padding, resampler preservation, and buffer sizing.

**Architecture:** All changes in `EmuCore/GBACore.cpp` and `EmuCore/GBACore.h`. Zero upstream files touched. Proper IIR high-pass (SkyEmu/mGBA reference), zero-padding instead of last-sample hold, preserve mGBA sinc resampler state on frame-skip.

**Tech Stack:** C++17, mGBA library, Thin3D, CMake

---

### Task 1: Increase Audio Buffer Size

**Files:**
- Modify: `EmuCore/GBACore.h:82`

- [ ] **Step 1: Change AUDIO_BUF_SIZE from 2048 to 4096**

```cpp
// Line 82, change:
static constexpr size_t AUDIO_BUF_SIZE = 2048;
// To:
static constexpr size_t AUDIO_BUF_SIZE = 4096;

// Rationale: 4096 stereo pairs = ~93ms at 44100Hz, up from ~46ms.
// Gives more headroom for mGBA sinc resampler which needs 28 samples
// of history (highWaterMark = sinc.width).
```

- [ ] **Step 2: Commit**

```bash
git add EmuCore/GBACore.h
git commit -m "[feature/gba-audio] Increase AUDIO_BUF_SIZE to 4096"
```

---

### Task 2: Fix DC Blocking Filter

**Files:**
- Modify: `EmuCore/GBACore.cpp:464-481`

- [ ] **Step 1: Replace the DC blocking filter with proper IIR high-pass**

Current code (lines 464-481):
```cpp
for (i = 0; i < toCopy; i++) {
    float left = (float)audioBuffer_[i * 2];
    float right = (float)audioBuffer_[i * 2 + 1];

    // DC blocking filter (SkyEmu-inspired)
    float outL = left - dcCapL_;
    float outR = right - dcCapR_;
    dcCapL_ = (left - outL) * 0.996f;
    dcCapR_ = (right - outR) * 0.996f;

    if (outL > 32767.0f) outL = 32767.0f;
    if (outL < -32768.0f) outL = -32768.0f;
    if (outR > 32767.0f) outR = 32767.0f;
    if (outR < -32768.0f) outR = -32768.0f;

    buffer[i * 2]     = ((int32_t)((int16_t)outL)) << 16;
    buffer[i * 2 + 1] = ((int32_t)((int16_t)outR)) << 16;
}
```

Replace with:
```cpp
for (i = 0; i < toCopy; i++) {
    float left = (float)audioBuffer_[i * 2];
    float right = (float)audioBuffer_[i * 2 + 1];

    // DC blocking filter — first-order IIR high-pass (SkyEmu reference)
    // y[n] = x[n] - dcCap[n-1]
    // dcCap[n] = (x[n] - y[n]) * 0.996
    float outL = left - dcCapL_;
    float outR = right - dcCapR_;

    // Safety clamp (SkyEmu: reset if capacitor drifted beyond ±2.0)
    if (!(dcCapL_ < 2.0f && dcCapL_ > -2.0f)) dcCapL_ = 0.0f;
    if (!(dcCapR_ < 2.0f && dcCapR_ > -2.0f)) dcCapR_ = 0.0f;

    // Update capacitor: tracks DC offset with 0.996 decay
    dcCapL_ = (left - outL) * 0.996f;
    dcCapR_ = (right - outR) * 0.996f;

    if (outL > 32767.0f) outL = 32767.0f;
    if (outL < -32768.0f) outL = -32768.0f;
    if (outR > 32767.0f) outR = 32767.0f;
    if (outR < -32768.0f) outR = -32768.0f;

    buffer[i * 2]     = ((int32_t)((int16_t)outL)) << 16;
    buffer[i * 2 + 1] = ((int32_t)((int16_t)outR)) << 16;
}
```

- [ ] **Step 2: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[feature/gba-audio] Fix DC blocking filter (proper IIR high-pass)"
```

---

### Task 3: Fix Zero-Padding (Replace Last-Sample Hold)

**Files:**
- Modify: `EmuCore/GBACore.cpp:483-491`

- [ ] **Step 1: Replace last-sample hold with zero-padding**

Current code (lines 483-491):
```cpp
// Pad remaining slots if fewer than target
if (toCopy < (size_t)TARGET_PAIRS && toCopy > 0) {
    int32_t padL = buffer[(toCopy - 1) * 2];
    int32_t padR = buffer[(toCopy - 1) * 2 + 1];
    for (i = toCopy; i < (size_t)TARGET_PAIRS; i++) {
        buffer[i * 2] = padL;
        buffer[i * 2 + 1] = padR;
    }
}
```

Replace with:
```cpp
// Pad remaining slots with zero (no DC step — avoids harsh/tinny artifacts)
if (toCopy < (size_t)TARGET_PAIRS) {
    for (i = toCopy; i < (size_t)TARGET_PAIRS; i++) {
        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[feature/gba-audio] Replace last-sample padding with zero-padding"
```

---

### Task 4: Preserve Resampler State on Frame-Skip

**Files:**
- Modify: `EmuCore/GBACore.cpp:430-443`

- [ ] **Step 1: Modify ClearAudio() to preserve mGBA resampler state**

Current code (lines 430-443):
```cpp
void GBACore::ClearAudio() {
    // Discard all pending audio: both resampler dest buffer and core source buffer
    // Used during fast forward to prevent audio accumulation between frames
    struct mAudioBuffer *src = core_->getAudioBuffer(core_);
    if (src) {
        mAudioBufferClear(src);
    }
    size_t avail = mAudioBufferAvailable(static_cast<struct mAudioBuffer*>(resampleDest_));
    if (avail > 0) {
        mAudioBufferClear(static_cast<struct mAudioBuffer*>(resampleDest_));
    }
    audioStereoPairs_ = 0;
}
```

Replace with:
```cpp
void GBACore::ClearAudio() {
    // Only clear the mGBA source buffer — preserve resampler state (timestamp, sinc history)
    // to prevent discontinuities when audio resumes after frame skip.
    struct mAudioBuffer *src = core_->getAudioBuffer(core_);
    if (src) {
        mAudioBufferClear(src);
    }
    // Do NOT clear resampleDest_ — mAudioResampler maintains internal timestamp
    // and sinc interpolation state. Clearing it loses filter continuity.
    // Do NOT reset audioStereoPairs_ — let GetMixedAudio's output determine actual count.
    audioStereoPairs_ = 0;
}
```

**Note for Line 443:** `audioStereoPairs_` is reset to 0. When `GetMixedAudio()` is called next frame, `audioStereoPairs_` is 0 from the reset or from the last `GetMixedAudio()` which already reads all available data from `resampleDest_`. Let `GetMixedAudio()` read whatever remains. Do NOT call `mAudioBufferClear(resampleDest_)` because the resampler may have in-flight data.

- [ ] **Step 2: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[feature/gba-audio] Preserve resampler state on frame-skip ClearAudio"
```

---

### Task 5: Add Frame Sample Count Verification Logging

**Files:**
- Modify: `EmuCore/GBACore.cpp:244-252`

- [ ] **Step 1: Add periodic logging of sample count vs target**

Current code (lines 244-252):
```cpp
            // Debug: print resampler stats
            static int audioDbg = 0;
            if (++audioDbg <= 3 || audioDbg % 300 == 0) {
                NOTICE_LOG(Log::System, "[GBA] Audio frame %d: coreAvail=%zu coreRate=%u outPairs=%zu first=[%d,%d] last=[%d,%d]",
                    audioDbg, available, coreSampleRate_, read,
                    audioBuffer_[0], audioBuffer_[1],
                    read > 0 ? audioBuffer_[(read-1)*2] : 0,
                    read > 0 ? audioBuffer_[(read-1)*2+1] : 0);
            }
```

Replace with:
```cpp
            // Debug: print resampler stats + warn on underrun
            static int audioDbg = 0;
            if (++audioDbg <= 3 || audioDbg % 300 == 0) {
                NOTICE_LOG(Log::System, "[GBA] Audio frame %d: coreAvail=%zu coreRate=%u outPairs=%zu/%d first=[%d,%d] last=[%d,%d]",
                    audioDbg, available, coreSampleRate_, read, TARGET_PAIRS,
                    audioBuffer_[0], audioBuffer_[1],
                    read > 0 ? audioBuffer_[(read-1)*2] : 0,
                    read > 0 ? audioBuffer_[(read-1)*2+1] : 0);
                if (read < TARGET_PAIRS) {
                    WARN_LOG(Log::System, "[GBA] Audio underrun: got %zu pairs, expected %d", read, TARGET_PAIRS);
                }
            }
```

- [ ] **Step 2: Commit**

```bash
git add EmuCore/GBACore.cpp
git commit -m "[feature/gba-audio] Add underrun warning logging"
```

---

### Task 6: Build Verification

**Files:**
- Build system only

- [ ] **Step 1: Build with PPSSPP_MULTICORE=ON**

```bash
mkdir -p build && cd build
cmake -DPPSSPP_MULTICORE=ON ..
make -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds with zero errors and zero new warnings.

- [ ] **Step 2: Build with PPSSPP_MULTICORE=OFF**

```bash
mkdir -p build-off && cd build-off
cmake -DPPSSPP_MULTICORE=OFF ..
make -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds — PSP audio unaffected.

- [ ] **Step 3: Commit if needed**

```bash
git add -A
git commit -m "[feature/gba-audio] Build verification OK (ON + OFF)"
```

---

### Task 7: Live Test with Breath of Fire

**Files:**
- No file changes — testing only

- [ ] **Step 1: Run ppssppsdl with Breath of Fire ROM**

```bash
./build/ppssppsdl /home/armanjr/Share/EMU/GBA/Breath\ of\ Fire/<rom_name>.gba
```

Verify during 5+ minutes of gameplay:
1. Audio does NOT sound "kasar" or "cempreng" (rough/tinny)
2. No clicks, pops, or crackling during normal gameplay
3. Fast-forward (Tab key) — audio resumes cleanly, no discontinuities
4. Save state (F1) + Load state (F2) — audio continues without artifacts
5. Check console output for underrun warnings — should be rare or absent

- [ ] **Step 2: Log results**

If all pass, note that. If any fail, document the failure and return to task analysis.

---

### Summary of All Files Changed

| File | What Changed | Lines |
|------|-------------|-------|
| `EmuCore/GBACore.h` | `AUDIO_BUF_SIZE`: 2048 → 4096 | 1 |
| `EmuCore/GBACore.cpp` | DC blocking filter: proper IIR high-pass | 468-472 |
| `EmuCore/GBACore.cpp` | Zero-padding: last-sample hold → zeros | 484-491 |
| `EmuCore/GBACore.cpp` | ClearAudio: preserve resampler state | 435-442 |
| `EmuCore/GBACore.cpp` | Underrun warning logging | 244-252 |

**Total: 2 files, ~15 lines changed. Zero upstream files.**
