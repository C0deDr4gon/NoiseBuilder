// Tiny test exe. No framework. Run: bin/noisebuilder_tests
// Exit 0 = all pass. Small sizes only (32x32) so it runs fast.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "analyze/analyze.hpp"
#include "analyze/fft.hpp"
#include "gen/dist.hpp"
#include "gen/swap.hpp"
#include "gen/void_cluster.hpp"
#include "image.hpp"
#include "io/tga.hpp"
#include "rng.hpp"
#include "simd.hpp"

static int gFails = 0, gChecks = 0;

#define CHECK(cond, ...)                                          \
    do {                                                          \
        ++gChecks;                                                \
        if (!(cond)) {                                            \
            ++gFails;                                             \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                             \
            std::printf("\n");                                    \
        }                                                         \
    } while (0)

// every value rankValue(rank, N) used exactly once in each slice of each channel
static bool perfectHistogram(const Image& img) {
    const size_t n = (size_t)img.w * img.h;
    for (int c = 0; c < img.c; ++c)
        for (int z = 0; z < img.d; ++z) {
            std::vector<int> seen(n, 0);
            const float* p = img.plane(c) + (size_t)z * n;
            for (size_t i = 0; i < n; ++i) {
                long r = std::lround((p[i] * 255.0 + 0.5) * (double)n / 256.0 - 0.5);
                if (r < 0 || r >= (long)n || seen[r]++) return false;
            }
        }
    return true;
}

static Image genVc(int size, int depth, float timeWeight, uint64_t seed) {
    Image img(size, size, depth, 1);
    VcParams p;
    p.w = p.h = size;
    p.depth = depth;
    p.timeWeight = timeWeight;
    p.seed = seed;
    voidCluster(p, img.plane(0));
    return img;
}

static Image genSwap(int size, int depth, int ch, Dist dist, int iters, int threads, bool initVc = false) {
    Image img(size, size, depth, ch);
    SwapParams p;
    p.size = size;
    p.depth = depth;
    p.channels = ch;
    p.dist = dist;
    p.iters = iters;
    p.threads = threads;
    p.initVc = initVc;
    float* pl[4];
    for (int c = 0; c < ch; ++c) pl[c] = img.plane(c);
    swapOptimize(p, pl);
    return img;
}

// ---------------------------------------------------------------------------

static void testRng() {
    Rng a(42, 7), b(42, 7), c(43, 7), d(42, 8);
    bool same = true, diffSeed = false, diffStream = false;
    for (int i = 0; i < 100; ++i) {
        uint32_t x = a.next(), y = b.next();
        same = same && x == y;
        diffSeed = diffSeed || x != c.next();
        diffStream = diffStream || x != d.next();
    }
    CHECK(same, "same seed must give same sequence");
    CHECK(diffSeed, "other seed must differ");
    CHECK(diffStream, "other stream must differ");

    Rng r(1);
    bool inRange = true;
    for (int i = 0; i < 10000; ++i) inRange = inRange && r.below(37) < 37;
    CHECK(inRange, "below(n) out of range");
    float u = r.uniform();
    CHECK(u >= 0.0f && u < 1.0f, "uniform out of [0,1): %f", u);
}

static void testSimd() {
    // vector exp vs std::exp and vs scalar twin
    double worst = 0, worstTwin = 0;
    alignas(64) float in[simd::W], out[simd::W];
    for (float x0 = -80.0f; x0 < 80.0f; x0 += 0.37f * simd::W) {
        for (int i = 0; i < simd::W; ++i) in[i] = x0 + 0.37f * i;
        simd::store(out, simd::exp(simd::load(in)));
        for (int i = 0; i < simd::W; ++i) {
            double ref = std::exp((double)in[i]);
            worst = std::max(worst, std::fabs(out[i] - ref) / ref);
            worstTwin = std::max(worstTwin, (double)std::fabs(out[i] - simd::expScalar(in[i])) / ref);
        }
    }
    CHECK(worst < 1e-6, "exp rel err %g", worst);
    CHECK(worstTwin < 1e-6, "exp vs expScalar rel err %g", worstTwin);

    for (int i = 0; i < simd::W; ++i) in[i] = -200.0f;
    simd::store(out, simd::exp(simd::load(in)));
    CHECK(out[0] == 0.0f, "exp(-200) must be 0, got %g", out[0]);
    CHECK(simd::expScalar(-200.0f) == 0.0f, "expScalar(-200) must be 0");

    // reductions
    float sum = 0, mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < simd::W; ++i) {
        in[i] = (float)((i * 7) % 5) - 1.5f;
        sum += in[i]; mn = std::min(mn, in[i]); mx = std::max(mx, in[i]);
    }
    simd::vf v = simd::load(in);
    CHECK(std::fabs(simd::reduceAdd(v) - sum) < 1e-5f, "reduceAdd");
    CHECK(simd::reduceMin(v) == mn, "reduceMin");
    CHECK(simd::reduceMax(v) == mx, "reduceMax");

    // aligned alloc
    simd::avector<float> a(37);
    CHECK(((uintptr_t)a.data() % simd::kAlign) == 0, "avector not aligned");
}

static void testFft() {
    // delta -> flat spectrum of 1
    std::vector<cplx> a(16, cplx(0, 0));
    a[0] = 1.0;
    fft(a);
    bool flat = true;
    for (auto& v : a) flat = flat && std::abs(v - cplx(1, 0)) < 1e-12;
    CHECK(flat, "fft(delta) must be flat");

    // single cosine -> energy at bins k and n-k
    const int n = 32, k = 5;
    std::vector<cplx> c(n);
    for (int i = 0; i < n; ++i) c[i] = std::cos(2 * 3.14159265358979 * k * i / n);
    fft(c);
    CHECK(std::abs(std::abs(c[k]) - n / 2.0) < 1e-9 && std::abs(std::abs(c[n - k]) - n / 2.0) < 1e-9,
          "cos peak wrong");

    // Parseval, 2D
    Rng r(3);
    std::vector<cplx> g(16 * 8);
    double e0 = 0, e1 = 0;
    for (auto& v : g) { v = r.uniform() - 0.5; e0 += std::norm(v); }
    fft2d(g, 16, 8);
    for (auto& v : g) e1 += std::norm(v);
    CHECK(std::fabs(e1 / g.size() - e0) < 1e-9, "parseval %g vs %g", e1 / g.size(), e0);
}

static void testTga() {
    auto dir = std::filesystem::temp_directory_path() / "noisebuilder_tests";
    std::filesystem::create_directories(dir);
    for (int ch : {1, 2, 3, 4}) {
        Image img(8, 4, 1, ch);
        for (int c = 0; c < ch; ++c)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                    img.at(x, y, 0, c) = fromByte((uint8_t)((x * 31 + y * 17 + c * 53) % 256));
        std::string path = (dir / ("t" + std::to_string(ch) + ".tga")).string(), err;
        CHECK(writeTga(path, img, 0, err), "write %d ch: %s", ch, err.c_str());
        Image back;
        CHECK(readTga(path, back, err), "read %d ch: %s", ch, err.c_str());
        CHECK(back.w == 8 && back.h == 4 && back.c == ch, "%d ch read back as %dx%d c%d", ch, back.w, back.h, back.c);
        bool same = back.c == ch;
        for (int c = 0; c < ch && same; ++c)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                    same = same && back.at(x, y, 0, c) == img.at(x, y, 0, c);
        CHECK(same, "%d ch round trip values differ", ch);
    }

    // 3D: slices + atlas
    Image vol(4, 4, 3, 1);
    for (size_t i = 0; i < vol.data.size(); ++i) vol.data[i] = fromByte((uint8_t)((i * 37) % 256));
    std::string err, sp = (dir / "s.tga").string(), ap = (dir / "a.tga").string();
    CHECK(writeTgaSlices(sp, vol, err), "slices: %s", err.c_str());
    CHECK(std::filesystem::exists(slicePath(sp, 2)), "slice 2 missing");
    CHECK(writeTgaAtlas(ap, vol, err), "atlas: %s", err.c_str());
    Image at;
    CHECK(readTga(ap, at, err) && at.h == 12, "atlas height %d", at.h);
    CHECK(slicePath("a/b.c/name.tga", 7) == "a/b.c/name_007.tga", "slicePath: %s", slicePath("a/b.c/name.tga", 7).c_str());

    std::filesystem::remove_all(dir);
}

static void testVoidCluster() {
    Image a = genVc(32, 1, 0, 1), b = genVc(32, 1, 0, 1), c = genVc(32, 1, 0, 2);
    CHECK(perfectHistogram(a), "vc histogram not perfect");
    CHECK(a.data == b.data, "vc same seed must be same");
    CHECK(a.data != c.data, "vc other seed must differ");

    Report r = analyzeImage(a, "");
    CHECK(r.ch[0].lowFreq < 0.002, "vc 32 lowfreq %.4f (want < 0.002)", r.ch[0].lowFreq);
    CHECK(r.ch[0].highFreq > 1.1, "vc 32 highfreq %.3f (want > 1.1)", r.ch[0].highFreq);
    CHECK(std::fabs(r.ch[0].seamX - 1) < 0.25 && std::fabs(r.ch[0].seamY - 1) < 0.25,
          "vc seams %.2f %.2f", r.ch[0].seamX, r.ch[0].seamY);

    // tiny + non-square-ish sizes must not crash
    Image t = genVc(4, 1, 0, 1);
    CHECK(perfectHistogram(t), "vc 4x4 histogram");
}

static void testStbn() {
    Image s = genVc(32, 16, 1.0f, 1);
    CHECK(perfectHistogram(s), "stbn per-slice histogram not perfect");
    Report r = analyzeImage(s, "");
    CHECK(r.ch[0].lowFreq < 0.005, "stbn space lowfreq %.4f (want < 0.005)", r.ch[0].lowFreq);
    CHECK(r.ch[0].tLowFreq < 0.1, "stbn time lowfreq %.4f (want < 0.1)", r.ch[0].tLowFreq);

    // independent slices = white over time
    Image w = genVc(32, 16, 0.0f, 1);
    Report rw = analyzeImage(w, "");
    CHECK(rw.ch[0].tLowFreq > 0.5, "independent slices time %.3f should be ~1", rw.ch[0].tLowFreq);
}

static void testSwap() {
    Image a = genSwap(32, 1, 1, Dist::Uniform, 100, 1);
    CHECK(perfectHistogram(a), "swap histogram not perfect");
    Report r = analyzeImage(a, "");
    CHECK(r.ch[0].lowFreq < 0.03, "swap 32 lowfreq %.4f (want < 0.03)", r.ch[0].lowFreq);

    // thread count must not change result
    Image t1 = genSwap(64, 1, 2, Dist::Uniform, 10, 1);
    Image t4 = genSwap(64, 1, 2, Dist::Uniform, 10, 4);
    CHECK(t1.data == t4.data, "swap 1 vs 4 threads differ");

    Image s1 = genSwap(32, 4, 1, Dist::Uniform, 10, 1);
    Image s4 = genSwap(32, 4, 1, Dist::Uniform, 10, 4);
    CHECK(s1.data == s4.data, "stbn-swap 1 vs 4 threads differ");
    CHECK(perfectHistogram(s1), "stbn-swap histogram");

    Image v = genSwap(32, 4, 1, Dist::Uniform, 10, 2, true);
    CHECK(perfectHistogram(v), "stbn-swap init vc histogram");
}

static void testDist() {
    struct Case { Dist d; double meanZ; bool unit; };
    for (Case cs : {Case{Dist::Sphere, 0.0, true}, Case{Dist::Hemi, 0.5, true},
                    Case{Dist::CosHemi, 2.0 / 3.0, true}, Case{Dist::Circle, 0.0, true},
                    Case{Dist::Disk, 0.0, false}}) {
        int ch = distChannels(cs.d);
        Image img = genSwap(32, 1, ch, cs.d, 20, 1);
        const size_t n = img.pixels();
        double sumZ = 0, worstLen = 0, maxLen = 0;
        for (size_t i = 0; i < n; ++i) {
            double l2 = 0;
            for (int c = 0; c < ch; ++c) {
                double v = img.plane(c)[i] * 2.0 - 1.0;
                l2 += v * v;
            }
            if (ch == 3) sumZ += img.plane(2)[i] * 2.0 - 1.0;
            worstLen = std::max(worstLen, std::fabs(std::sqrt(l2) - 1.0));
            maxLen = std::max(maxLen, std::sqrt(l2));
        }
        const char* nm = distName(cs.d);
        if (cs.unit) CHECK(worstLen < 1e-5, "%s not unit, err %g", nm, worstLen);
        else         CHECK(maxLen <= 1.0 + 1e-5, "%s outside unit disk %g", nm, maxLen);
        if (ch == 3) CHECK(std::fabs(sumZ / n - cs.meanZ) < 0.01, "%s mean z %.4f want %.4f", nm, sumZ / n, cs.meanZ);
    }
}

int main() {
    std::printf("noisebuilder tests  simd %s (%d wide)\n", simd::name(), simd::W);
    std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"rng", testRng}, {"simd", testSimd}, {"fft", testFft}, {"tga", testTga},
        {"void_cluster", testVoidCluster}, {"stbn", testStbn}, {"swap", testSwap}, {"dist", testDist},
    };
    for (auto& t : tests) {
        int before = gFails;
        t.second();
        std::printf("%-14s %s\n", t.first, gFails == before ? "ok" : "FAIL");
    }
    std::printf("%d checks, %d failed\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
