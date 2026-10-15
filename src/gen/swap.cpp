#include "gen/swap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "gen/void_cluster.hpp"
#include "rng.hpp"
#include "simd.hpp"
#include "thread_pool.hpp"

using namespace simd;

namespace {

// Neighbor window. a[k] = spatial/temporal part of exponent.
// Center + pad = huge (-> exp = 0, no self term).
struct Window {
    int r = 0, side = 0, count = 0, padded = 0;
    avector<float> a;

    // 2D square, radius r
    static Window square(int r, float sigma) {
        Window w;
        w.r = r; w.side = 2 * r + 1; w.count = w.side * w.side;
        w.padded = (int)padW((size_t)w.count);
        w.a.assign(w.padded, 1e30f);
        float inv = 1.0f / (sigma * sigma);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx || dy) w.a[(dy + r) * w.side + (dx + r)] = (float)(dx * dx + dy * dy) * inv;
        return w;
    }

    // 1D line, radius r. weight -> exponent offset -log(weight).
    static Window line(int r, float sigma, float weight) {
        Window w;
        w.r = r; w.side = 2 * r + 1; w.count = w.side;
        w.padded = (int)padW((size_t)w.count);
        w.a.assign(w.padded, 1e30f);
        float inv = 1.0f / (sigma * sigma), off = -std::log(weight);
        for (int dt = -r; dt <= r; ++dt)
            if (dt) w.a[dt + r] = (float)(dt * dt) * inv + off;
        return w;
    }
};

// Value distance term |v|^(C/2) / sigmaV^2 for C channels.
template <int C> inline vf valTerm(const vf* d, vf invSv) {
    if constexpr (C == 1) return sqrt(abs(d[0])) * invSv;
    vf s = d[0] * d[0];
    for (int c = 1; c < C; ++c) s = fma(d[c], d[c], s);
    if constexpr (C == 2) return sqrt(s) * invSv;
    else if constexpr (C == 3) { vf q = sqrt(s); return q * sqrt(q) * invSv; }  // |v|^1.5
    else return s * invSv;                          // C == 4: |v|^2
}
template <int C> inline float valTermS(const float* d, float invSv) {
    if constexpr (C == 1) return std::sqrt(std::fabs(d[0])) * invSv;
    float s = 0;
    for (int c = 0; c < C; ++c) s += d[c] * d[c];
    if constexpr (C == 2) return std::sqrt(s) * invSv;
    else if constexpr (C == 3) { float q = std::sqrt(s); return q * std::sqrt(q) * invSv; }
    else return s * invSv;
}

template <int C>
struct Opt {
    int n, mask, shift, depth;    // size = 1 << shift
    size_t nn;                    // pixels per slice
    const Window& ws;             // spatial
    const Window* wt;             // temporal, null if 2D
    float invSv;
    std::vector<float*> v;        // C planes, each depth slices

    Opt(int size, int d, const Window& s, const Window* t, float sigmaV, float* const* planes)
        : n(size), mask(size - 1), depth(d), nn((size_t)size * size), ws(s), wt(t),
          invSv(1.0f / (sigmaV * sigmaV)) {
        shift = 0;
        while ((1 << shift) < size) ++shift;
        v.assign(planes, planes + C);
    }

    size_t idx(int x, int y, int z) const { return (size_t)z * nn + ((size_t)y << shift) + x; }

    // Spatial window around (x, y) in slice z -> buf[c][k]
    void gatherS(int x, int y, int z, float* buf) const {
        const int r = ws.r, side = ws.side;
        for (int c = 0; c < C; ++c) {
            float* o = buf + (size_t)c * ws.padded;
            const float* src = v[c] + (size_t)z * nn;
            for (int dy = -r; dy <= r; ++dy) {
                const float* row = src + ((size_t)((y + dy) & mask) << shift);
                float* orow = o + (dy + r) * side;
                for (int dx = -r; dx <= r; ++dx) orow[dx + r] = row[(x + dx) & mask];
            }
            for (int k = ws.count; k < ws.padded; ++k) o[k] = 0.0f;
        }
    }

    // Temporal window: pixel (x, y) over slices z-r .. z+r (wrap) -> buf[c][k]
    void gatherT(int x, int y, int z, float* buf) const {
        const int r = wt->r;
        const size_t p = ((size_t)y << shift) + x;
        for (int c = 0; c < C; ++c) {
            float* o = buf + (size_t)c * wt->padded;
            for (int dt = -r; dt <= r; ++dt) {
                int t = ((z + dt) % depth + depth) % depth;
                o[dt + r] = v[c][(size_t)t * nn + p];
            }
            for (int k = wt->count; k < wt->padded; ++k) o[k] = 0.0f;
        }
    }

    // sum_k exp(-a_k - g(vNew - w_k)) - exp(-a_k - g(vOld - w_k))
    float delta(const Window& win, const float* buf, const float* vNew, const float* vOld) const {
        vf acc = zero();
        vf vn[C], vo[C];
        for (int c = 0; c < C; ++c) { vn[c] = set1(vNew[c]); vo[c] = set1(vOld[c]); }
        const vf isv = set1(invSv);
        for (int k = 0; k < win.padded; k += W) {
            vf a = load(win.a.data() + k);
            vf dn[C], dold[C];
            for (int c = 0; c < C; ++c) {
                vf w = load(buf + (size_t)c * win.padded + k);
                dn[c] = vn[c] - w;
                dold[c] = vo[c] - w;
            }
            acc += simd::exp(zero() - a - valTerm<C>(dn, isv));
            acc -= simd::exp(zero() - a - valTerm<C>(dold, isv));
        }
        return reduceAdd(acc);
    }

    // Energy change if pixels p and q in slice z swap. buf = scratch.
    float swapDelta(int px, int py, int qx, int qy, int z, float* buf) const {
        float vp[C], vq[C];
        for (int c = 0; c < C; ++c) {
            vp[c] = v[c][idx(px, py, z)];
            vq[c] = v[c][idx(qx, qy, z)];
        }
        float* b0 = buf;
        float* b1 = buf + (size_t)C * ws.padded;
        gatherS(px, py, z, b0);
        gatherS(qx, qy, z, b1);
        float d = delta(ws, b0, vq, vp) + delta(ws, b1, vp, vq);

        // Both windows counted the p-q pair as if it changed. It did not. Remove.
        int dx = ((qx - px + n / 2) & mask) - n / 2;
        int dy = ((qy - py + n / 2) & mask) - n / 2;
        if (std::abs(dx) <= ws.r && std::abs(dy) <= ws.r) {
            float a = ws.a[(dy + ws.r) * ws.side + (dx + ws.r)];
            float diff[C];
            for (int c = 0; c < C; ++c) diff[c] = vp[c] - vq[c];
            d -= 2.0f * (expScalar(-a) - expScalar(-a - valTermS<C>(diff, invSv)));
        }

        // Temporal: p and q are different pixels, so no shared pair.
        if (wt) {
            gatherT(px, py, z, b0);
            gatherT(qx, qy, z, b1);
            d += delta(*wt, b0, vq, vp) + delta(*wt, b1, vp, vq);
        }
        return d;
    }

    void swap(int px, int py, int qx, int qy, int z) {
        for (int c = 0; c < C; ++c) std::swap(v[c][idx(px, py, z)], v[c][idx(qx, qy, z)]);
    }
};

template <int C>
void run(const SwapParams& prm, float* const* planes) {
    const int n = prm.size, depth = prm.depth;
    int r = std::max(1, (int)std::ceil(3.0f * prm.sigmaS));
    r = std::min(r, (n - 1) / 2);
    Window ws = Window::square(r, prm.sigmaS);

    Window wtStore;
    const Window* wt = nullptr;
    if (depth > 1 && prm.timeWeight > 0.0f) {
        int rt = std::max(1, (int)std::ceil(3.0f * prm.sigmaT));
        rt = std::min(rt, depth / 2);   // at depth/2 both sides hit same slice: counted twice, ok (wrap)
        wtStore = Window::line(rt, prm.sigmaT, prm.timeWeight);
        wt = &wtStore;
    }
    Opt<C> opt(n, depth, ws, wt, prm.sigmaV, planes);

    // Blocks: pot side B > r, 2x2 colors. Same color blocks are B apart,
    // so their reads (block + r) never touch each other's writes -> safe in parallel.
    // Grid shifts by random offset each round (wraps, nb even -> colors still valid).
    // Slices run one after another; temporal reads only touch other slices -> safe.
    int B = 16;
    while (B <= r) B *= 2;
    int nb = n / B;
    if (nb < 2) { B = n; nb = 1; }
    const int colors = nb == 1 ? 1 : 4;

    const int rounds = std::max(1, prm.iters);
    const int swapsPerBlock = B * B;
    const size_t bufLen = 2 * (size_t)C * std::max(ws.padded, wt ? wt->padded : 0);
    const int mask = n - 1;

    for (int rd = 0; rd < rounds; ++rd) {
        float temp = prm.temp * (1.0f - (float)rd / rounds);
        for (int z = 0; z < depth; ++z) {
            // Shift block grid each round, else values never cross block edges.
            Rng gridRng(prm.seed ^ 0xA5A5A5A5ULL,
                        (prm.stream * 1000003ULL + (uint64_t)rd) * 4099ULL + (uint64_t)z);
            const int ox = nb == 1 ? 0 : (int)gridRng.below(n);
            const int oy = nb == 1 ? 0 : (int)gridRng.below(n);

            for (int col = 0; col < colors; ++col) {
                std::vector<int> blocks;
                for (int by = 0; by < nb; ++by)
                    for (int bx = 0; bx < nb; ++bx)
                        if (colors == 1 || ((bx & 1) + 2 * (by & 1)) == col) blocks.push_back(by * nb + bx);

                parallelFor((int)blocks.size(), prm.threads, [&](int i) {
                    int blk = blocks[i];
                    int bx0 = (blk % nb) * B + ox, by0 = (blk / nb) * B + oy;
                    // own rng per (round, slice, block) -> same result for any thread count
                    Rng rng(prm.seed ^ ((uint64_t)rd << 32) ^ ((uint64_t)z << 20) ^
                                (uint64_t)blk * 0x9E3779B97F4A7C15ULL,
                            prm.stream);
                    avector<float> buf(bufLen);
                    for (int s = 0; s < swapsPerBlock; ++s) {
                        int px = (bx0 + (int)rng.below(B)) & mask, py = (by0 + (int)rng.below(B)) & mask;
                        int qx = (bx0 + (int)rng.below(B)) & mask, qy = (by0 + (int)rng.below(B)) & mask;
                        float u = rng.uniform();
                        if (px == qx && py == qy) continue;
                        float d = opt.swapDelta(px, py, qx, qy, z, buf.data());
                        bool take = d < 0.0f || (temp > 0.0f && u < std::exp(-d / temp));
                        if (take) opt.swap(px, py, qx, qy, z);
                    }
                });
            }
        }
        if (prm.progress && (rd + 1) % std::max(1, rounds / 10) == 0)
            std::printf("  swap %3d%%\n", (rd + 1) * 100 / rounds), std::fflush(stdout);
    }
}

} // namespace

void swapOptimize(const SwapParams& prm, float* const* planes) {
    const int n = prm.size * prm.size;

    // Init each channel. vc init on volume = spatiotemporal vc (time-linked).
    for (int c = 0; c < prm.channels; ++c) {
        if (prm.initVc) {
            VcParams vp;
            vp.w = vp.h = prm.size;
            vp.depth = prm.depth;
            vp.sigma = prm.vcSigma;
            vp.sigmaT = prm.vcSigmaT;
            vp.timeWeight = prm.depth > 1 ? prm.vcTimeWeight : 0.0f;
            vp.seed = prm.seed;
            vp.stream = prm.stream * 16 + c;
            voidCluster(vp, planes[c]);
            continue;
        }
        if (prm.dist != Dist::Uniform) break;   // vector dists filled below
        for (int z = 0; z < prm.depth; ++z) {
            float* dst = planes[c] + (size_t)z * n;
            Rng rng(prm.seed, (prm.stream * 4099ULL + (uint64_t)z) * 16ULL + (uint64_t)c);
            for (int i = 0; i < n; ++i) dst[i] = (i + 0.5f) / (float)n;
            for (int i = n - 1; i > 0; --i) std::swap(dst[i], dst[rng.below(i + 1)]);
        }
    }

    if (prm.dist != Dist::Uniform)
        for (int z = 0; z < prm.depth; ++z) {
            float* sl[4];
            for (int c = 0; c < prm.channels; ++c) sl[c] = planes[c] + (size_t)z * n;
            fillDist(prm.dist, n, prm.seed, prm.stream * 4099ULL + (uint64_t)z, sl);
        }

    switch (prm.channels) {
        case 1: run<1>(prm, planes); break;
        case 3: run<3>(prm, planes); break;
        case 2: run<2>(prm, planes); break;
        case 4: run<4>(prm, planes); break;
        default: break;
    }
}
