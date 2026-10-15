#include "analyze/analyze.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "analyze/fft.hpp"
#include "io/tga.hpp"

namespace {

double mean(const float* v, size_t n) {
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += v[i];
    return s / (double)n;
}

double variance(const float* v, size_t n, double m) {
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += (v[i] - m) * (v[i] - m);
    return s / (double)n;
}

// Spatial power spectrum of one channel, averaged over all slices.
// Normalized: white noise -> 1.0. Row-major w x h, DC at [0].
std::vector<double> spatialPower(const Image& img, int ch) {
    const int w = img.w, h = img.h;
    const size_t n = (size_t)w * h;
    std::vector<double> pow(n, 0.0);
    std::vector<cplx> buf(n);
    for (int z = 0; z < img.d; ++z) {
        const float* p = img.plane(ch) + (size_t)z * n;
        double m = mean(p, n), var = variance(p, n, m);
        if (var <= 0) continue;
        for (size_t i = 0; i < n; ++i) buf[i] = cplx(p[i] - m, 0.0);
        fft2d(buf, w, h);
        for (size_t i = 0; i < n; ++i) pow[i] += std::norm(buf[i]) / ((double)n * var);
    }
    for (auto& v : pow) v /= img.d;
    return pow;
}

// Radial average. bin = round(radius in freq units). bins 0 .. min(w,h)/2.
std::vector<double> radial(const std::vector<double>& pow, int w, int h) {
    int nb = std::min(w, h) / 2 + 1;
    std::vector<double> sum(nb, 0.0);
    std::vector<int> cnt(nb, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int fx = x <= w / 2 ? x : x - w;
            int fy = y <= h / 2 ? y : y - h;
            int b = (int)std::lround(std::sqrt((double)(fx * fx + fy * fy)));
            if (b >= nb) continue;
            sum[b] += pow[(size_t)y * w + x];
            cnt[b]++;
        }
    for (int b = 0; b < nb; ++b) sum[b] = cnt[b] ? sum[b] / cnt[b] : 0.0;
    return sum;
}

// Mean power over pixels with radius in (r0, r1].
double bandPower(const std::vector<double>& pow, int w, int h, double r0, double r1) {
    double s = 0;
    int c = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int fx = x <= w / 2 ? x : x - w;
            int fy = y <= h / 2 ? y : y - h;
            double r = std::sqrt((double)(fx * fx + fy * fy));
            if (r > r0 && r <= r1) { s += pow[(size_t)y * w + x]; ++c; }
        }
    return c ? s / c : 0.0;
}

// Temporal power per pixel along z, averaged. Normalized white = 1. Index = freq 0..d/2.
std::vector<double> temporalPower(const Image& img, int ch) {
    const int d = img.d;
    const size_t n = (size_t)img.w * img.h;
    const float* p = img.plane(ch);
    double m = mean(p, n * d), var = variance(p, n * d, m);
    std::vector<double> pow(d / 2 + 1, 0.0);
    if (var <= 0) return pow;
    std::vector<cplx> buf(d);
    for (size_t i = 0; i < n; ++i) {
        for (int z = 0; z < d; ++z) buf[z] = cplx(p[(size_t)z * n + i] - m, 0.0);
        fft(buf);
        for (int k = 0; k <= d / 2; ++k) pow[k] += std::norm(buf[k]) / ((double)d * var);
    }
    for (auto& v : pow) v /= (double)n;
    return pow;
}

void histogram(const Image& img, int ch, int& mn, int& mx) {
    int bins[256] = {};
    const float* p = img.plane(ch);
    for (size_t i = 0; i < img.pixels(); ++i) {
        int b = (int)(p[i] * 256.0f);
        bins[b < 0 ? 0 : b > 255 ? 255 : b]++;
    }
    mn = *std::min_element(bins, bins + 256);
    mx = *std::max_element(bins, bins + 256);
}

// Mean |neighbor diff| across wrap edge vs inside. Over all slices.
void seams(const Image& img, int ch, double& sx, double& sy) {
    double inX = 0, edX = 0, inY = 0, edY = 0;
    for (int z = 0; z < img.d; ++z)
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                float v = img.at(x, y, z, ch);
                double dx = std::fabs(v - img.at((x + 1) % img.w, y, z, ch));
                double dy = std::fabs(v - img.at(x, (y + 1) % img.h, z, ch));
                (x == img.w - 1 ? edX : inX) += dx;
                (y == img.h - 1 ? edY : inY) += dy;
            }
    // per-pixel means: edge has 1 column, inside has w-1
    sx = (edX / 1.0) / (inX / (img.w - 1));
    sy = (edY / 1.0) / (inY / (img.h - 1));
}

double pearson(const float* a, const float* b, size_t n) {
    double ma = mean(a, n), mb = mean(b, n), sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    return (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : 0.0;
}

// Log power, DC centered (fftshift), scaled 0..1 -> TGA.
void writeSpectrum(const std::string& path, const std::vector<double>& pow, int w, int h) {
    Image out(w, h, 1, 1);
    double lo = 1e300, hi = -1e300;
    std::vector<double> lg(pow.size());
    for (size_t i = 0; i < pow.size(); ++i) {
        lg[i] = std::log10(pow[i] + 1e-6);
        if (i == 0) continue;  // skip DC for range
        lo = std::min(lo, lg[i]);
        hi = std::max(hi, lg[i]);
    }
    double span = hi > lo ? hi - lo : 1.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            double v = i == 0 ? 0.0 : (lg[i] - lo) / span;
            out.at((x + w / 2) % w, (y + h / 2) % h, 0, 0) = (float)std::min(v, 0.999);
        }
    std::string err;
    if (!writeTga(path, out, 0, err)) std::fprintf(stderr, "warn: %s\n", err.c_str());
}

void writeCsv(const std::string& path, const char* head, const std::vector<double>& v) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::fprintf(stderr, "warn: cannot write %s\n", path.c_str()); return; }
    std::fprintf(f, "%s\n", head);
    for (size_t i = 0; i < v.size(); ++i) std::fprintf(f, "%zu,%.6f\n", i, v[i]);
    std::fclose(f);
}

} // namespace

Report analyzeImage(const Image& img, const std::string& outDir) {
    Report r;
    r.channels = img.c;
    if (!outDir.empty()) std::filesystem::create_directories(outDir);

    const int s = std::min(img.w, img.h);
    for (int c = 0; c < img.c; ++c) {
        ChannelReport& cr = r.ch[c];
        auto pow = spatialPower(img, c);
        cr.lowFreq  = bandPower(pow, img.w, img.h, 0.0, s / 8.0);
        cr.highFreq = bandPower(pow, img.w, img.h, s / 4.0, 1e9);
        histogram(img, c, cr.histMin, cr.histMax);
        seams(img, c, cr.seamX, cr.seamY);

        std::vector<double> tpow;
        if (img.d > 1) {
            tpow = temporalPower(img, c);
            int kLow = std::max(1, img.d / 8);
            double sum = 0;
            for (int k = 1; k <= kLow; ++k) sum += tpow[k];
            cr.tLowFreq = sum / kLow;
        }

        if (!outDir.empty()) {
            std::string base = outDir + "/ch" + std::to_string(c);
            writeSpectrum(base + "_spectrum.tga", pow, img.w, img.h);
            writeCsv(base + "_radial.csv", "freq,power", radial(pow, img.w, img.h));
            if (img.d > 1) writeCsv(base + "_temporal.csv", "freq,power", tpow);
        }
    }

    for (int a = 0; a < img.c; ++a)
        for (int b = a + 1; b < img.c; ++b)
            r.maxCrossCorr = std::max(r.maxCrossCorr,
                                      std::fabs(pearson(img.plane(a), img.plane(b), img.pixels())));
    return r;
}

void printReport(const Report& r, const Image& img) {
    std::printf("image %dx%dx%d  ch %d\n", img.w, img.h, img.d, img.c);
    std::printf("power: white noise = 1.00. blue = low freq near 0.\n");
    for (int c = 0; c < r.channels; ++c) {
        const ChannelReport& cr = r.ch[c];
        std::printf("ch%d  lowfreq %.4f  highfreq %.3f  hist %d..%d  seam %.2f/%.2f",
                    c, cr.lowFreq, cr.highFreq, cr.histMin, cr.histMax, cr.seamX, cr.seamY);
        if (cr.tLowFreq >= 0) std::printf("  t-lowfreq %.4f", cr.tLowFreq);
        std::printf("\n");
    }
    if (r.channels > 1) std::printf("max cross-corr %.4f\n", r.maxCrossCorr);
}
