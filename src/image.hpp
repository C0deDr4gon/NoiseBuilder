#pragma once
#include <cstddef>
#include <cstdint>
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

// Bytes like libtex / GPU UNORM: byte = round(v * 255), value = byte / 255.
inline uint8_t toByte(float v) {
    float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    return (uint8_t)(int)(c * 255.0f + 0.5f);
}

inline float fromByte(uint8_t b) { return b / 255.0f; }

// Rank 0..n-1 -> value. Affine map of (rank + 0.5) / n so toByte gives
// floor(rank * 256 / n): n multiple of 256 -> every byte used n / 256 times.
// Range [-0.5/255, 1 + 0.5/255] (toByte clamps), all n values distinct.
inline float rankValue(int rank, int n) {
    return (float)(((rank + 0.5) * 256.0 / n - 0.5) / 255.0);
}
