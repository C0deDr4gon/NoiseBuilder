#pragma once
#include <cstdint>

// Swap optimizer (Georgiev & Fajardo 2016). 2D, toroidal.
// Start: each channel = random permutation of (rank+0.5)/N (or void-and-cluster).
// Swap whole pixel vectors -> per-channel histogram stays perfect.
// Energy = sum over pixel pairs of
//   exp(-|pi-pj|^2 / sigmaS^2 - |vi-vj|^(C/2) / sigmaV^2)
// C channels = vector value -> joint blue noise, not just per channel.
struct SwapParams {
    int size = 64;           // w = h, pot
    int channels = 1;        // 1, 2, 4
    float sigmaS = 2.1f;     // spatial sigma (paper 2.1)
    float sigmaV = 1.0f;     // value sigma (paper 1.0)
    int iters = 100;         // swap tries per pixel
    float temp = 0.0f;       // annealing start temp, 0 = greedy
    bool initVc = false;     // start from void-and-cluster per channel
    float vcSigma = 1.9f;
    uint64_t seed = 1;
    uint64_t stream = 0;
    int threads = 1;
    bool progress = false;
};

// planes[c] = size*size floats out, one per channel.
void swapOptimize(const SwapParams& p, float* const* planes);
