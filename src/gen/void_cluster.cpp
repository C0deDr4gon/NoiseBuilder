#include "gen/void_cluster.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include "rng.hpp"
#include "simd.hpp"

using namespace simd;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// Energy field over volume w x h x d.
// E[p] = sum over ones q of kernel(p - q), toroidal.
// kernel = spatial gauss if same slice, temporal gauss if same pixel, else 0.
// Per-row cache (row = slice z, line y) of best void (min E over zeros) and
// best cluster (max E over ones), so each step only rescans touched rows.
struct Field {
    int w, h, d, r, rt;
    size_t nn;
    avector<float> kd;       // spatial kernel rows, each doubled to 2w: kd[ky*2w + i] = K[ky][i % w]
    std::vector<float> kt;   // temporal kernel, index dt + rt (0 at center)
    avector<float> e;        // energy, w*h*d
    avector<float> b;        // 1 = one, 0 = zero
    std::vector<float> rowVoid, rowClus;
    std::vector<int> rowVoidX, rowClusX;

    Field(int w_, int h_, int d_, float sigma, float sigmaT, float tw)
        : w(w_), h(h_), d(d_), rt(0), nn((size_t)w_ * h_) {
        // Rows past r add < 1e-7 of peak. Skip them.
        r = (int)std::ceil(sigma * std::sqrt(2.0f * std::log(1e7f)));
        kd.assign((size_t)h * 2 * w, 0.0f);
        float inv = 1.0f / (2.0f * sigma * sigma);
        for (int ky = 0; ky < h; ++ky)
            for (int kx = 0; kx < w; ++kx) {
                int dx = std::min(kx, w - kx), dy = std::min(ky, h - ky);
                float k = std::exp(-(float)(dx * dx + dy * dy) * inv);
                kd[(size_t)ky * 2 * w + kx] = k;
                kd[(size_t)ky * 2 * w + kx + w] = k;
            }
        if (d > 1 && tw > 0.0f) {
            // at dt = +-d/2 both sides hit same slice: counted twice (wrap), ok
            rt = std::min((int)std::ceil(sigmaT * std::sqrt(2.0f * std::log(1e7f))), d / 2);
            kt.assign(2 * rt + 1, 0.0f);
            float it = 1.0f / (2.0f * sigmaT * sigmaT);
            for (int dt = -rt; dt <= rt; ++dt)
                if (dt) kt[dt + rt] = tw * std::exp(-(float)(dt * dt) * it);
        }
        e.assign(nn * d, 0.0f);
        b.assign(nn * d, 0.0f);
        rowVoid.assign((size_t)h * d, kInf); rowClus.assign((size_t)h * d, -kInf);
        rowVoidX.assign((size_t)h * d, 0);   rowClusX.assign((size_t)h * d, 0);
        for (int row = 0; row < h * d; ++row) scanRow(row);
    }

    // row = z * h + y
    void scanRow(int row) {
        const float* er = e.data() + (size_t)row * w;
        const float* br = b.data() + (size_t)row * w;
        const vf half = set1(0.5f), inf = set1(kInf), ninf = set1(-kInf);
        vf vmin = inf, vmax = ninf;
        int x = 0;
        for (; x + W <= w; x += W) {
            vf ev = loadu(er + x);
            vm one = loadu(br + x) > half;
            vmin = min(vmin, select(one, inf, ev));
            vmax = max(vmax, select(one, ev, ninf));
        }
        float mn = reduceMin(vmin), mx = reduceMax(vmax);
        for (int t = x; t < w; ++t) {
            if (br[t] > 0.5f) mx = std::max(mx, er[t]);
            else              mn = std::min(mn, er[t]);
        }
        // first index hitting the value = deterministic tie break
        int ix = 0, cx = 0;
        bool fv = false, fc = false;
        for (int t = 0; t < w && !(fv && fc); ++t) {
            bool one = br[t] > 0.5f;
            if (!fv && !one && er[t] == mn) { ix = t; fv = true; }
            if (!fc && one && er[t] == mx)  { cx = t; fc = true; }
        }
        rowVoid[row] = mn; rowVoidX[row] = ix;
        rowClus[row] = mx; rowClusX[row] = cx;
    }

    // Toggle pixel (px, py) in slice z: s = +1 add one, -1 remove one.
    void toggle(int px, int py, int z, float s) {
        b[(size_t)z * nn + (size_t)py * w + px] = s > 0 ? 1.0f : 0.0f;

        // spatial: rows within r in slice z
        const vf sv = set1(s);
        bool all = 2 * r + 1 >= h;
        int y0 = all ? 0 : py - r, y1 = all ? h - 1 : py + r;
        for (int yy = y0; yy <= y1; ++yy) {
            int y = ((yy % h) + h) % h;
            int ky = ((y - py) % h + h) % h;
            const float* kr = kd.data() + (size_t)ky * 2 * w + (w - px);
            float* er = e.data() + (size_t)z * nn + (size_t)y * w;
            int x = 0;
            for (; x + W <= w; x += W) storeu(er + x, fma(sv, loadu(kr + x), loadu(er + x)));
            for (; x < w; ++x) er[x] += s * kr[x];
        }
        for (int yy = y0; yy <= y1; ++yy) scanRow(z * h + ((yy % h) + h) % h);

        // temporal: same pixel, other slices
        for (int dt = -rt; dt <= rt; ++dt) {
            if (!dt) continue;
            int t = ((z + dt) % d + d) % d;
            e[(size_t)t * nn + (size_t)py * w + px] += s * kt[dt + rt];
        }
        for (int dt = -rt; dt <= rt; ++dt) {
            if (!dt) continue;
            scanRow((((z + dt) % d + d) % d) * h + py);
        }
    }

    bool isOne(int p, int z) const { return b[(size_t)z * nn + p] > 0.5f; }

    // best void / cluster in slice z -> pixel index in slice
    int bestVoid(int z) const {
        int base = z * h, by = 0;
        for (int y = 1; y < h; ++y) if (rowVoid[base + y] < rowVoid[base + by]) by = y;
        return by * w + rowVoidX[base + by];
    }
    int bestCluster(int z) const {
        int base = z * h, by = 0;
        for (int y = 1; y < h; ++y) if (rowClus[base + y] > rowClus[base + by]) by = y;
        return by * w + rowClusX[base + by];
    }

    void togglePix(int p, int z, float s) { toggle(p % w, p / w, z, s); }
};

} // namespace

void voidCluster(const VcParams& prm, float* out) {
    const int n = prm.w * prm.h, d = prm.depth;
    Rng rng(prm.seed, prm.stream);
    Field f(prm.w, prm.h, d, prm.sigma, prm.sigmaT, prm.timeWeight);

    // Initial pattern: ~10% ones at random spots, every slice.
    int ones = std::max(1, n / 10);
    for (int z = 0; z < d; ++z)
        for (int placed = 0; placed < ones;) {
            int p = (int)rng.below((uint32_t)n);
            if (f.isOne(p, z)) continue;
            f.togglePix(p, z, 1.0f);
            ++placed;
        }

    // Relax: per slice, move tightest cluster into biggest void.
    // Repeat over all slices until no slice moves.
    for (int it = 0; it < 10 * n; ++it) {
        bool moved = false;
        for (int z = 0; z < d; ++z) {
            int c = f.bestCluster(z);
            f.togglePix(c, z, -1.0f);
            int v = f.bestVoid(z);
            f.togglePix(v, z, 1.0f);
            if (v != c) moved = true;
        }
        if (!moved) break;
    }

    std::vector<int> rank((size_t)n * d, -1);
    Field proto = f;

    // Phase 1: remove tightest clusters, ranks ones-1 .. 0. Slices in lockstep.
    for (int rk = ones - 1; rk >= 0; --rk)
        for (int z = 0; z < d; ++z) {
            int c = f.bestCluster(z);
            f.togglePix(c, z, -1.0f);
            rank[(size_t)z * n + c] = rk;
        }

    // Phase 2 + 3: fill biggest voids, ranks ones .. n-1. Slices in lockstep.
    // (Ulichney phase 3 = tightest cluster of zeros. With toroidal kernel,
    //  zero-energy = const - one-energy, so it is the same pick.)
    f = std::move(proto);
    int lastTick = 0;
    for (int rk = ones; rk < n; ++rk) {
        for (int z = 0; z < d; ++z) {
            int v = f.bestVoid(z);
            f.togglePix(v, z, 1.0f);
            rank[(size_t)z * n + v] = rk;
        }
        int tick = (int)((int64_t)(rk - ones + 1) * 10 / (n - ones));
        if (prm.progress && tick > lastTick) {
            lastTick = tick;
            std::printf("  vc %3d%%\n", tick * 10), std::fflush(stdout);
        }
    }

    for (size_t i = 0; i < (size_t)n * d; ++i) out[i] = (rank[i] + 0.5f) / (float)n;
}
