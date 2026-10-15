#pragma once

#if defined(__wasm__)
#define ALIGN16 __attribute__((aligned(16)))
#define THREAD _Thread_local
static inline double fsqrt(double v) { return __builtin_sqrt(v); }
static inline float fsqrtf(float v) { return __builtin_sqrtf(v); }
static inline double ffloor(double v) { return __builtin_floor(v); }
static inline double fceil(double v) { return __builtin_ceil(v); }
static inline double fnan(void) { return __builtin_nan(""); }
static inline double finf(void) { return __builtin_inf(); }
static inline void copy(void *d, const void *s, unsigned n) { __builtin_memcpy(d, s, n); }
static inline void zero(void *d, unsigned n) { __builtin_memset(d, 0, n); }
#else
#include <math.h>
#include <string.h>
#if defined(_MSC_VER)
#define ALIGN16 __declspec(align(16))
#define THREAD __declspec(thread)
#else
#define ALIGN16 __attribute__((aligned(16)))
#define THREAD _Thread_local
#endif
static inline double fsqrt(double v) { return sqrt(v); }
static inline float fsqrtf(float v) { return sqrtf(v); }
static inline double ffloor(double v) { return floor(v); }
static inline double fceil(double v) { return ceil(v); }
static inline double fnan(void) { return (double)NAN; }
static inline double finf(void) { return (double)INFINITY; }
static inline void copy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
static inline void zero(void *d, unsigned n) { memset(d, 0, n); }
#endif

typedef unsigned long long u64;

static inline double fround(double v) { return ffloor(v + 0.5); }
static inline double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
