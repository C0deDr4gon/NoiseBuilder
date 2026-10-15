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

// Window around a pixel, radius R. Spatial part of exponent precomputed.
struct Window {
    int r = 0, side = 0, count = 0, padded = 0;
    avector<float> a;   // |d|^2 / sigmaS^2, center + pad = huge (-> exp = 0)

    Window(int r_, float sigmaS) : r(r_) {
        side = 2 * r + 1;
        count = side * side;
        padded = (int)padW((size_t)count);
        a.assign(padded, 1e30f);
        float inv = 1.0f / (sigmaS * sigmaS);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx || dy) a[(dy + r) * side + (dx + r)] = (float)(dx * dx + dy * dy) * inv;
    }
};

// Value distance term |v|^(C/2) / sigmaV^2 for C channels.
template <int C> inline vf valTerm(const vf* d, vf invSv) {
    if constexpr (C == 1) return sqrt(abs(d[0])) * invSv;
    vf s = d[0] * d[0];
    for (int c = 1; c < C; ++c) s = fma(d[c], d[c], s);
    if constexpr (C == 2) return sqrt(s) * invSv;
    else return s * invSv;                          // C == 4: |v|^2
}
template <int C> inline float valTermS(const float* d, float invSv) {
    if constexpr (C == 1) return std::sqrt(std::fabs(d[0])) * invSv;
    float s = 0;
    for (int c = 0; c < C; ++c) s += d[c] * d[c];
    if constexpr (C == 2) return std::sqrt(s) * invSv;
    else return s * invSv;
}

template <int C>
struct Opt {
    int n, mask, shift;           // size = 1 << shift
    const Window& win;
    float invSv;
    std::vector<float*> v;        // C planes

    Opt(int size, const Window& w, float sigmaV, float* const* planes)
        : n(size), mask(size - 1), win(w), invSv(1.0f / (sigmaV * sigmaV)) {
        shift = 0;
        while ((1 << shift) < size) ++shift;
        v.assign(planes, planes + C);
    }

    // Gather window values around (x, y) into buf[c][k].
    void gather(int x, int y, float* buf) const {
        const int r = win.r, side = win.side;
        for (int c = 0; c < C; ++c) {
            float* o = buf + (size_t)c * win.padded;
            const float* src = v[c];
            for (int dy = -r; dy <= r; ++dy) {
                const float* row = src + ((size_t)((y + dy) & mask) << shift);
                float* orow = o + (dy + r) * side;
                for (int dx = -r; dx <= r; ++dx) orow[dx + r] = row[(x + dx) & mask];
            }
            for (int k = win.count; k < win.padded; ++k) o[k] = 0.0f;
        }
    }

    // sum_k exp(-a_k - g(vNew - w_k)) - exp(-a_k - g(vOld - w_k))
    float delta(const float* buf, const float* vNew, const float* vOld) const {
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

    // Energy change if pixels p and q swap. buf = scratch, 2 windows.
    float swapDelta(int px, int py, int qx, int qy, float* buf) const {
        float vp[C], vq[C];
        for (int c = 0; c < C; ++c) {
            vp[c] = v[c][(py << shift) + px];
            vq[c] = v[c][(qy << shift) + qx];
        }
        float* bp = buf;
        float* bq = buf + (size_t)C * win.padded;
        gather(px, py, bp);
        gather(qx, qy, bq);
        float d = delta(bp, vq, vp) + delta(bq, vp, vq);

        // Both windows counted the p-q pair as if it changed. It did not. Remove.
        int dx = ((qx - px + n / 2) & mask) - n / 2;
        int dy = ((qy - py + n / 2) & mask) - n / 2;
        if (std::abs(dx) <= win.r && std::abs(dy) <= win.r) {
            float a = win.a[(dy + win.r) * win.side + (dx + win.r)];
            float diff[C];
            for (int c = 0; c < C; ++c) diff[c] = vp[c] - vq[c];
            d -= 2.0f * (expScalar(-a) - expScalar(-a - valTermS<C>(diff, invSv)));
        }
        return d;
    }

    void swap(int px, int py, int qx, int qy) {
        for (int c = 0; c < C; ++c)
            std::swap(v[c][(py << shift) + px], v[c][(qy << shift) + qx]);
    }
};

template <int C>
void run(const SwapParams& prm, float* const* planes) {
    const int n = prm.size;
    int r = std::max(1, (int)std::ceil(3.0f * prm.sigmaS));
    r = std::min(r, (n - 1) / 2);
    Window win(r, prm.sigmaS);
    Opt<C> opt(n, win, prm.sigmaV, planes);

    // Blocks: pot side B > r, 2x2 colors. Same color blocks are B apart,
    // so their reads (block + r) never touch each other's writes -> safe in parallel.
    // Grid shifts by random offset each round (wraps, nb even -> colors still valid).
    int B = 16;
    while (B <= r) B *= 2;
    int nb = n / B;
    if (nb < 2) { B = n; nb = 1; }
    const int colors = nb == 1 ? 1 : 4;

    const int rounds = std::max(1, prm.iters);
    const int swapsPerBlock = B * B;
    const size_t bufLen = 2 * (size_t)C * win.padded;

    const int mask = n - 1;
    for (int rd = 0; rd < rounds; ++rd) {
        float temp = prm.temp * (1.0f - (float)rd / rounds);
        // Shift block grid each round, else values never cross block edges.
        Rng gridRng(prm.seed ^ 0xA5A5A5A5ULL, prm.stream * 1000003ULL + (uint64_t)rd);
        const int ox = nb == 1 ? 0 : (int)gridRng.below(n);
        const int oy = nb == 1 ? 0 : (int)gridRng.below(n);
        for (int col = 0; col < colors; ++col) {
            // blocks of this color
            std::vector<int> blocks;
            for (int by = 0; by < nb; ++by)
                for (int bx = 0; bx < nb; ++bx)
                    if (colors == 1 || ((bx & 1) + 2 * (by & 1)) == col) blocks.push_back(by * nb + bx);

            parallelFor((int)blocks.size(), prm.threads, [&](int i) {
                int blk = blocks[i];
                int bx0 = (blk % nb) * B + ox, by0 = (blk / nb) * B + oy;
                // own rng per (round, block) -> same result for any thread count
                Rng rng(prm.seed ^ ((uint64_t)rd << 32) ^ (uint64_t)blk * 0x9E3779B97F4A7C15ULL,
                        prm.stream);
                avector<float> buf(bufLen);
                for (int s = 0; s < swapsPerBlock; ++s) {
                    int px = (bx0 + (int)rng.below(B)) & mask, py = (by0 + (int)rng.below(B)) & mask;
                    int qx = (bx0 + (int)rng.below(B)) & mask, qy = (by0 + (int)rng.below(B)) & mask;
                    float u = rng.uniform();
                    if (px == qx && py == qy) continue;
                    float d = opt.swapDelta(px, py, qx, qy, buf.data());
                    bool take = d < 0.0f || (temp > 0.0f && u < std::exp(-d / temp));
                    if (take) opt.swap(px, py, qx, qy);
                }
            });
        }
        if (prm.progress && (rd + 1) % std::max(1, rounds / 10) == 0)
            std::printf("  swap %3d%%\n", (rd + 1) * 100 / rounds), std::fflush(stdout);
    }
}

} // namespace

void swapOptimize(const SwapParams& prm, float* const* planes) {
    const int n = prm.size * prm.size;

    // Init each channel.
    for (int c = 0; c < prm.channels; ++c) {
        if (prm.initVc) {
            VcParams vp;
            vp.w = vp.h = prm.size;
            vp.sigma = prm.vcSigma;
            vp.seed = prm.seed;
            vp.stream = prm.stream * 16 + c;
            voidCluster(vp, planes[c]);
        } else {
            Rng rng(prm.seed, prm.stream * 16 + c);
            for (int i = 0; i < n; ++i) planes[c][i] = (i + 0.5f) / (float)n;
            for (int i = n - 1; i > 0; --i) std::swap(planes[c][i], planes[c][rng.below(i + 1)]);
        }
    }

    switch (prm.channels) {
        case 1: run<1>(prm, planes); break;
        case 2: run<2>(prm, planes); break;
        case 4: run<4>(prm, planes); break;
        default: break;
    }
}
