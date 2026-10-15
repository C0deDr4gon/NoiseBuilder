#pragma once
#include <cstdint>

// Void and cluster (Ulichney 1993), toroidal (tiles seamless).
// Out: w*h*depth values, slice z at z*w*h. value = (rank + 0.5) / (w*h).
// Every slice uses every rank once -> flat histogram per slice.
//
// depth > 1 + timeWeight > 0 = spatiotemporal (STBN, Wolfe et al. 2022):
//   energy kernel = gauss in space (same slice) + gauss over time (same pixel).
//   All slices step together, one rank at a time.
// depth > 1 + timeWeight = 0 = independent slices.
struct VcParams {
    int w = 64, h = 64, depth = 1;
    float sigma = 1.9f;       // spatial sigma, pixels
    float sigmaT = 1.5f;      // temporal sigma, slices (1.5 tested best)
    float timeWeight = 0.0f;  // temporal kernel scale. 0 = no time link
    uint64_t seed = 1;
    uint64_t stream = 0;      // different stream = different texture, same seed
    bool progress = false;    // print % done
};

void voidCluster(const VcParams& p, float* out);
