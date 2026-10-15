#pragma once
#include <cstdint>

// Void and cluster (Ulichney 1993). 2D, toroidal (tiles seamless).
// Out: w*h values, value = (rank + 0.5) / N. Every rank used once -> flat histogram.
struct VcParams {
    int w = 64, h = 64;
    float sigma = 1.9f;      // gaussian energy sigma, pixels
    uint64_t seed = 1;
    uint64_t stream = 0;     // different stream = different texture, same seed
};

void voidCluster(const VcParams& p, float* out);
