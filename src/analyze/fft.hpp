#pragma once
#include <complex>
#include <vector>

using cplx = std::complex<double>;

// In-place radix-2 FFT. n = a.size() must be power of two.
void fft(std::vector<cplx>& a);

// In-place 2D FFT, row-major w x h, both pot.
void fft2d(std::vector<cplx>& a, int w, int h);
