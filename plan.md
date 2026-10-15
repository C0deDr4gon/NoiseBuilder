# NoiseBuilder Plan

CLI tool. Make blue noise textures. For AO, raytracing, denoise sampling.
C++17. Linux + Windows. No build/test until user say go.

Check box after each step. User check before next step.

---

## Decided

- Build: `build.sh` (Linux, g++/clang++) + `build.bat` (Windows, MSVC `cl`). No CMake.
- Deps: none. All own code. Only std lib.
- Output: own `.tga` writer.
  - 1 ch → 8-bit grayscale TGA
  - 2 ch → 24-bit TGA, R G used, B = 0 (TGA have no 2-channel type)
  - 4 ch → 32-bit RGBA TGA
- Channels: 1, 2, 4.
- Size: power of two only (16, 32, 64, 128, 256, ...).
- 3D over time (spatiotemporal): yes.
- CPU only. Wide SIMD: AVX-512 / AVX2 / SSE2 + scalar fallback.
- Test here only small (32x32, few slices). Big ones user build on PC.

---

## Step 1 — Skeleton

- [x] `build.sh`: `-O3 -std=c++17 -march=native -pthread`, flag for debug
- [x] `build.bat`: `cl /O2 /std:c++17 /arch:AVX2` (or `/arch:AVX512`), flag for debug
- [x] Folders (only main, cli, image, rng now. rest come in own step):
  ```
  build.sh
  build.bat
  src/
    main.cpp        // entry, call cli
    cli.hpp/.cpp    // arg parse
    image.hpp       // float buffer W x H x D x C
    rng.hpp         // pcg32, seeded
    simd.hpp        // SIMD wrapper (pick widest at compile time)
    thread_pool.hpp // tiny pool, std::thread
    gen/            // generators
    io/tga.hpp/.cpp // tga writer (+ reader for analyze)
    analyze/        // fft, spectrum, scores
  tests/
  ```
- [x] `--help` print. Nothing else yet.

---

## Step 2 — SIMD layer

- [x] `simd.hpp`: one type `vf` = 16 / 8 / 4 / 1 floats depending on build
  - AVX-512 (`__AVX512F__`), AVX2 (`__AVX2__` + FMA), SSE2, scalar
- [x] Ops: load, store, add, mul, fma, exp approx, min, reduce sum
- [x] Fast `exp` approx (energy kernel = many exp calls, hot path)
- [x] Data layout SoA, 64-byte aligned, padded to SIMD width
- [x] Note: FMA paths (AVX2/512) vs SSE2/scalar give tiny float diff

---

## Step 3 — TGA writer + reader

- [x] Uncompressed TGA, type 3 (gray) and type 2 (RGB/RGBA)
- [x] Origin top-left flag set
- [x] 3D: one file per slice `name_000.tga` ... or one atlas (slices stacked)
- [x] Reader for own files only (for analyze tool)

---

## Step 4 — Baseline generator: Void and Cluster

Classic. Ulichney 1993. Make rank map → value = rank / N.

- [x] Gaussian energy, toroidal wrap (tile seamless)
- [x] Start pattern: random ~10% ones
- [x] Phase 1: remove tightest cluster → rank down
- [x] Phase 2: add into biggest void → rank up to half
- [x] Phase 3: fill rest → rank up to N (same pick as phase 2 with toroidal kernel, merged)
- [x] Energy map update incremental, SIMD, only rows within kernel radius + per-row min/max cache
- [x] Multi-channel + 3D slices: each own rng stream, run on threads (`thread_pool.hpp`)
- [x] `gen --algo vc` wired in main, `--atlas` flag
- [x] Param `--sigma` (default 1.9)

---

## Step 5 — Quality analyzer

Need measure to prove "higher quality". Build before fancy generators.

- [x] Own radix-2 FFT (pot size = easy). 2D + 3D.
- [x] Power spectrum image (log scale) → TGA
- [x] Radial average power → CSV
- [x] Histogram flat check
- [x] Low-freq energy score (lower = more blue)
- [x] Channel cross-correlation (for 2/4 ch)
- [x] 3D: per slice spatial score + per pixel temporal score
- [x] CLI: `noisebuilder analyze in.tga [--depth N] [--atlas] [--out dir]`
- [x] `gen --report dir` = analyze right after gen (float data, no 8-bit loss)
- [x] Power normalized: white noise = 1.0 everywhere

---

## Step 6 — Better generator: swap optimizer

Higher quality than void-and-cluster. Georgiev & Fajardo 2016 style.

- [x] Start: random permutation of 0..N-1 (histogram perfect always)
- [x] Energy per pair: `exp(-|pi-pj|²/σi² - |vi-vj|^(d/2)/σs²)`, vector value for 2/4 ch
- [x] Swap two pixels, keep if energy down (+ annealing option)
- [x] Local window only, radius 3·σ (fast). SIMD over window, vector exp.
- [x] Threads: 2x2-colored blocks (side > window radius), same color in parallel, grid shifted randomly each round, rng per (round, block) → same result any thread count
- [x] `--init vc` start from void-and-cluster
- [x] CLI: `--algo swap --sigma-v --iters --temp --init`

---

## Step 7 — Spatiotemporal blue noise (STBN)

Wolfe et al. 2022. Big win for raytrace + TAA.

- [x] Volume W x H x T (T pot too)
- [x] Each slice = blue noise in space
- [x] Each pixel over time = blue noise in 1D
- [x] Separate sigma space / time
- [x] Wrap all 3 axes
- [x] 1 / 2 / 4 channel (vector value, same as swap)
- [x] Built on swap optimizer: swaps stay in one slice (per-slice histogram perfect), energy = spatial pairs in slice + temporal pairs same pixel
- [x] CLI: `--algo stbn --depth N --sigma-t --time-weight`

---

## Build results (here, 4 cores, AVX-512)

- Builds clean, no warnings. SSE2 / AVX2 / AVX-512 all build + same scores.
- Same output for 1 vs 4 threads (swap, stbn, stbn-swap).
- Scores (lowfreq, lower = better, white = 1.0):

| algo | size | space | time |
|---|---|---|---|
| vc | 64² | 0.0003 | - |
| swap (tuned) | 64² | 0.0057 | - |
| swap 4 ch (tuned, was 0.029) | 64² | 0.0038 | - |
| vc slices | 32²x16 | 0.0003 | 1.01 (white) |
| **stbn (vc 3D)** | 32²x16 | **0.0013** | **0.056** |
| stbn 64²x32 | 64²x32 | 0.0011 | 0.044 |
| stbn-swap rand | 32²x16 | 0.017 | 0.20 |
| stbn-swap init vc | 32²x16 | 0.0026 | 0.077 |

- [x] stbn now = void and cluster in 3D (cross kernel: space gauss in slice + time gauss same pixel), slices step together → per-slice flat histogram. 50x faster than swap version.
- [x] old swap stbn kept as `stbn-swap`, `--init vc` starts it from stbn.
- [x] Tuned defaults: stbn sigma-t 1.5. swap sigma 2.5, sigma-v 4.0.
- Note: channel cross-corr (pearson) is fixed by init for swap (whole pixel vectors move). Not a real quality metric for swap.
- Note: vc best for per-channel quality. swap/stbn-swap only for joint vector energy, analyzer doesn't measure that yet.

---

## Step 8 — AO / raytrace variants

- [x] `--dist disk|circle|sphere|hemi|coshemi` (swap + stbn-swap)
- [x] Init = Fibonacci point set (near perfect spread), shuffled. Swaps move whole vectors → distribution exact
- [x] Vector distance energy (C = 2 or 3, power C/2)
- [x] 3 ch (vec3) → 24-bit RGB TGA. Encode v*0.5+0.5, decode tex*2-1, z up
- [x] Checked: unit length 1 ± 0.006 (8-bit limit), hemi mean z 0.50, coshemi mean z 0.667
- [x] Blue: per-channel lowfreq 0.15-0.4 (it 1) → 0.001-0.009 (it 300), 64²
- [x] stbn-swap coshemi 32²x16: space 0.009, time 0.07 (z: 0.028 / 0.26, weakest)
- Dropped: "low cross-correlation" item. Pearson fixed by init for swap, not useful.

---

## Step 9 — CLI final

```
noisebuilder gen --algo vc|swap|stbn --size 128 --depth 64 \
                 --channels 1|2|4 --seed 42 --sigma 1.9 \
                 --threads 0 --out blue.tga
noisebuilder analyze blue.tga --out report/
```

- [ ] Arg check, nice error (not pot = error)
- [ ] Progress print
- [ ] Print which SIMD path built in
- [ ] Same seed = same file on Linux + Windows (careful: fast exp + FMA must match, or accept tiny diff)

---

## Step 10 — Tests

- [ ] Tiny test exe, no framework. `build.sh test`
- [ ] RNG determinism, toroidal distance, histogram uniform
- [ ] SIMD path == scalar path (within epsilon)
- [ ] 32x32 (and 32x32x8 3D) quality gate: low-freq energy below threshold
- [ ] TGA write → read round trip

---

## Step 11 — Docs

- [ ] README: build, usage, spectrum pictures
- [ ] Shader use: tile, offset per frame, golden ratio

---

## Rules

- No build, no test, no run until user say.
- One step at a time. User check. Then next.
- Here: only small sizes (32x32). Big on user PC.
- No dependencies. Keep code small.
