# GBA Graphics Quality Improvement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve GBA graphics quality in PPSSPP fork by: GPU-accelerated pixel conversion (remove CPU loop), integer scaling for crisp pixels, and PPSSPP post-processing shader support.

**Architecture:** All changes in `EmuCore/GBACore.h/.cpp`, `GPU/Common/PresentationCommon.cpp`, and GBA Settings Screen. Zero upstream files touched.

**Tech Stack:** C++17, mGBA library, Thin3D, GLSL shaders, CMake

---

## Phase 1 — GPU Pixel Conversion

Replace the CPU-bound pixel-by-pixel conversion loop (`for y for x`) with direct GPU upload of raw mGBA framebuffer, converting BGR→RGB in the shader.

**Rationale:** CPU does 38,400 iterations per frame extracting RGB bits. GPU can do this in a single shader pass — faster and frees CPU cycles.

**Pipeline change:**

```
SEBELUM:
mGBA rawBuffer (M_RGB5_TO_BGR8) → CPU loop → videoBuffer (RGBA8888) → upload → GPU

SESUDAH:
mGBA rawBuffer (M_RGB5_TO_BGR8) → upload (RAW) → GPU → shader unpack → screen
```

### Task 1.1 — Remove CPU loop, upload raw buffer directly

**Files:**
- Modify: `EmuCore/GBACore.cpp`
- Modify: `EmuCore/GBACore.h`

- [x] In `RunFrame()`: delete the CPU `for(y) for(x)` conversion loop entirely
- [x] In `InitRendering()`: change blend to opaque (`{ false, 0xF }`) — raw buffer has alpha=0
- [x] In `Render()`: upload `rawVideoBuffer_` instead of `videoBuffer_` via `UpdateTextureLevels`
- [x] Keep `R8G8B8A8_UNORM` format (data byte order R,G,B,0 maps correctly to RGBA channels)
- [x] In `SaveStateToFile()`: convert raw→RGBA inline for PNG thumbnail
- [x] Update `GetVideoBuffer()` to return `rawVideoBuffer_`

```cpp
// RunFrame() — no CPU loop, just debug log
// Capture video — mGBA rendered into rawVideoBuffer_ via setVideoBuffer
// No CPU conversion needed: upload raw buffer directly to GPU
// (rawVideoBuffer_ byte order: R,G,B,0 in little-endian memory)

// InitRendering() — opaque blend
// [PPSSPP-FORK] MultiCore: opaque blend — raw GBA buffer has no alpha (byte3=0)
BlendState *blend = draw->CreateBlendState({ false, 0xF });

// Render() — upload raw buffer directly
const uint8_t *data = reinterpret_cast<const uint8_t *>(rawVideoBuffer_);
draw->UpdateTextureLevels(gbaTexture_, &data, nullptr, 1);
```

- [x] **Commit**: `feat(gba-gfx): Phase 1.1 — GPU pixel conversion, remove CPU loop`

### Task 1.2 — (SKIPPED — not needed)

Original plan: custom shader with BGR→RGB swizzle. Not needed because:
- We kept `R8G8B8A8_UNORM` format (not `B8G8R8A8`)
- Data byte order R,G,B,0 maps correctly to RGBA
- Disabled alpha blending instead of forcing alpha=1.0 in shader
- Simpler: no backend-specific shader code needed

### Task 1.3 — Remove stale videoBuffer_ code

**Files:**
- Modify: `EmuCore/GBACore.h`

- [x] Remove `videoBuffer_` member declaration (stale, no longer filled)
- [x] Remove stale comment about RGBA8888 format

- [x] **Commit**: `feat(gba-gfx): Phase 1.3 — cleanup stale videoBuffer_ member`

---

## Phase 2 — Integer Scaling

Add integer scaling option so GBA 240×160 pixels scale by exact integer factor (1×, 2×, 3×, 4×) instead of stretching to fill the screen. Results in crisp pixel-art rendering with letterbox borders.

```
Normal:   240×160 → stretch to viewport (blurry)
Integer 2×: 480×320 → centered in viewport + black borders (crisp)
Integer 3×: 720×480 → centered + borders
Integer 4×: 960×640 → centered + borders (might not fit on small screens)
```

**Config:**

| Setting | Key | Values | Default |
|---------|-----|--------|---------|
| Integer scale | `iGBAIntegerScale` | 0=off, 1=auto, 2=2×, 3=3×, 4=4× | 0 (off) |
| Auto mode | — | Choose largest integer that fits viewport | Only when `iGBAIntegerScale=1` |

### Task 2.1 — Add config

**Files:**
- New: `EmuCore/Config.h` — add to GBA config section
- Modify: `EmuCore/GBACore.h` — add scale state

- [ ] Add `int iGBAIntegerScale = 0;` to GBA config (in `EmuCore/Config.h` or `Core/Config.h`)
- [ ] Add `int currentScale_ = 1;` to `GBACore` class for computed scale
- [ ] **Commit**: `feat(gba-gfx): Phase 2.1 — add integer scale config`

### Task 2.2 — Update GetRenderRect() for integer scaling

**Files:**
- Modify: `EmuCore/GBACore.cpp` — `GetRenderRect()`

- [ ] Read `g_Config.iGBAIntegerScale`
- [ ] If 0: use current behavior (stretch to fill)
- [ ] If 1 (auto): compute `scale = floor(min(viewW / GBA_WIDTH, viewH / GBA_HEIGHT))`, min 1
- [ ] If 2/3/4: use fixed `scale = iGBAIntegerScale`
- [ ] Compute scaled `w = GBA_WIDTH * scale`, `h = GBA_HEIGHT * scale`
- [ ] Center in viewport: `x = (viewW - w) * 0.5f`, `y = (viewH - h) * 0.5f`
- [ ] Update `gbaScaleUniform_` if shader needs it

```cpp
// GetRenderRect() logic
int scale = 0;
if (g_Config.iGBAIntegerScale == 0) {
    // stretch to fill (current behavior)
} else {
    if (g_Config.iGBAIntegerScale == 1) {  // auto
        scale = std::min((int)(viewW / GBA_WIDTH), (int)(viewH / GBA_HEIGHT));
        if (scale < 1) scale = 1;
    } else {
        scale = g_Config.iGBAIntegerScale;
    }
    w = GBA_WIDTH * scale;
    h = GBA_HEIGHT * scale;
    x = (viewW - w) * 0.5f;
    y = (viewH - h) * 0.5f;
}
```

- [ ] **Commit**: `feat(gba-gfx): Phase 2.2 — integer scaling in GetRenderRect()`

### Task 2.3 — Add toggle in GBA Settings Screen

**Files:**
- Modify: `UI/GBASettingsScreen.cpp`

- [ ] Add `PopupSliderChoice` for integer scale (Off / Auto / 2× / 3× / 4×)
- [ ] Add description text explaining the effect
- [ ] **Commit**: `feat(gba-gfx): Phase 2.3 — integer scale UI toggle`

---

## Phase 3 — Apply PPSSPP Post-Processing Shaders

Reuse PPSSPP's existing post-processing shader system (FXAA, CRT, 5xBR, FSR, sharpen, scanlines, etc.) for GBA output.

**Pipeline change:**

```
SEBELUM:
GBA → render → screen

SESUDAH:
GBA → render → offscreen texture → postprocess chain → screen
                                     ├─ FXAA, CRT, 5xBR, FSR...
                                     └─ LCD grid shader (Phase 4)
```

**Key insight:** PPSSPP's `PresentationCommon` already handles post-processing for PSP. GBA currently bypasses it entirely (`RenderGBA()` renders direct to screen). We need to route GBA through the same pipeline.

### Task 3.1 — Create offscreen framebuffer for GBA

**Files:**
- Modify: `EmuCore/GBACore.h` — add framebuffer members
- Modify: `EmuCore/GBACore.cpp` — render to framebuffer

- [ ] Add `Draw::Framebuffer *gbaOffscreen_ = nullptr;` to GBACore
- [ ] Create framebuffer in `InitRendering()`
- [ ] In `Render()`, set render target to offscreen framebuffer
- [ ] After rendering, bind offscreen texture as input for postprocess
- [ ] **Commit**: `feat(gba-gfx): Phase 3.1 — GBA offscreen framebuffer`

### Task 3.2 — Route GBA through PresentationCommon postprocess

**Files:**
- Modify: `GPU/Common/PresentationCommon.cpp` — add GBA entry point
- Modify: `EmuCore/GBACore.cpp` — call postprocess

- [ ] Add method `RenderWithPostprocess(offscreenTex)` to PresentationCommon
- [ ] Or create a standalone postprocess helper (avoids touching PresentationCommon)
- [ ] Apply `g_Config.vPostShaderNames` chain to GBA offscreen texture
- [ ] **Commit**: `feat(gba-gfx): Phase 3.2 — GBA post-processing pipeline`

### Task 3.3 — Share config or create GBA-specific shader config

**Files:**
- Modify: `Core/Config.h`

- [ ] Option A: Share `g_Config.vPostShaderNames` with PSP (simpler, user picks 1 shader for both)
- [ ] Option B: Create `g_Config.vPostShaderNamesGBA` (independent, more complex)
- [ ] **Recommendation**: Option A first (PSP shaders already configured), Option B if separate needed

- [ ] **Commit**: `feat(gba-gfx): Phase 3.3 — post-process config`

### Task 3.4 — Shader selector in GBA Settings Screen

**Files:**
- Modify: `UI/GBASettingsScreen.cpp`

- [ ] Add "Post-processing shader" selector (reuse existing PPSSPP shader chooser)
- [ ] Show preview of active shader
- [ ] **Commit**: `feat(gba-gfx): Phase 3.4 — shader UI`

---

## Phase 4 — GBA LCD Shader (Custom)

Create a custom shader that simulates the original GBA LCD screen characteristics:

- **LCD grid** — visible pixel grid lines (GBA screen has distinct sub-pixel pattern)
- **Color bleed** — slight color bleeding between adjacent pixels
- **Ghosting** — motion blur from LCD response time
- **Color tint** — GBA LCD has a specific greenish/grayish tint

### Task 4.1 — Write gba_lcd.fsh

**Files:**
- New: `assets/shaders/gba_lcd.fsh`
- New: `assets/shaders/gba_lcd.vsh`

- [ ] Write shader with:
  - LCD grid overlay (thin lines between pixels)
  - Optional color tint matrix
  - Optional ghosting (requires previous frame buffer)
- [ ] Register in `assets/shaders/defaultshaders.ini`
- [ ] **Commit**: `feat(gba-gfx): Phase 4.1 — LCD shader`

### Task 4.2 — Install shader in build

**Files:**
- Modify: `CMakeLists.txt` or asset install script

- [ ] Ensure `gba_lcd.fsh` is included in build assets
- [ ] **Commit**: `feat(gba-gfx): Phase 4.2 — shader build integration`

---

## Rollback Plan

If any phase causes regression:

```bash
git checkout feature/lan-sync -- EmuCore/GBACore.h EmuCore/GBACore.cpp
```

## Key Risks

| Risk | Mitigation |
|------|-----------|
| `B8G8R8A8_UNORM` not supported on all GPUs | Fallback to CPU conversion if format unsupported |
| Post-processing adds latency on mobile | Only enable when explicitly chosen by user |
| Shader compilation fails | Graceful fallback to passthrough shader |
| Integer scaling + post-process conflict | Post-process runs on upscaled buffer, not native res |
