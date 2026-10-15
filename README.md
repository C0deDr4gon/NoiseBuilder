# NoiseBuilder

CLI tool. Makes blue noise textures for AO, raytracing, shadows, TAA, dithering.

- C++17, no dependencies. Linux + Windows.
- CPU only, SIMD (AVX-512 / AVX2 / SSE2 / scalar), threaded.
- Output: own uncompressed `.tga` (8-bit). Sizes power of two.
- Tiles seamless (wraps in x, y, and time).

## Build

Linux (g++ or clang++):

```
./build.sh              # release -> bin/noisebuilder
./build.sh debug
./build.sh test         # -> bin/noisebuilder_tests
CXX=clang++ ./build.sh
ARCH=x86-64-v3 ./build.sh   # AVX2 build for other machines (default: -march=native)
```

Windows (MSVC, from "x64 Native Tools Command Prompt"):

```
build.bat                     # release, AVX2
build.bat release avx512
build.bat release sse2
build.bat test                # -> bin\noisebuilder_tests.exe
```

`noisebuilder version` prints which SIMD path is in.

## Algorithms

| `--algo` | What | Use for |
|---|---|---|
| `vc` | Void and cluster (Ulichney 1993). Fast. Best per-channel quality. | 2D blue noise, 1/2/4 independent channels |
| `stbn` | Void and cluster in 3D. Each slice blue in space, each pixel blue over time. | Per-frame noise with TAA / temporal accumulation |
| `swap` | Swap optimizer (Georgiev & Fajardo 2016). Channels optimized as one vector. | Vector noise, directions (`--dist`) |
| `stbn-swap` | `swap` + time. | Directions that change per frame (AO rays) |

Every slice of every channel holds each value exactly once → perfectly flat histogram.

### Vector / direction noise (`swap`, `stbn-swap`)

| `--dist` | Value | Channels |
|---|---|---|
| `uniform` | per channel 0..1 | `--channels` |
| `disk` | vec2 in unit disk | 2 (RG) |
| `circle` | vec2 unit direction | 2 (RG) |
| `sphere` | vec3 unit sphere | 3 (RGB) |
| `hemi` | vec3 uniform hemisphere, z up | 3 (RGB) |
| `coshemi` | vec3 cosine hemisphere, z up | 3 (RGB) |

Vectors stored as `v * 0.5 + 0.5`. Decode `v = tex * 2 - 1`. 8-bit → length 1 ± 0.006, normalize in shader if needed.

## Usage

```
noisebuilder gen --size 128 --out bn.tga
noisebuilder gen --size 128 --channels 4 --out bn4.tga
noisebuilder gen --algo stbn --size 128 --depth 64 --out st.tga          # st_000.tga .. st_063.tga
noisebuilder gen --algo stbn --size 128 --depth 64 --atlas --out st.tga  # one 128 x 8192 file
noisebuilder gen --algo stbn-swap --dist coshemi --size 64 --depth 32 --init vc --out ao.tga
noisebuilder analyze st.tga --depth 64 --out report
```

Common options:

| Option | Default | |
|---|---|---|
| `--size N` | 64 | width = height, power of two |
| `--depth N` | 1 | slices (time), power of two |
| `--channels 1\|2\|4` | 1 | |
| `--seed N` | 1 | same seed = same file (any thread count) |
| `--sigma F` | vc/stbn 1.9, swap 2.5 | spatial energy sigma |
| `--threads N` | 0 = all cores | |
| `--atlas` | off | 3D: one stacked file instead of slices |
| `--report dir` | off | run analyze after gen |

`stbn` / `stbn-swap`: `--sigma-t` (time sigma, stbn 1.5), `--time-weight` (time vs space, 1.0).
`swap` / `stbn-swap`: `--iters` (100), `--sigma-v` (4.0), `--temp` (anneal, 0), `--init rand|vc`, `--dist`.

`noisebuilder help` lists everything.

## Quality check

`analyze` (or `gen --report dir`) prints per channel:

- `lowfreq` — spatial power at low frequencies. White noise = 1.0. Lower = more blue.
- `highfreq` — power at high frequencies. Blue noise ~1.2.
- `t-lowfreq` — 3D only. Power over time at low frequencies (incl. pixel mean). Lower = better. Needs depth >= 16 to mean much.
- `hist` — min..max count per byte value. Equal = flat.
- `seam` — diff across wrap edge / inside. ~1.0 = seamless.

Files: `chN_spectrum.tga` (log power, center = DC), `chN_radial.csv`, `chN_temporal.csv`.

Measured (32x32, 16 slices):

| | space | time |
|---|---|---|
| `vc` slices | 0.0003 | 1.01 (white) |
| `stbn` | 0.0013 | 0.044 |
| `stbn-swap --init vc` | 0.0026 | 0.056 |

## Shader use

**2D, still image** — tile by pixel:

```hlsl
float n = BlueNoise.Load(int3(pixel & (SIZE - 1), 0)).r;
```

**2D, per frame (no stbn)** — offset value by golden ratio each frame. Stays blue in space, low discrepancy over time:

```hlsl
float n = frac(BlueNoise.Load(int3(pixel & (SIZE - 1), 0)).r + frame * 0.61803398875);
```

Can also offset the pixel coord by a random per-frame shift, but that makes time white.

**stbn** — use slice `frame % DEPTH`. No golden ratio offset (it breaks the time blueness):

```hlsl
// texture array or 3D texture with DEPTH slices
float n = BlueNoiseArray.Load(int4(pixel & (SIZE - 1), frame & (DEPTH - 1), 0)).r;
```

Atlas file: slice z = rows `z * SIZE .. z * SIZE + SIZE - 1`.

**Cosine hemisphere AO rays** (`--dist coshemi`):

```hlsl
float3 d = BlueNoiseArray.Load(int4(pixel & (SIZE - 1), frame & (DEPTH - 1), 0)).rgb * 2 - 1;
d = normalize(d);                       // z up, tangent space
float3 ray = d.x * T + d.y * B + d.z * N;
```

**Many samples per pixel** — use different channels (4 ch texture) or different textures (other `--seed`). Don't reuse one channel with an offset.

**Sampling notes**
- Load / point sampling only. Never filter or mip blue noise.
- Store as linear (UNORM), not sRGB.
- Byte = round(v * 255), read back as byte / 255 (UNORM, same as libtex). Ranks map so every byte is used N / 256 times.

## Tests

```
./build.sh test && ./bin/noisebuilder_tests
```

Checks rng, SIMD exp + reductions, FFT, TGA round trip, histogram, quality gates, thread determinism, direction distributions.

## Notes

- Same seed + same build = same file for any thread count. Linux vs Windows builds may differ by tie breaks (float rounding in exp / FMA). Both stay valid blue noise.
- vc is fastest and best per channel. swap is slower; use it for vectors.
- Big sizes: vc cost ~ N · kernel rows · width. 64² takes 0.03 s here. 256²+ not measured yet.
