#pragma once

#include <smmintrin.h>

typedef struct { __m128 v; } vec;
typedef struct { __m128i v; } ivec;
typedef struct { __m128d v; } dvec;

static inline vec V(__m128 v) { vec r = { v }; return r; }
static inline ivec I(__m128i v) { ivec r = { v }; return r; }
static inline dvec D(__m128d v) { dvec r = { v }; return r; }

static inline vec load(const float *p) { return V(_mm_loadu_ps(p)); }
static inline void store(float *p, vec a) { _mm_storeu_ps(p, a.v); }
static inline vec splat(float v) { return V(_mm_set1_ps(v)); }
static inline vec vmake(float a, float b, float c, float d) { return V(_mm_setr_ps(a, b, c, d)); }
static inline vec add(vec a, vec b) { return V(_mm_add_ps(a.v, b.v)); }
static inline vec sub(vec a, vec b) { return V(_mm_sub_ps(a.v, b.v)); }
static inline vec mul(vec a, vec b) { return V(_mm_mul_ps(a.v, b.v)); }
static inline vec vdiv(vec a, vec b) { return V(_mm_div_ps(a.v, b.v)); }
static inline vec vmin(vec a, vec b) { return V(_mm_or_ps(_mm_or_ps(_mm_min_ps(a.v, b.v), _mm_min_ps(b.v, a.v)), _mm_cmpunord_ps(a.v, b.v))); }
static inline vec vmax(vec a, vec b) { return V(_mm_or_ps(_mm_and_ps(_mm_max_ps(a.v, b.v), _mm_max_ps(b.v, a.v)), _mm_cmpunord_ps(a.v, b.v))); }
static inline vec vabs(vec a) { return V(_mm_and_ps(a.v, _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)))); }
static inline vec vneg(vec a) { return V(_mm_xor_ps(a.v, _mm_castsi128_ps(_mm_set1_epi32((int)0x80000000)))); }
static inline float lane(vec a, int k) { float t[4]; _mm_storeu_ps(t, a.v); return t[k]; }
static inline vec with(vec a, int k, float x) { float t[4]; _mm_storeu_ps(t, a.v); t[k] = x; return V(_mm_loadu_ps(t)); }
#define vshuffle(a, x, y, z, w) V(_mm_shuffle_ps((a).v, (a).v, _MM_SHUFFLE(w, z, y, x)))

static inline ivec lt(vec a, vec b) { return I(_mm_castps_si128(_mm_cmplt_ps(a.v, b.v))); }
static inline ivec gt(vec a, vec b) { return I(_mm_castps_si128(_mm_cmpgt_ps(a.v, b.v))); }
static inline ivec eq(vec a, vec b) { return I(_mm_castps_si128(_mm_cmpeq_ps(a.v, b.v))); }
static inline ivec ne(vec a, vec b) { return I(_mm_castps_si128(_mm_cmpneq_ps(a.v, b.v))); }
static inline vec vselect(ivec m, vec a, vec b) { __m128 k = _mm_castsi128_ps(m.v); return V(_mm_or_ps(_mm_and_ps(k, a.v), _mm_andnot_ps(k, b.v))); }
static inline vec keep(vec a, ivec m) { return V(_mm_and_ps(a.v, _mm_castsi128_ps(m.v))); }
static inline int all(ivec m) { return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(m.v, _mm_setzero_si128()))) == 0; }

static inline ivec iload(const void *p) { return I(_mm_loadu_si128((const __m128i *)p)); }
static inline void istore(void *p, ivec a) { _mm_storeu_si128((__m128i *)p, a.v); }
static inline ivec iload4(const void *p) { int v; copy(&v, p, 4); return I(_mm_cvtsi32_si128(v)); }
static inline void istore4(void *p, ivec a) { int v = _mm_cvtsi128_si32(a.v); copy(p, &v, 4); }
static inline ivec isplat(int v) { return I(_mm_set1_epi32(v)); }
static inline ivec imake(int a, int b, int c, int d) { return I(_mm_setr_epi32(a, b, c, d)); }
static inline ivec iadd(ivec a, ivec b) { return I(_mm_add_epi32(a.v, b.v)); }
static inline ivec isub(ivec a, ivec b) { return I(_mm_sub_epi32(a.v, b.v)); }
static inline ivec imul(ivec a, ivec b) { return I(_mm_mullo_epi32(a.v, b.v)); }
static inline ivec iabs(ivec a) { return I(_mm_abs_epi32(a.v)); }
static inline ivec ineg(ivec a) { return I(_mm_sub_epi32(_mm_setzero_si128(), a.v)); }
static inline ivec iand(ivec a, ivec b) { return I(_mm_and_si128(a.v, b.v)); }
static inline ivec ior(ivec a, ivec b) { return I(_mm_or_si128(a.v, b.v)); }
static inline ivec ilt(ivec a, ivec b) { return I(_mm_cmplt_epi32(a.v, b.v)); }
static inline ivec igt(ivec a, ivec b) { return I(_mm_cmpgt_epi32(a.v, b.v)); }
static inline ivec ieq(ivec a, ivec b) { return I(_mm_cmpeq_epi32(a.v, b.v)); }
static inline ivec iselect(ivec m, ivec a, ivec b) { return I(_mm_or_si128(_mm_and_si128(m.v, a.v), _mm_andnot_si128(m.v, b.v))); }
#define ishr(a, n) I(_mm_srai_epi32((a).v, n))
#define iushr(a, n) I(_mm_srli_epi32((a).v, n))
static inline int ilane(ivec a, int k) { int t[4]; _mm_storeu_si128((__m128i *)t, a.v); return t[k]; }
#define ishuffle(a, x, y, z, w) I(_mm_shuffle_epi32((a).v, _MM_SHUFFLE(w, z, y, x)))
#define bshuffle(a, ...) I(_mm_shuffle_epi8((a).v, _mm_setr_epi8(__VA_ARGS__)))

static inline vec tofloat(ivec a) { return V(_mm_cvtepi32_ps(a.v)); }
static inline vec tofloatu(ivec a) {
  __m128 hi = _mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(a.v, 16)), _mm_set1_ps(65536.0f));
  return V(_mm_add_ps(hi, _mm_cvtepi32_ps(_mm_and_si128(a.v, _mm_set1_epi32(0xffff)))));
}
static inline ivec vtrunc(vec a) {
  __m128i r = _mm_cvttps_epi32(a.v);
  r = _mm_xor_si128(r, _mm_castps_si128(_mm_cmpge_ps(a.v, _mm_set1_ps(2147483648.0f))));
  return I(_mm_and_si128(r, _mm_castps_si128(_mm_cmpord_ps(a.v, a.v))));
}
static inline ivec bits(vec a) { return I(_mm_castps_si128(a.v)); }
static inline vec floats(ivec a) { return V(_mm_castsi128_ps(a.v)); }
static inline ivec low2(ivec a, ivec b) { return I(_mm_unpacklo_epi64(a.v, b.v)); }
static inline vec vswizzle(vec a, ivec bytes) { return V(_mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(a.v), _mm_or_si128(bytes.v, _mm_cmpgt_epi8(bytes.v, _mm_set1_epi8(15)))))); }
static inline ivec bytes4(ivec a) { return I(_mm_cvtepu8_epi32(a.v)); }
static inline ivec narrow(ivec a, ivec b, ivec c, ivec d) { return I(_mm_packus_epi16(_mm_packus_epi32(a.v, b.v), _mm_packus_epi32(c.v, d.v))); }

static inline dvec dsplat(double v) { return D(_mm_set1_pd(v)); }
static inline dvec dmake(double a, double b) { return D(_mm_setr_pd(a, b)); }
static inline dvec dadd(dvec a, dvec b) { return D(_mm_add_pd(a.v, b.v)); }
static inline dvec dmin(dvec a, dvec b) { return D(_mm_or_pd(_mm_or_pd(_mm_min_pd(a.v, b.v), _mm_min_pd(b.v, a.v)), _mm_cmpunord_pd(a.v, b.v))); }
static inline dvec dmax(dvec a, dvec b) { return D(_mm_or_pd(_mm_and_pd(_mm_max_pd(a.v, b.v), _mm_max_pd(b.v, a.v)), _mm_cmpunord_pd(a.v, b.v))); }
static inline dvec dfloor(dvec a) { return D(_mm_floor_pd(a.v)); }
static inline dvec dlow(vec a) { return D(_mm_cvtps_pd(a.v)); }
static inline dvec dhigh(vec a) { return D(_mm_cvtps_pd(_mm_movehl_ps(a.v, a.v))); }
static inline ivec dtrunc(dvec a) {
  __m128i r = _mm_cvttpd_epi32(a.v);
  __m128i big = _mm_shuffle_epi32(_mm_castpd_si128(_mm_cmpge_pd(a.v, _mm_set1_pd(2147483648.0))), _MM_SHUFFLE(3, 3, 2, 0));
  __m128i ok = _mm_shuffle_epi32(_mm_castpd_si128(_mm_cmpord_pd(a.v, a.v)), _MM_SHUFFLE(3, 3, 2, 0));
  r = _mm_xor_si128(r, _mm_and_si128(big, _mm_setr_epi32(-1, -1, 0, 0)));
  return I(_mm_and_si128(r, ok));
}
static inline double dlane(dvec a, int k) { double t[2]; _mm_storeu_pd(t, a.v); return t[k]; }
