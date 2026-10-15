#include "gen/void_cluster.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "rng.hpp"
#include "simd.hpp"

using namespace simd;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// Energy field state.
// E[p] = sum over ones q of gauss(toroidal dist p..q)
// Per-row cache of best void (min E over zeros) + best cluster (max E over ones),
// so each step only rescans rows the update touched.
struct Field {
    int w, h, r;
    avector<float> kd;       // kernel rows, each doubled to 2w: kd[ky*2w + i] = K[ky][i % w]
    avector<float> e;        // energy, w*h
    avector<float> b;        // 1 = one, 0 = zero
    std::vector<float> rowVoid, rowClus;
    std::vector<int> rowVoidX, rowClusX;

    Field(int w_, int h_, float sigma) : w(w_), h(h_) {
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
        e.assign((size_t)w * h, 0.0f);
        b.assign((size_t)w * h, 0.0f);
        rowVoid.assign(h, kInf); rowClus.assign(h, -kInf);
        rowVoidX.assign(h, 0);   rowClusX.assign(h, 0);
        rescanAll();
    }

    void scanRow(int y) {
        const float* er = e.data() + (size_t)y * w;
        const float* br = b.data() + (size_t)y * w;
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
        rowVoid[y] = mn; rowVoidX[y] = ix;
        rowClus[y] = mx; rowClusX[y] = cx;
    }

    // Toggle pixel p: s = +1 add one, -1 remove one.
    void toggle(int p, float s) {
        int px = p % w, py = p / w;
        b[p] = s > 0 ? 1.0f : 0.0f;
        const vf sv = set1(s);
        bool all = 2 * r + 1 >= h;
        int y0 = all ? 0 : py - r, y1 = all ? h - 1 : py + r;
        for (int yy = y0; yy <= y1; ++yy) {
            int y = ((yy % h) + h) % h;
            int ky = ((y - py) % h + h) % h;
            const float* kr = kd.data() + (size_t)ky * 2 * w + (w - px);
            float* er = e.data() + (size_t)y * w;
            int x = 0;
            for (; x + W <= w; x += W) storeu(er + x, fma(sv, loadu(kr + x), loadu(er + x)));
            for (; x < w; ++x) er[x] += s * kr[x];
        }
        for (int yy = y0; yy <= y1; ++yy) scanRow(((yy % h) + h) % h);
    }

    void rescanAll() { for (int y = 0; y < h; ++y) scanRow(y); }

    int bestVoid() const {
        int by = 0;
        for (int y = 1; y < h; ++y) if (rowVoid[y] < rowVoid[by]) by = y;
        return by * w + rowVoidX[by];
    }
    int bestCluster() const {
        int by = 0;
        for (int y = 1; y < h; ++y) if (rowClus[y] > rowClus[by]) by = y;
        return by * w + rowClusX[by];
    }
};

} // namespace

void voidCluster(const VcParams& prm, float* out) {
    const int n = prm.w * prm.h;
    Rng rng(prm.seed, prm.stream);
    Field f(prm.w, prm.h, prm.sigma);

    // Initial pattern: ~10% ones at random spots.
    int ones = std::max(1, n / 10);
    for (int placed = 0; placed < ones;) {
        int p = (int)rng.below((uint32_t)n);
        if (f.b[p] > 0.5f) continue;
        f.toggle(p, 1.0f);
        ++placed;
    }

    // Relax: move tightest cluster into biggest void until stable.
    for (int it = 0; it < 10 * n; ++it) {
        int c = f.bestCluster();
        f.toggle(c, -1.0f);
        int v = f.bestVoid();
        f.toggle(v, 1.0f);
        if (v == c) break;
    }

    std::vector<int> rank(n, -1);
    Field proto = f;

    // Phase 1: remove tightest clusters, ranks ones-1 .. 0.
    for (int rk = ones - 1; rk >= 0; --rk) {
        int c = f.bestCluster();
        f.toggle(c, -1.0f);
        rank[c] = rk;
    }

    // Phase 2 + 3: fill biggest voids, ranks ones .. n-1.
    // (Ulichney phase 3 = tightest cluster of zeros. With toroidal kernel,
    //  zero-energy = const - one-energy, so it is the same pick.)
    f = std::move(proto);
    for (int rk = ones; rk < n; ++rk) {
        int v = f.bestVoid();
        f.toggle(v, 1.0f);
        rank[v] = rk;
    }

    for (int i = 0; i < n; ++i) out[i] = (rank[i] + 0.5f) / (float)n;
}
