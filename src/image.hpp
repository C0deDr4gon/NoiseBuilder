#pragma once
#include <cstddef>
#include "simd.hpp"

// Float volume: W x H x D, C channels. D = 1 for 2D.
// Layout: channel-planar (SoA), each plane x fastest, then y, then z.
// Planar + 64-byte aligned = good for SIMD. pot sizes >= 4x4 keep every plane aligned.
struct Image {
    int w = 0, h = 0, d = 1, c = 1;
    simd::avector<float> data;

    Image() = default;
    Image(int w_, int h_, int d_, int c_)
        : w(w_), h(h_), d(d_), c(c_), data((size_t)w_ * h_ * d_ * c_, 0.0f) {}

    size_t pixels() const { return (size_t)w * h * d; }
    float* plane(int ch) { return data.data() + pixels() * ch; }
    const float* plane(int ch) const { return data.data() + pixels() * ch; }

    float& at(int x, int y, int z, int ch) {
        return data[pixels() * ch + ((size_t)z * h + y) * w + x];
    }
    float at(int x, int y, int z, int ch) const {
        return data[pixels() * ch + ((size_t)z * h + y) * w + x];
    }
};

inline bool isPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }
