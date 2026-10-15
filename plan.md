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

- [ ] `build.sh`: `-O3 -std=c++17 -march=native -pthread`, flag for debug
- [ ] `build.bat`: `cl /O2 /std:c++17 /arch:AVX2` (or `/arch:AVX512`), flag for debug
- [ ] Folders:
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
- [ ] `--help` print. Nothing else yet.

---

## Step 2 — SIMD layer

- [ ] `simd.hpp`: one type `vf` = 16 / 8 / 4 / 1 floats depending on build
  - AVX-512 (`__AVX512F__`), AVX2 (`__AVX2__` + FMA), SSE2, scalar
- [ ] Ops: load, store, add, mul, fma, exp approx, min, reduce sum
- [ ] Fast `exp` approx (energy kernel = many exp calls, hot path)
- [ ] Data layout SoA, 64-byte aligned, padded to SIMD width

---

## Step 3 — TGA writer + reader

- [ ] Uncompressed TGA, type 3 (gray) and type 2 (RGB/RGBA)
- [ ] Origin top-left flag set
- [ ] 3D: one file per slice `name_000.tga` ... and/or one atlas (slices stacked)
- [ ] Reader for own files only (for analyze tool)

---

## Step 4 — Baseline generator: Void and Cluster

Classic. Ulichney 1993. Make rank map → value = rank / N.

- [ ] Gaussian energy, toroidal wrap (tile seamless)
- [ ] Start pattern: random ~10% ones
- [ ] Phase 1: remove tightest cluster → rank down
- [ ] Phase 2: add into biggest void → rank up to half
- [ ] Phase 3: fill rest → rank up to N
- [ ] Energy map update incremental, SIMD (O(N) per step)
- [ ] Multi-channel: each channel own seed (simple version)
- [ ] Param `--sigma` (default 1.9)

---

## Step 5 — Quality analyzer

Need measure to prove "higher quality". Build before fancy generators.

- [ ] Own radix-2 FFT (pot size = easy). 2D + 3D.
- [ ] Power spectrum image (log scale) → TGA
- [ ] Radial average power → CSV
- [ ] Histogram flat check
- [ ] Low-freq energy score (lower = more blue)
- [ ] Channel cross-correlation (for 2/4 ch)
- [ ] 3D: per slice spatial score + per pixel temporal score
- [ ] CLI: `noisebuilder analyze in.tga`

---

## Step 6 — Better generator: swap optimizer

Higher quality than void-and-cluster. Georgiev & Fajardo 2016 style.

- [ ] Start: random permutation of 0..N-1 (histogram perfect always)
- [ ] Energy per pair: `exp(-|pi-pj|²/σi² - |vi-vj|^(d/2)/σs²)`, vector value for 2/4 ch
- [ ] Swap two pixels, keep if energy down (+ annealing option)
- [ ] Local window only (fast). SIMD over window.
- [ ] Threads: work on far-apart tiles at same time, deterministic order
- [ ] Can start from void-and-cluster result → converge faster

---

## Step 7 — Spatiotemporal blue noise (STBN)

Wolfe et al. 2022. Big win for raytrace + TAA.

- [ ] Volume W x H x T (T pot too)
- [ ] Each slice = blue noise in space
- [ ] Each pixel over time = blue noise in 1D
- [ ] Separate sigma space / time
- [ ] Wrap all 3 axes
- [ ] Scalar first, then 2 / 4 channel

---

## Step 8 — AO / raytrace variants

- [ ] Vec2 / unit vec3 / cosine-hemisphere direction noise (pack into 2/4 ch TGA)
- [ ] Low cross-correlation between channels

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
