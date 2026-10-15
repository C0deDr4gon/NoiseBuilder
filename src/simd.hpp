#pragma once
// SIMD wrapper. One float vector type `vf`, width picked at compile time:
//   AVX-512 -> 16 floats, AVX2 (+FMA) -> 8, SSE2 -> 4, else scalar -> 1.
// Code written once against `vf` runs on all of them.
//
// Same algorithm on all paths (same fast exp poly, same rounding), but
// paths with FMA round a bit different from paths without. Tiny diff only.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#if defined(__AVX512F__)
  #define NB_SIMD_AVX512 1
  #include <immintrin.h>
#elif defined(__AVX2__)
  #define NB_SIMD_AVX2 1
  #include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #define NB_SIMD_SSE2 1
  #include <emmintrin.h>
#else
  #define NB_SIMD_SCALAR 1
#endif

namespace simd {

// ---------------------------------------------------------------------------
// Aligned memory. 64 bytes = cache line = one AVX-512 register.

constexpr size_t kAlign = 64;

template <class T>
struct AlignedAlloc {
    using value_type = T;
    AlignedAlloc() = default;
    template <class U> AlignedAlloc(const AlignedAlloc<U>&) {}
    T* allocate(size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(kAlign)));
    }
    void deallocate(T* p, size_t) { ::operator delete(p, std::align_val_t(kAlign)); }
    template <class U> bool operator==(const AlignedAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const AlignedAlloc<U>&) const { return false; }
};

template <class T>
using avector = std::vector<T, AlignedAlloc<T>>;

// ---------------------------------------------------------------------------
// vf = float vector, vm = compare mask

#if NB_SIMD_AVX512

constexpr int W = 16;
inline const char* name() { return "AVX-512"; }
struct vf { __m512 v; };
struct vm { __mmask16 m; };

inline vf zero()                     { return { _mm512_setzero_ps() }; }
inline vf set1(float x)              { return { _mm512_set1_ps(x) }; }
inline vf load(const float* p)       { return { _mm512_load_ps(p) }; }
inline vf loadu(const float* p)      { return { _mm512_loadu_ps(p) }; }
inline void store(float* p, vf a)    { _mm512_store_ps(p, a.v); }
inline void storeu(float* p, vf a)   { _mm512_storeu_ps(p, a.v); }
inline vf operator+(vf a, vf b)      { return { _mm512_add_ps(a.v, b.v) }; }
inline vf operator-(vf a, vf b)      { return { _mm512_sub_ps(a.v, b.v) }; }
inline vf operator*(vf a, vf b)      { return { _mm512_mul_ps(a.v, b.v) }; }
inline vf fma(vf a, vf b, vf c)      { return { _mm512_fmadd_ps(a.v, b.v, c.v) }; } // a*b+c
inline vf min(vf a, vf b)            { return { _mm512_min_ps(a.v, b.v) }; }
inline vf max(vf a, vf b)            { return { _mm512_max_ps(a.v, b.v) }; }
inline vm operator<(vf a, vf b)      { return { _mm512_cmp_ps_mask(a.v, b.v, _CMP_LT_OQ) }; }
inline vm operator>(vf a, vf b)      { return { _mm512_cmp_ps_mask(a.v, b.v, _CMP_GT_OQ) }; }
inline vf select(vm m, vf a, vf b)   { return { _mm512_mask_blend_ps(m.m, b.v, a.v) }; } // m ? a : b
inline float reduceAdd(vf a)         { return _mm512_reduce_add_ps(a.v); }
inline float reduceMin(vf a)         { return _mm512_reduce_min_ps(a.v); }
inline float reduceMax(vf a)         { return _mm512_reduce_max_ps(a.v); }

// 2^n for integer-valued float n, n in [-126, 127]
inline vf roundNearest(vf a)         { return { _mm512_roundscale_ps(a.v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC) }; }
inline vf pow2i(vf n) {
    __m512i i = _mm512_cvtps_epi32(n.v);
    i = _mm512_slli_epi32(_mm512_add_epi32(i, _mm512_set1_epi32(127)), 23);
    return { _mm512_castsi512_ps(i) };
}

#elif NB_SIMD_AVX2

constexpr int W = 8;
inline const char* name() { return "AVX2+FMA"; }
struct vf { __m256 v; };
struct vm { __m256 m; };

inline vf zero()                     { return { _mm256_setzero_ps() }; }
inline vf set1(float x)              { return { _mm256_set1_ps(x) }; }
inline vf load(const float* p)       { return { _mm256_load_ps(p) }; }
inline vf loadu(const float* p)      { return { _mm256_loadu_ps(p) }; }
inline void store(float* p, vf a)    { _mm256_store_ps(p, a.v); }
inline void storeu(float* p, vf a)   { _mm256_storeu_ps(p, a.v); }
inline vf operator+(vf a, vf b)      { return { _mm256_add_ps(a.v, b.v) }; }
inline vf operator-(vf a, vf b)      { return { _mm256_sub_ps(a.v, b.v) }; }
inline vf operator*(vf a, vf b)      { return { _mm256_mul_ps(a.v, b.v) }; }
inline vf fma(vf a, vf b, vf c)      { return { _mm256_fmadd_ps(a.v, b.v, c.v) }; }
inline vf min(vf a, vf b)            { return { _mm256_min_ps(a.v, b.v) }; }
inline vf max(vf a, vf b)            { return { _mm256_max_ps(a.v, b.v) }; }
inline vm operator<(vf a, vf b)      { return { _mm256_cmp_ps(a.v, b.v, _CMP_LT_OQ) }; }
inline vm operator>(vf a, vf b)      { return { _mm256_cmp_ps(a.v, b.v, _CMP_GT_OQ) }; }
inline vf select(vm m, vf a, vf b)   { return { _mm256_blendv_ps(b.v, a.v, m.m) }; }

inline __m128 lo(vf a) { return _mm256_castps256_ps128(a.v); }
inline __m128 hi(vf a) { return _mm256_extractf128_ps(a.v, 1); }
inline float reduceAdd(vf a) {
    __m128 x = _mm_add_ps(lo(a), hi(a));
    x = _mm_add_ps(x, _mm_movehl_ps(x, x));
    return _mm_cvtss_f32(_mm_add_ss(x, _mm_shuffle_ps(x, x, 1)));
}
inline float reduceMin(vf a) {
    __m128 x = _mm_min_ps(lo(a), hi(a));
    x = _mm_min_ps(x, _mm_movehl_ps(x, x));
    return _mm_cvtss_f32(_mm_min_ss(x, _mm_shuffle_ps(x, x, 1)));
}
inline float reduceMax(vf a) {
    __m128 x = _mm_max_ps(lo(a), hi(a));
    x = _mm_max_ps(x, _mm_movehl_ps(x, x));
    return _mm_cvtss_f32(_mm_max_ss(x, _mm_shuffle_ps(x, x, 1)));
}

inline vf roundNearest(vf a)         { return { _mm256_round_ps(a.v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC) }; }
inline vf pow2i(vf n) {
    __m256i i = _mm256_cvtps_epi32(n.v);
    i = _mm256_slli_epi32(_mm256_add_epi32(i, _mm256_set1_epi32(127)), 23);
    return { _mm256_castsi256_ps(i) };
}

#elif NB_SIMD_SSE2

constexpr int W = 4;
inline const char* name() { return "SSE2"; }
struct vf { __m128 v; };
struct vm { __m128 m; };

inline vf zero()                     { return { _mm_setzero_ps() }; }
inline vf set1(float x)              { return { _mm_set1_ps(x) }; }
inline vf load(const float* p)       { return { _mm_load_ps(p) }; }
inline vf loadu(const float* p)      { return { _mm_loadu_ps(p) }; }
inline void store(float* p, vf a)    { _mm_store_ps(p, a.v); }
inline void storeu(float* p, vf a)   { _mm_storeu_ps(p, a.v); }
inline vf operator+(vf a, vf b)      { return { _mm_add_ps(a.v, b.v) }; }
inline vf operator-(vf a, vf b)      { return { _mm_sub_ps(a.v, b.v) }; }
inline vf operator*(vf a, vf b)      { return { _mm_mul_ps(a.v, b.v) }; }
inline vf fma(vf a, vf b, vf c)      { return { _mm_add_ps(_mm_mul_ps(a.v, b.v), c.v) }; } // no FMA on SSE2
inline vf min(vf a, vf b)            { return { _mm_min_ps(a.v, b.v) }; }
inline vf max(vf a, vf b)            { return { _mm_max_ps(a.v, b.v) }; }
inline vm operator<(vf a, vf b)      { return { _mm_cmplt_ps(a.v, b.v) }; }
inline vm operator>(vf a, vf b)      { return { _mm_cmpgt_ps(a.v, b.v) }; }
inline vf select(vm m, vf a, vf b)   { return { _mm_or_ps(_mm_and_ps(m.m, a.v), _mm_andnot_ps(m.m, b.v)) }; }

inline float reduceAdd(vf a) {
    __m128 x = _mm_add_ps(a.v, _mm_movehl_ps(a.v, a.v));
    return _mm_cvtss_f32(_mm_add_ss(x, _mm_shuffle_ps(x, x, 1)));
}
inline float reduceMin(vf a) {
    __m128 x = _mm_min_ps(a.v, _mm_movehl_ps(a.v, a.v));
    return _mm_cvtss_f32(_mm_min_ss(x, _mm_shuffle_ps(x, x, 1)));
}
inline float reduceMax(vf a) {
    __m128 x = _mm_max_ps(a.v, _mm_movehl_ps(a.v, a.v));
    return _mm_cvtss_f32(_mm_max_ss(x, _mm_shuffle_ps(x, x, 1)));
}

// SSE2 has no round_ps. cvtps rounds to nearest-even (default MXCSR), same as the others.
inline vf roundNearest(vf a)         { return { _mm_cvtepi32_ps(_mm_cvtps_epi32(a.v)) }; }
inline vf pow2i(vf n) {
    __m128i i = _mm_cvtps_epi32(n.v);
    i = _mm_slli_epi32(_mm_add_epi32(i, _mm_set1_epi32(127)), 23);
    return { _mm_castsi128_ps(i) };
}

#else // scalar

constexpr int W = 1;
inline const char* name() { return "scalar"; }
struct vf { float v; };
struct vm { bool m; };

inline vf zero()                     { return { 0.0f }; }
inline vf set1(float x)              { return { x }; }
inline vf load(const float* p)       { return { *p }; }
inline vf loadu(const float* p)      { return { *p }; }
inline void store(float* p, vf a)    { *p = a.v; }
inline void storeu(float* p, vf a)   { *p = a.v; }
inline vf operator+(vf a, vf b)      { return { a.v + b.v }; }
inline vf operator-(vf a, vf b)      { return { a.v - b.v }; }
inline vf operator*(vf a, vf b)      { return { a.v * b.v }; }
inline vf fma(vf a, vf b, vf c)      { return { a.v * b.v + c.v }; }
inline vf min(vf a, vf b)            { return { a.v < b.v ? a.v : b.v }; }
inline vf max(vf a, vf b)            { return { a.v > b.v ? a.v : b.v }; }
inline vm operator<(vf a, vf b)      { return { a.v < b.v }; }
inline vm operator>(vf a, vf b)      { return { a.v > b.v }; }
inline vf select(vm m, vf a, vf b)   { return { m.m ? a.v : b.v }; }
inline float reduceAdd(vf a)         { return a.v; }
inline float reduceMin(vf a)         { return a.v; }
inline float reduceMax(vf a)         { return a.v; }

inline vf roundNearest(vf a)         { return { std::nearbyint(a.v) }; }
inline vf pow2i(vf n) {
    int32_t i = ((int32_t)n.v + 127) << 23;
    float f;
    std::memcpy(&f, &i, 4);
    return { f };
}

#endif

// ---------------------------------------------------------------------------
// Shared helpers, written once on top of the ops above.

inline vf& operator+=(vf& a, vf b) { a = a + b; return a; }
inline vf& operator-=(vf& a, vf b) { a = a - b; return a; }
inline vf& operator*=(vf& a, vf b) { a = a * b; return a; }

// Fast e^x. Rel error ~2e-7 in range. x < -87 -> 0. x > 88 clamped.
// e^x = 2^n * 2^f, n = round(x*log2e), f in [-0.5, 0.5], 2^f by poly.
inline vf exp(vf x) {
    const vf lo = set1(-87.0f), hi = set1(88.0f);
    vm under = x < lo;
    x = min(max(x, lo), hi);

    vf t = x * set1(1.44269504f);        // x * log2(e)
    vf n = roundNearest(t);
    vf f = t - n;                        // [-0.5, 0.5]

    // 2^f, Taylor of e^(f*ln2), Horner
    vf p = set1(1.5403530e-4f);
    p = fma(p, f, set1(1.3333558e-3f));
    p = fma(p, f, set1(9.6181291e-3f));
    p = fma(p, f, set1(5.5504109e-2f));
    p = fma(p, f, set1(2.4022651e-1f));
    p = fma(p, f, set1(6.9314718e-1f));
    p = fma(p, f, set1(1.0f));

    return select(under, zero(), p * pow2i(n));
}

// Scalar version of same exp, same math. For tail loops and checks.
inline float expScalar(float x) {
    if (x < -87.0f) return 0.0f;
    if (x > 88.0f) x = 88.0f;
    float t = x * 1.44269504f;
    float n = std::nearbyint(t);
    float f = t - n;
    float p = 1.5403530e-4f;
    p = p * f + 1.3333558e-3f;
    p = p * f + 9.6181291e-3f;
    p = p * f + 5.5504109e-2f;
    p = p * f + 2.4022651e-1f;
    p = p * f + 6.9314718e-1f;
    p = p * f + 1.0f;
    int32_t i = ((int32_t)n + 127) << 23;
    float s;
    std::memcpy(&s, &i, 4);
    return p * s;
}

// Round count up to multiple of W (for padding arrays).
inline size_t padW(size_t n) { return (n + W - 1) / W * W; }

} // namespace simd
