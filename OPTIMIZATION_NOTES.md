# CodeX-Pet Optimization Notes

## Project Overview

Refactored `main.c` (1499 lines monolith) into 14 modular source files, optimized logic, simplified code, converted all comments to Chinese, and reduced the final exe from **~30KB+** to **10,240 bytes (10KB)**.

---

## Successes

### 1. Code Splitting

Split `main.c` into 7 modules by responsibility:

| File | Responsibility |
|------|---------------|
| `pet_ai.c/h` | AI state machine, trajectory, weighted transitions |
| `pet_io.c/h` | Directory scanning, WIC image loading |
| `pet_json.c/h` | JSON parsing |
| `pet_render.c/h` | Frame buffer rendering, scaling, compositing |
| `pet_wnd.c/h` | Window procedures, pet spawning |
| `pet_data.c` | Shared const data (frame counts, durations) |
| `pet_common.h` | Shared types, constants, extern declarations |

Key refactoring details:
- Merged RECT/TRI/POLY trajectories into unified polygon, CIRCLE/ELLIPSE/ARC into unified arc
- Weighted state transitions use compact `tbl[][]` / `dst[][]` lookup tables
- `draw_text_with_spacing` simplified to `DrawTextW` API

### 2. Dynamic Allocation to Shrink .data

Original code had static arrays `pets[64]` (~288KB) and `instances[256]` (~480KB) in `.data`, bloating the exe. Changed to `calloc()` at runtime, reducing `.data` from **137KB to <1KB**.

### 3. x86 (32-bit) + No-CRT Build

The final winning strategy: build as **32-bit x86** with **zero CRT dependency**.

Why x86 over x64:
- UPX 32-bit PE decompressor: **~2-3KB** overhead
- UPX 64-bit PE decompressor: **~8-10KB** overhead
- x86 instructions are generally shorter than x86-64
- Import address table entries are 4 bytes (vs 8 bytes on x64)
- WoW64 on Windows 11 x64 runs 32-bit executables transparently

CRT replacement (`crt_repl.c`):
| CRT Function | Replacement |
|-------------|-------------|
| `calloc/malloc/free` | `HeapAlloc/HeapFree` (kernel32) |
| `srand/rand` | Custom LCG (MSVC-compatible constants) |
| `time()` | `GetTickCount()` (kernel32) |
| `sin/cos/sqrt` | x87 FPU: `fsin`/`fcos`/`fsqrt` inline assembly |
| `memset/memcpy` | Custom byte-loop implementations |
| `strlen/wcslen/wcscpy/wcscmp/wcsrchr/strstr` | Custom implementations |

Result: only **5 DLL imports** (kernel32, user32, gdi32, ole32, msimg32), down from 11+.

### 4. Linker Optimizations

- `/MERGE:.rdata=.text` — merge read-only data into code section
- `/FIXED` — remove relocation section (saves ~1KB)
- `/OPT:REF /OPT:ICF` — eliminate unreferenced functions and merge identical COMDATs
- `/ENTRY:WinMain` — skip CRT startup entirely
- `/FILEALIGN:256` (MinGW) — reduce section alignment padding

### 5. UPX Compression

`upx --ultra-brute` achieved **15,872 -> 10,240 bytes** (35% reduction).

---

## Failures and Lessons Learned

### 1. MinGW nostartfiles + UPX = Crash on Windows 11

**Problem:** MinGW `-nostartfiles -nodefaultlibs` produced a working 20.9KB exe, but UPX-compressed version crashed on Windows 11.

**Root cause:** Windows 11 security mitigations (likely CET or modified PE loader behavior) interfere with UPX runtime decompression for certain PE structures. The MinGW nostartfiles build has a non-standard entry point flow that exacerbates this.

**Lesson:** UPX on Win11 is unreliable for non-standard PE layouts. A standard PE with proper CRT startup compresses and runs fine.

### 2. MinGW `-fno-builtin` and Math Dependency Hell

**Problem:** GCC 14.2 fuses `sinf()+cosf()` into `sincosf()`, which is undefined without libm. Adding `-fno-builtin` fixes sincosf but causes sinf/cosf/sqrtf to call libmsvcrt implementations that need `__mingw_raise_matherr` from libmingwex, which needs `__setusermatherr` from libmingw32... creating a dependency chain impossible to resolve with `-nodefaultlibs`.

**Lesson:** GCC's math builtin fusion makes it very hard to build without CRT on MinGW. MSVC's approach of using `sin`/`cos`/`sqrt` (not sincosf) is more modular.

### 3. MSVC x64 + UPX: Still Too Large

**Problem:** Even with MSVC x64 no-CRT + all optimizations (18.4KB uncompressed), UPX compresses to only 12KB due to the large 64-bit decompressor stub.

**Lesson:** For <10KB target on Win11, x64 + UPX is mathematically impossible given ~8KB decompressor overhead. x86 + UPX is the only viable path.

### 4. MSVC LTCG vs Custom memset

**Problem:** MSVC `/GL` (LTCG) conflicts with custom `memset`/`memcpy` implementations — error C2268: "memset is a compiler intrinsic that cannot be used with /GL".

**Lesson:** When replacing CRT intrinsics, don't use `/GL` (whole program optimization). The size benefit of LTCG is negligible anyway for small projects.

### 5. 176KB .data Bloat from Static Arrays

**Problem:** First successful compilation produced a 176KB+ exe because `pets[64]` and `instances[256]` were static arrays in `.data`.

**Lesson:** On Windows PE, `.data` is fully embedded in the exe. Large arrays must be dynamically allocated with `calloc()`.

### 6. `___chkstk_ms` Stack Overflow

**Problem:** `render_scaled_frame_to` used a 159KB stack-allocated buffer for scaling, causing linker errors.

**Lesson:** Large buffers should always be heap-allocated. Stack on Windows is typically 1MB, but the linker's stack probe function (`__chkstk`) adds dependency bloat.

---

## Build Size Timeline

| Stage | Size | Method |
|-------|------|--------|
| Original main.c | ~30KB+ | MinGW default |
| Split + dynamic alloc | 20.9KB | MinGW nostartfiles |
| MSVC x64 no-CRT | 18.4KB | MSVC /ENTRY:WinMain /NODEFAULTLIB |
| MSVC x86 no-CRT | 15.4KB | Same but x86 + /FIXED |
| **Final (x86 no-CRT + UPX)** | **10,240 B** | UPX --ultra-brute |

---

## Files Changed

- `src/main.c` — Entry point, message loop, resource cleanup
- `src/crt_repl.c` — CRT replacement (new)
- `src/pet_ai.c/h` — AI module (new, extracted from main.c)
- `src/pet_io.c/h` — I/O module (new, extracted from main.c)
- `src/pet_json.c/h` — JSON module (new, extracted from main.c)
- `src/pet_render.c/h` — Render module (new, extracted from main.c)
- `src/pet_wnd.c/h` — Window module (new, extracted from main.c)
- `src/pet_data.c` — Shared const data (new, extracted from main.c)
- `src/pet_common.h` — Shared definitions (new, extracted from main.c)
- `Makefile` — Updated for MinGW nostartfiles build
- `build_msvc.bat` — MSVC x86 no-CRT + UPX build script (new)
