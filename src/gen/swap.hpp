#pragma once
#include <cstdint>

// Swap optimizer (Georgiev & Fajardo 2016), toroidal.
// Start: each channel of each slice = random permutation of (rank+0.5)/N
// (or void-and-cluster). Swap whole pixel vectors inside one slice ->
// every slice keeps a perfect per-channel histogram.
//
// Energy = sum over pixel pairs of
//   exp(-dist^2 / sigma^2 - |vi-vj|^(C/2) / sigmaV^2)
// C channels = vector value -> joint blue noise, not just per channel.
//
// depth > 1 = spatiotemporal (STBN, Wolfe et al. 2022 style):
//   pairs in same slice (spatial, sigmaS) + same pixel across time (temporal, sigmaT).
//   -> each slice blue in space, each pixel blue over time. Time wraps too.
struct SwapParams {
    int size = 64;           // w = h, pot
    int depth = 1;           // slices, pot. 1 = plain 2D
    int channels = 1;        // 1, 2, 4
    float sigmaS = 2.1f;     // spatial sigma (paper 2.1)
    float sigmaT = 2.1f;     // temporal sigma (depth > 1)
    float timeWeight = 1.0f; // temporal energy scale (depth > 1)
    float sigmaV = 1.0f;     // value sigma (paper 1.0)
    int iters = 100;         // swap tries per pixel
    float temp = 0.0f;       // annealing start temp, 0 = greedy
    bool initVc = false;     // start from void-and-cluster per slice/channel
    float vcSigma = 1.9f;
    uint64_t seed = 1;
    uint64_t stream = 0;
    int threads = 1;
    bool progress = false;
};

// planes[c] = size*size*depth floats out (slice z at z*size*size), one per channel.
void swapOptimize(const SwapParams& p, float* const* planes);
