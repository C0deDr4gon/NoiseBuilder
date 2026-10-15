# NoiseBuilder Plan

CLI tool. Make blue noise textures. For AO, raytracing, denoise sampling.
C++17. CMake. Linux + Windows. No build/test until user say go.

Check box after each step. User check before next step.

---

## Step 0 — Decide stuff (user answer)

- [ ] Texture sizes: 64, 128, 256? Power of two only?
- [ ] Channels: 1 (R), 2 (RG), 4 (RGBA independent)?
- [ ] Output format: PNG 8-bit, PNG 16-bit, EXR float, raw .bin?
- [ ] Need 3D / spatiotemporal (many slices over time)? Yes = big win for raytrace + TAA.
- [ ] Need GPU (compute) or CPU only fine?
- [ ] Third party libs OK? (stb_image_write = 1 header. tinyexr = 1 header.)

---

## Step 1 — Skeleton

- [ ] `CMakeLists.txt` (C++17, warnings on, Release flags, MSVC + GCC + Clang)
- [ ] Folders:
  ```
  src/
    main.cpp        // CLI parse, call generator
    cli.hpp/.cpp    // args
    image.hpp       // simple 2D/3D float buffer
    rng.hpp         // seeded fast RNG (pcg32)
    gen/            // generators
    io/             // writers
    analyze/        // quality check
  third_party/      // stb, tinyexr
  tests/
  ```
- [ ] `--help` print. Nothing else yet.

---

## Step 2 — Baseline generator: Void and Cluster

Classic. Ulichney 1993. Make rank map → threshold to any value.

- [ ] Gaussian energy filter, toroidal wrap (tile seamless)
- [ ] Initial binary pattern (random ~10% ones)
- [ ] Phase 1: remove tightest cluster → rank down
- [ ] Phase 2: add to biggest void → rank up to half
- [ ] Phase 3: fill rest → rank up to full
- [ ] Output rank / N → value 0..1 (uniform histogram)
- [ ] Speed: keep energy map, update incremental (O(N) per swap, not O(N²))
- [ ] Param: `--sigma` (default 1.5)

---

## Step 3 — Writers

- [ ] PNG 8-bit + 16-bit (stb_image_write)
- [ ] EXR float (tinyexr) — optional
- [ ] Raw binary dump (float / uint8 / uint16)
- [ ] 3D output = slices `name_000.png` ... or one tall atlas

---

## Step 4 — Quality analyzer (need this to prove "higher quality")

Without measure, no "better". Build before fancy generators.

- [ ] 2D DFT (simple, or pocketfft single header)
- [ ] Power spectrum image (log scale) → write PNG
- [ ] Radial average power curve → write CSV
- [ ] Histogram check (must be flat/uniform)
- [ ] Low-freq energy score (lower = more blue)
- [ ] Tile seam check
- [ ] CLI: `noisebuilder analyze in.png`

---

## Step 5 — Better generator: energy swap optimizer

Higher quality than void-and-cluster. Georgiev & Fajardo 2016 style.

- [ ] Start: random permutation of values 0..N-1 (histogram perfect from start)
- [ ] Energy: sum over pixel pairs `exp(-|pi-pj|²/σi² - |vi-vj|^(d/2)/σs²)`
- [ ] Swap two pixels, keep if energy go down (simulated annealing option)
- [ ] Only local window for energy (fast)
- [ ] Multi-channel: vector values per pixel → channels decorrelated
- [ ] Seed from void-and-cluster result → converge faster
- [ ] Multithread (std::thread), deterministic by seed

---

## Step 6 — Spatiotemporal blue noise (STBN)

Big quality win for raytracing + TAA. Wolfe et al. 2022 (NVIDIA).

- [ ] 3D volume XY × T
- [ ] Each 2D slice = blue noise in space
- [ ] Each pixel over time = blue noise in 1D
- [ ] Separate sigma for space + time
- [ ] Wrap in all 3 axes
- [ ] Scalar version first. Vector (unit vec / cosine hemisphere) after.

---

## Step 7 — Special variants (for AO / raytrace)

- [ ] Vec2 / Vec3 / unit-sphere / cosine-hemisphere direction noise
- [ ] Rank-1 / multi-channel with low cross-correlation
- [ ] Optional: tile with 1D + 2D low-discrepancy blend (golden ratio offset per frame doc)

---

## Step 8 — CLI final

```
noisebuilder gen --algo vc|swap|stbn --size 128 --depth 64 \
                 --channels 1 --seed 42 --sigma 1.5 \
                 --format png16 --out blue.png
noisebuilder analyze blue.png --out report/
```

- [ ] Arg validation, nice errors
- [ ] Progress print
- [ ] Deterministic same seed = same file on Linux + Windows

---

## Step 9 — Tests

- [ ] Unit: RNG determinism, toroidal distance, histogram uniform
- [ ] Golden: small 16x16 output hash stable
- [ ] Quality gate: low-freq energy below threshold
- [ ] CI: GitHub Actions Linux (gcc, clang) + Windows (MSVC)

---

## Step 10 — Docs

- [ ] README: build, usage, pictures of spectrum
- [ ] How to use in shader (tile, offset per frame, golden ratio)

---

## Rules

- No build, no test, no run until user say.
- One step at a time. User check. Then next.
- Keep code small. No big deps.
