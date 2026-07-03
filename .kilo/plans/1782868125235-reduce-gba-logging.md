# Plan: Reduce Excessive GBA Logging Frequency

## Problem
GBA logs terlalu sering (setiap frame/milidetik) mengganggu output:
- `[MULTICORE] PreRender: non-PSP core, skipping ProcessGameBoot` — setiap frame
- `[GBA] renderUI: root_ exists...` — setiap frame

## Solution
Ubah ke counter-based logging (hanya log beberapa frame pertama atau secara periodik).

## Changes Required

### File: UI/EmuScreen.cpp

| Line | Current | Fix |
|------|---------|-----|
| 2068 | `DEBUG_LOG(...PreRender...)` every call | Change to `static int count` and only log first 5 frames |
| 2472 | `NOTICE_LOG(...renderUI...)` every call | Change to `static int frameCount` and only log first 5 frames |

### Implementation
```cpp
// Line 2068 - PreRender
static int preRenderCount = 0;
if (IsGBA() && ++preRenderCount <= 5) {
    DEBUG_LOG(Log::System, "[MULTICORE] PreRender: non-PSP core, skipping ProcessGameBoot");
}

// Line 2472 - renderUI  
static int renderUICount = 0;
if (renderUICount++ < 5) {
    NOTICE_LOG(Log::System, "[GBA] renderUI: root_ exists...");
}
```

## Validation
- Build Linux SDL: `./b.sh --release`
- Build without MULTICORE: `cmake .. && make -j6`
- Both must succeed