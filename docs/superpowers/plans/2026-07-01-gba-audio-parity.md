# GBA Audio Parity — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make GBA audio quality and behavior match PSP audio on both PC and Android — same volume pipeline, same debug capabilities, same save/restore behavior.

**Architecture:** The PPSSPP audio pipeline is unified via `StereoResampler`/`GranularMixer` (shared ring buffer). GBA already benefits from drift prevention, extra buffering, and debug stats in the mixer layer. The gaps are: (1) `iGameVolume` not applied to GBA pushes, (2) alt-speed volume only works during fast-forward, (3) per-frame burst pushes cause buffer spikes, and (4) no audio save state. All fixes live in either `EmuCore/GBACore.*` (audio pipeline internals) or `UI/EmuScreen.cpp` (push site), using existing cross-platform infrastructure.

**Tech Stack:** C++17, PPSSPP fork with PPSSPP_MULTICORE, StereoResampler/GranularMixer (Core/HW/), mGBA sinc resampler, CMake

---

### Task 1: Integrate iGameVolume into GBA Audio Push

**Goal:** Master "Game Volume" slider affects GBA audio identically to PSP audio.

**Background:** PSP applies `Volume100ToMultiplier(g_Config.iGameVolume)` centrally in `__AudioUpdate()`. GBA bypasses this — its volume at `System_AudioPushSamples()` is always `1.0f` (or `iAltSpeedVolume` during FF). The GBA has its own `fGBAVolume` but this is separate from the master volume. For a PSP+GBA coprocessor scenario, both should respect the same master volume.

**Files:**
- Modify: `UI/EmuScreen.cpp:1597-1603`

- [ ] **Step 1: Read current GBA volume logic**

Read `UI/EmuScreen.cpp` lines 1597-1603 to understand the current volume calculation:

```cpp
float volume = 1.0f;
if (PSP_CoreParameter().fastForward) {
    volume = Volume100ToMultiplier(g_Config.iAltSpeedVolume);
}
System_AudioPushSamples(mixBuffer, (int)stereoPairs, volume);
```

- [ ] **Step 2: Add iGameVolume as baseline**

Change to apply `iGameVolume` first, then multiply by `iAltSpeedVolume` when applicable:

```cpp
float volume = Volume100ToMultiplier(std::clamp(g_Config.iGameVolume, 0, VOLUMEHI_FULL));
if (PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL && g_Config.iAltSpeedVolume != -1) {
    volume *= Volume100ToMultiplier(g_Config.iAltSpeedVolume);
}
System_AudioPushSamples(mixBuffer, (int)stereoPairs, volume);
```

Key changes from original:
1. `volume` starts from `iGameVolume` instead of `1.0f`
2. Alt-speed check matches PSP's logic: `fpsLimit != NORMAL && iAltSpeedVolume != -1`
3. Volume multiplier is multiplied (not replaced) — matching PSP's behavior

- [ ] **Step 3: Verify no iGameVolume double-application**

Check that `fGBAVolume` in `ProcessAudioSamples()` is not also multiplying by `iGameVolume`. It shouldn't be — `fGBAVolume` is the GBA-specific internal volume offset (like a per-emulator gain trim). The `iGameVolume` is the master volume applied at push time. This is correct design separation.

- [ ] **Step 4: Build and test**

```bash
cmake --build build-final --target ppsspp 2>&1 | tail -20
```

Expected: No errors.

Verify volume behavior in log — run PPSSPP with a GBA game, check that changing "Game Volume" slider changes GBA audio output volume.

---

### Task 2: Match Alt-Speed Volume Behavior to PSP

**Goal:** Alt-speed volume applies in ALL non-normal speed modes, not just fast-forward.

**Background:** PSP applies `iAltSpeedVolume` whenever `fpsLimit != FPSLimit::NORMAL || PSP_CoreParameter().fastForward`. GBA only checks `fastForward`. This means slow-motion (`fpsLimit == FPSLimit::CUSTOM1` with lower fps) doesn't lower GBA volume.

**Files:**
- Already covered in Task 1 Step 2 (the condition becomes `fpsLimit != NORMAL`).

- [ ] **Step 1: Verify the fix from Task 1 covers this**

The code change in Task 1 uses `PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL` which matches PSP's condition exactly. If Task 1 is done, this task is automatically complete.

- [ ] **Step 2: Verify edge case — fastForward + fpsLimit == NORMAL**

When user holds fast-forward button but fpsLimit is NORMAL, `Volume100ToMultiplier(iAltSpeedVolume)` should apply. Check that `PSP_CoreParameter().fastForward` sets `fpsLimit != NORMAL` — it does (fast-forward sets fpsLimit to 0 internally). So the condition is correct.

---

### Task 3: Smooth GBA Audio Push Bursts

**Goal:** Reduce buffer spikes caused by pushing 735 samples once per frame instead of 64 samples per audio update.

**Background:** PSP pushes `hwBlockSize` (64) samples per `__AudioUpdate()`, which fires ~689 times/sec. GBA pushes `TARGET_PAIRS` (735) samples once per frame (60 fps). This is a 11.5x larger burst. While the StereoResampler handles this, the buffer fullness fluctuates more → more stress on drift prevention → potential for overruns on low-buffer platforms.

**Solution:** Split the per-frame 735-sample push into smaller chunks (e.g., match PSP's 64-sample block size) using a staging buffer in GBACore. This also makes the GBA audio profile identical to PSP's.

**Files:**
- Modify: `EmuCore/GBACore.h` — add staging buffer members
- Modify: `EmuCore/GBACore.cpp` — add `GetAudioIncremental()` method
- Modify: `UI/EmuScreen.cpp` — call new incremental push method

- [ ] **Step 1: Add staging buffer to GBACore.h**

Inside the `private:` section of `GBACore`, add:

```cpp
// [PPSSPP-FORK] GBA Audio Parity: staging buffer to break 735-sample push into 64-sample blocks
int32_t stagingBuffer_[64 * 2]{};
size_t stagingFill_ = 0;
```

This gives GBACore the ability to hold a partial block between calls.

- [ ] **Step 2: Add GetAudioIncremental() to GBACore.cpp**

This method returns audio in small fixed-size chunks, matching PSP's hwBlockSize=64 pattern:

```cpp
// [PPSSPP-FORK] GBA Audio Parity: return audio in PSP-sized chunks (64 stereo pairs)
// instead of full-frame bursts (735 pairs). Returns number of stereo pairs written to buffer
// (always 64, except on the last chunk). Returns 0 when all audio has been consumed.
size_t GBACore::GetAudioIncremental(int32_t *buffer, size_t maxPairs) {
    if (!buffer || maxPairs == 0) return 0;

    // If we have leftover from last call, serve that first
    if (stagingFill_ > 0) {
        size_t toCopy = std::min(stagingFill_, maxPairs);
        memcpy(buffer, stagingBuffer_, toCopy * 2 * sizeof(int32_t));
        if (toCopy < stagingFill_) {
            // Move remaining to front of staging buffer
            memmove(stagingBuffer_, stagingBuffer_ + toCopy * 2, (stagingFill_ - toCopy) * 2 * sizeof(int32_t));
            stagingFill_ -= toCopy;
        } else {
            stagingFill_ = 0;
        }
        return toCopy;
    }

    // No leftover — run the full audio pipeline
    size_t pairs = 0;
    GetMixedAudio(stagingBuffer_, &pairs);
    if (pairs == 0) return 0;

    stagingFill_ = pairs;
    size_t toCopy = std::min(stagingFill_, maxPairs);
    memcpy(buffer, stagingBuffer_, toCopy * 2 * sizeof(int32_t));
    if (toCopy < stagingFill_) {
        memmove(stagingBuffer_, stagingBuffer_ + toCopy * 2, (stagingFill_ - toCopy) * 2 * sizeof(int32_t));
        stagingFill_ -= toCopy;
    } else {
        stagingFill_ = 0;
    }
    return toCopy;
}
```

- [ ] **Step 3: Declare GetAudioIncremental in GBACore.h**

Add to the `public:` section of `GBACore`:

```cpp
// [PPSSPP-FORK] GBA Audio Parity: incremental audio for PSP-sized chunks
size_t GetAudioIncremental(int32_t *buffer, size_t maxPairs);
```

- [ ] **Step 4: Update UpdateGBA() in EmuScreen.cpp to use incremental push**

Replace the current audio push code:

```cpp
// Old code (lines ~1590-1607):
size_t stereoPairs = 0;
int32_t mixBuffer[GBA_AUDIO_BUF_SIZE * 2];
gba->GetMixedAudio(mixBuffer, &stereoPairs);

static int audioDebug = 0;
if (++audioDebug <= 5 || audioDebug % 180 == 0) {
    NOTICE_LOG(Log::System, "[GBA] Audio frame %d: pairs=%zu frames=%d", audioDebug, stereoPairs, framesToRun);
}
if (stereoPairs > 0) {
    float volume = 1.0f;
    if (PSP_CoreParameter().fastForward) {
        volume = Volume100ToMultiplier(g_Config.iAltSpeedVolume);
    }
    System_AudioPushSamples(mixBuffer, (int)stereoPairs, volume);
}
```

Replace with:

```cpp
// [PPSSPP-FORK] GBA Audio Parity: incremental push matching PSP block size
float volume = Volume100ToMultiplier(std::clamp(g_Config.iGameVolume, 0, VOLUMEHI_FULL));
if (PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL && g_Config.iAltSpeedVolume != -1) {
    volume *= Volume100ToMultiplier(g_Config.iAltSpeedVolume);
}

int32_t chunkBuf[64 * 2];
size_t chunkPairs = 0;
static int audioDebug = 0;
int debugLimit = audioDebug <= 5 ? 5 : 0; // print first 5 frames

while ((chunkPairs = gba->GetAudioIncremental(chunkBuf, 64)) > 0) {
    System_AudioPushSamples(chunkBuf, (int)chunkPairs, volume);
    if (++audioDebug <= 5 && debugLimit == 5) {
        NOTICE_LOG(Log::System, "[GBA] Audio chunk: pairs=%zu volume=%.2f", chunkPairs, volume);
    }
}
```

Note: The staging buffer is per-GBACore instance, so the incremental pushes work correctly even when `framesToRun > 1` (fast-forward).

- [ ] **Step 5: Build and verify**

```bash
cmake --build build-final --target ppsspp 2>&1 | tail -20
```

Expected: No errors.

---

### Task 4: GBA Audio Save State Support

**Goal:** GBA audio state (pending samples, filter states, resampler position) is saved and restored with savestates, preventing clicks/pops.

**⚠️ Key constraint:** GBA `SaveState`/`LoadState` use mGBA's raw `void*` buffer format — can't extend. Instead, extend `SaveStateToFile`/`LoadStateFromFile` which are the ACTUAL entry points used by EmuScreen (F1/F3 hotkeys). Append audio state after mGBA core state with a magic marker for backward compat.

**Background:** PSP serializes all audio channels + resampler state via `PointerWrap`. GBA's `SaveState`/`LoadState` delegate to mGBA's raw `void*` buffer format — can't extend those. But GBA saving actually uses `SaveStateToFile`/`LoadStateFromFile` (file-based, called from EmuScreen). So we extend the file format: append audio state AFTER the mGBA core state with a magic marker for backward compatibility.

**Files:**
- Modify: `EmuCore/GBACore.h` — add audio state struct + `SaveAudioState`/`LoadAudioState`
- Modify: `EmuCore/GBACore.cpp` — implement audio serialization in `SaveStateToFile`/`LoadStateFromFile`

- [ ] **Step 1: Add audio state struct and private helpers to GBACore.h**

```cpp
// [PPSSPP-FORK] GBA Audio Parity: appended after mGBA core state in .ppst file
#pragma pack(push, 1)
struct GBAAudioStateHeader {
    uint32_t magic;         // 'AUDI'
    uint32_t version;       // 1
    uint32_t totalSize;     // sizeof header + payload
    float dcCapL, dcCapR;
    float lowPassL, lowPassR;
    double resamplerTimestamp;
    uint32_t stagingFill;   // pairs in staging buffer
    // followed by stagingBuffer_[stagingFill * 2] if stagingFill > 0
};
#pragma pack(pop)
```

Also add private methods to `GBACore.h`:
```cpp
// [PPSSPP-FORK] GBA Audio Parity
bool AppendAudioState(std::vector<uint8_t> &buffer) const;
bool RestoreAudioState(const uint8_t *data, size_t dataSize);
```

- [ ] **Step 2: Implement AppendAudioState() in GBACore.cpp**

```cpp
// [PPSSPP-FORK] GBA Audio Parity
bool GBACore::AppendAudioState(std::vector<uint8_t> &buffer) const {
    auto *r = static_cast<struct mAudioResampler*>(resampler_);
    double ts = r ? r->timestamp : 0.0;

    GBAAudioStateHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = 0x414F4449;  // 'AUDI' little-endian
    h.version = 1;
    h.dcCapL = dcCapL_;
    h.dcCapR = dcCapR_;
    h.lowPassL = lowPassL_;
    h.lowPassR = lowPassR_;
    h.resamplerTimestamp = ts;
    h.stagingFill = (uint32_t)stagingFill_;
    h.totalSize = (uint32_t)(sizeof(h) + (stagingFill_ * 2 * sizeof(int32_t)));

    // Append header
    const uint8_t *hdr = reinterpret_cast<const uint8_t*>(&h);
    buffer.insert(buffer.end(), hdr, hdr + sizeof(h));

    // Append staging buffer data if any
    if (stagingFill_ > 0) {
        const uint8_t *stg = reinterpret_cast<const uint8_t*>(stagingBuffer_);
        buffer.insert(buffer.end(), stg, stg + stagingFill_ * 2 * sizeof(int32_t));
    }

    return true;
}
```

- [ ] **Step 3: Implement RestoreAudioState() in GBACore.cpp**

```cpp
// [PPSSPP-FORK] GBA Audio Parity
bool GBACore::RestoreAudioState(const uint8_t *data, size_t dataSize) {
    if (!data || dataSize < sizeof(GBAAudioStateHeader)) return false;

    GBAAudioStateHeader h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != 0x414F4449 || h.version != 1) return false;
    if (h.totalSize > dataSize) return false;

    dcCapL_ = h.dcCapL;
    dcCapR_ = h.dcCapR;
    lowPassL_ = h.lowPassL;
    lowPassR_ = h.lowPassR;
    stagingFill_ = h.stagingFill;

    if (stagingFill_ > 0) {
        size_t stgBytes = stagingFill_ * 2 * sizeof(int32_t);
        if (sizeof(h) + stgBytes <= dataSize) {
            memcpy(stagingBuffer_, data + sizeof(h), stgBytes);
        } else {
            stagingFill_ = 0;  // corrupt, discard
        }
    }

    // Restore resampler timestamp
    if (resampler_) {
        auto *r = static_cast<struct mAudioResampler*>(resampler_);
        r->timestamp = h.resamplerTimestamp;
    }

    // Clear pending mixed audio — will be regenerated
    if (resampleDest_) {
        mAudioBufferClear(static_cast<struct mAudioBuffer*>(resampleDest_));
    }
    audioStereoPairs_ = 0;

    // Reset filter accumulators if timestamp indicates fresh start
    if (h.resamplerTimestamp < 1.0) {
        dcCapL_ = 0.0f; dcCapR_ = 0.0f;
        lowPassL_ = 0.0f; lowPassR_ = 0.0f;
    }

    return true;
}
```

- [ ] **Step 4: Extend SaveStateToFile() and LoadStateFromFile()**

In `SaveStateToFile()`, after writing mGBA core state to the file buffer:
```cpp
	// [PPSSPP-FORK] GBA Audio Parity: append audio state after core state
	AppendAudioState(buffer);
	// (existing write-to-file code follows — same File::WriteDataToFile call)
```

In `LoadStateFromFile()`, after loading core state from the file, check for audio trailer:
```cpp
	// [PPSSPP-FORK] GBA Audio Parity: restore audio state from appended data
	size_t coreSize = GetStateSize();
	if (data.size() > coreSize + sizeof(GBAAudioStateHeader)) {
		const uint8_t *audioData = reinterpret_cast<const uint8_t*>(data.data()) + coreSize;
		size_t audioSize = data.size() - coreSize;
		RestoreAudioState(audioData, audioSize);
	} else {
		NOTICE_LOG(Log::SaveState, "[GBA] No audio state in save file (upgrade from older version)");
	}
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build build-final --target ppsspp 2>&1 | tail -20
```

Expected: No errors.

---

### Task 5: GBA Audio Debug Stats Overlay

**Goal:** Show GBA-specific audio diagnostics in the debug overlay, same as PSP has.

**Background:** The PSP audio debug overlay (from `StereoResampler::GetAudioDebugStats`) shows buffer latency, underruns/overruns, sample rates. GBA audio goes through the same StereoResampler so those stats already cover GBA too. But GBA-specific metrics are missing: sinc resampler state, filter state, staging buffer fill.

**Files:**
- Modify: `EmuCore/GBACore.h` — add `GetAudioDebugStats()` method
- Modify: `EmuCore/GBACore.cpp` — implement
- Modify: `UI/EmuScreen.cpp` or `UI/DebugOverlay.cpp` — integrate into debug display

- [ ] **Step 1: Add GetAudioDebugStats to GBACore.h**

```cpp
// [PPSSPP-FORK] GBA Audio Parity: debug stats for GBA audio pipeline
void GetAudioDebugStats(char *buf, size_t bufSize) const;
```

- [ ] **Step 2: Implement in GBACore.cpp**

```cpp
// [PPSSPP-FORK] GBA Audio Parity
void GBACore::GetAudioDebugStats(char *buf, size_t bufSize) const {
    if (!buf || bufSize == 0) return;

    auto *r = static_cast<struct mAudioResampler*>(resampler_);
    double resamplerPos = r ? r->timestamp : 0.0;

    snprintf(buf, bufSize,
        "GBA Audio:\n"
        "  Sample rate: %u Hz (native)\n"
        "  Target rate: %d Hz (output)\n"
        "  Staging fill: %zu / %d pairs\n"
        "  Resampler pos: %.1f\n"
        "  DC filter: L=%.2f R=%.2f\n"
        "  Low-pass filter: L=%.2f R=%.2f\n"
        "  Pending mixed: %zu pairs\n",
        coreSampleRate_,
        TARGET_RATE,
        stagingFill_, 64,
        resamplerPos,
        dcCapL_, dcCapR_,
        lowPassL_, lowPassR_,
        audioStereoPairs_);
}
```

- [ ] **Step 3: Integrate into `DrawAudioDebugStats()` in DebugOverlay.cpp**

The function is at `UI/DebugOverlay.cpp:69-93`. After the `__SasGetDebugStats` block (line ~89), add GBA audio stats:

```cpp
	__SasGetDebugStats(statbuf, sizeof(statbuf));
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 21, bounds.y + 31, bounds.w - left, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 20, bounds.y + 30, bounds.w - left, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

#ifdef PPSSPP_MULTICORE
	// [PPSSPP-FORK] GBA Audio Parity: GBA-specific audio debug stats
	if (g_gbaModeActive && g_activeCore) {
		EmuCore::GBACore *gba = static_cast<EmuCore::GBACore *>(g_activeCore);
		char gbaAudioBuf[1024];
		gba->GetAudioDebugStats(gbaAudioBuf, sizeof(gbaAudioBuf));
		ctx->Draw()->DrawTextRect(ubuntu24, gbaAudioBuf, bounds.x + 11, bounds.y + 151, bounds.w - 20, bounds.h - 150, 0xc0000000, FLAG_DYNAMIC_ASCII);
		ctx->Draw()->DrawTextRect(ubuntu24, gbaAudioBuf, bounds.x + 10, bounds.y + 150, bounds.w - 20, bounds.h - 150, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	}
#endif

	ctx->Draw()->SetFontScale(1.0f, 1.0f);
```

Note: `g_activeCore` (declared `extern` in `UI/EmuScreen.h`) is already used by `UI/PauseScreen.cpp` for the same purpose — gives access to the active GBA core from outside EmuScreen.

- [ ] **Step 4: Build and verify**

```bash
cmake --build build-final --target ppsspp 2>&1 | tail -20
```

Expected: No errors.

---

### Verification

- [ ] **Build check:** Build with `PPSSPP_MULTICORE=ON` (build-final) and `=OFF` — both must succeed

```bash
cmake --build build-final --target ppsspp 2>&1 | tail -5
```

- [ ] **Runtime check:** Load a GBA game, verify:
  1. Changing "Game Volume" slider changes GBA audio volume (Task 1)
  2. Fast-forward volume matches PSP behavior (Task 2)
  3. No audio quality regression (Task 3 — staging buffer is transparent)
  4. Save state → load state produces no audio click (Task 4)
  5. Debug overlay shows GBA audio stats (Task 5)

- [ ] **Sanity check for PSP-only builds:** All changes are in `#ifdef PPSSPP_MULTICORE` blocks or inside GBA-specific files. PSP-only build must not be affected.

```bash
cmake --build build-release --target ppsspp 2>&1 | tail -5
```
