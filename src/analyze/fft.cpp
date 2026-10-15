#include "analyze/fft.hpp"
#include <cmath>
#include <utility>

void fft(std::vector<cplx>& a) {
    const size_t n = a.size();
    // bit reverse order
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    const double pi = 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * pi / (double)len;
        cplx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cplx w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                cplx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

void fft2d(std::vector<cplx>& a, int w, int h) {
    std::vector<cplx> tmp(w);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) tmp[x] = a[(size_t)y * w + x];
        fft(tmp);
        for (int x = 0; x < w; ++x) a[(size_t)y * w + x] = tmp[x];
    }
    tmp.resize(h);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) tmp[y] = a[(size_t)y * w + x];
        fft(tmp);
        for (int y = 0; y < h; ++y) a[(size_t)y * w + x] = tmp[y];
    }
}
