#pragma once

#include <wasm_simd128.h>

typedef struct { v128_t v; } vec;
typedef struct { v128_t v; } ivec;
typedef struct { v128_t v; } dvec;

static inline vec V(v128_t v) { vec r = { v }; return r; }
static inline ivec I(v128_t v) { ivec r = { v }; return r; }
static inline dvec D(v128_t v) { dvec r = { v }; return r; }

static inline vec load(const float *p) { return V(wasm_v128_load(p)); }
static inline void store(float *p, vec a) { wasm_v128_store(p, a.v); }
static inline vec splat(float v) { return V(wasm_f32x4_splat(v)); }
static inline vec vmake(float a, float b, float c, float d) { return V(wasm_f32x4_make(a, b, c, d)); }
static inline vec add(vec a, vec b) { return V(wasm_f32x4_add(a.v, b.v)); }
static inline vec sub(vec a, vec b) { return V(wasm_f32x4_sub(a.v, b.v)); }
static inline vec mul(vec a, vec b) { return V(wasm_f32x4_mul(a.v, b.v)); }
static inline vec vdiv(vec a, vec b) { return V(wasm_f32x4_div(a.v, b.v)); }
static inline vec vmin(vec a, vec b) { return V(wasm_f32x4_min(a.v, b.v)); }
static inline vec vmax(vec a, vec b) { return V(wasm_f32x4_max(a.v, b.v)); }
static inline vec vabs(vec a) { return V(wasm_f32x4_abs(a.v)); }
static inline vec vneg(vec a) { return V(wasm_f32x4_neg(a.v)); }
#define lane(a, k) wasm_f32x4_extract_lane((a).v, k)
#define with(a, k, x) V(wasm_f32x4_replace_lane((a).v, k, x))
#define vshuffle(a, x, y, z, w) V(wasm_i32x4_shuffle((a).v, (a).v, x, y, z, w))

static inline ivec lt(vec a, vec b) { return I(wasm_f32x4_lt(a.v, b.v)); }
static inline ivec gt(vec a, vec b) { return I(wasm_f32x4_gt(a.v, b.v)); }
static inline ivec eq(vec a, vec b) { return I(wasm_f32x4_eq(a.v, b.v)); }
static inline ivec ne(vec a, vec b) { return I(wasm_f32x4_ne(a.v, b.v)); }
static inline vec vselect(ivec m, vec a, vec b) { return V(wasm_v128_bitselect(a.v, b.v, m.v)); }
static inline vec keep(vec a, ivec m) { return V(wasm_v128_and(a.v, m.v)); }
static inline int all(ivec m) { return wasm_i32x4_all_true(m.v); }

static inline ivec iload(const void *p) { return I(wasm_v128_load(p)); }
static inline void istore(void *p, ivec a) { wasm_v128_store(p, a.v); }
static inline ivec iload4(const void *p) { return I(wasm_v128_load32_zero(p)); }
static inline void istore4(void *p, ivec a) { wasm_v128_store32_lane(p, a.v, 0); }
static inline ivec isplat(int v) { return I(wasm_i32x4_splat(v)); }
static inline ivec imake(int a, int b, int c, int d) { return I(wasm_i32x4_make(a, b, c, d)); }
static inline ivec iadd(ivec a, ivec b) { return I(wasm_i32x4_add(a.v, b.v)); }
static inline ivec isub(ivec a, ivec b) { return I(wasm_i32x4_sub(a.v, b.v)); }
static inline ivec imul(ivec a, ivec b) { return I(wasm_i32x4_mul(a.v, b.v)); }
static inline ivec iabs(ivec a) { return I(wasm_i32x4_abs(a.v)); }
static inline ivec ineg(ivec a) { return I(wasm_i32x4_neg(a.v)); }
static inline ivec iand(ivec a, ivec b) { return I(wasm_v128_and(a.v, b.v)); }
static inline ivec ior(ivec a, ivec b) { return I(wasm_v128_or(a.v, b.v)); }
static inline ivec ilt(ivec a, ivec b) { return I(wasm_i32x4_lt(a.v, b.v)); }
static inline ivec igt(ivec a, ivec b) { return I(wasm_i32x4_gt(a.v, b.v)); }
static inline ivec ieq(ivec a, ivec b) { return I(wasm_i32x4_eq(a.v, b.v)); }
static inline ivec iselect(ivec m, ivec a, ivec b) { return I(wasm_v128_bitselect(a.v, b.v, m.v)); }
#define ishr(a, n) I(wasm_i32x4_shr((a).v, n))
#define iushr(a, n) I(wasm_u32x4_shr((a).v, n))
#define ilane(a, k) wasm_i32x4_extract_lane((a).v, k)
#define ishuffle(a, x, y, z, w) I(wasm_i32x4_shuffle((a).v, (a).v, x, y, z, w))
#define bshuffle(a, ...) I(wasm_i8x16_shuffle((a).v, (a).v, __VA_ARGS__))

static inline vec tofloat(ivec a) { return V(wasm_f32x4_convert_i32x4(a.v)); }
static inline vec tofloatu(ivec a) { return V(wasm_f32x4_convert_u32x4(a.v)); }
static inline ivec vtrunc(vec a) { return I(wasm_i32x4_trunc_sat_f32x4(a.v)); }
static inline ivec bits(vec a) { return I(a.v); }
static inline vec floats(ivec a) { return V(a.v); }
static inline ivec low2(ivec a, ivec b) { return I(wasm_i32x4_shuffle(a.v, b.v, 0, 1, 4, 5)); }
static inline vec vswizzle(vec a, ivec bytes) { return V(wasm_i8x16_swizzle(a.v, bytes.v)); }
static inline ivec bytes4(ivec a) { return I(wasm_u32x4_extend_low_u16x8(wasm_u16x8_extend_low_u8x16(a.v))); }
static inline ivec narrow(ivec a, ivec b, ivec c, ivec d) { return I(wasm_u8x16_narrow_i16x8(wasm_u16x8_narrow_i32x4(a.v, b.v), wasm_u16x8_narrow_i32x4(c.v, d.v))); }

static inline dvec dsplat(double v) { return D(wasm_f64x2_splat(v)); }
static inline dvec dmake(double a, double b) { return D(wasm_f64x2_make(a, b)); }
static inline dvec dadd(dvec a, dvec b) { return D(wasm_f64x2_add(a.v, b.v)); }
static inline dvec dmin(dvec a, dvec b) { return D(wasm_f64x2_min(a.v, b.v)); }
static inline dvec dmax(dvec a, dvec b) { return D(wasm_f64x2_max(a.v, b.v)); }
static inline dvec dfloor(dvec a) { return D(wasm_f64x2_floor(a.v)); }
static inline dvec dlow(vec a) { return D(wasm_f64x2_promote_low_f32x4(a.v)); }
static inline dvec dhigh(vec a) { return D(wasm_f64x2_promote_low_f32x4(wasm_i32x4_shuffle(a.v, a.v, 2, 3, 0, 1))); }
static inline ivec dtrunc(dvec a) { return I(wasm_i32x4_trunc_sat_f64x2_zero(a.v)); }
#define dlane(a, k) wasm_f64x2_extract_lane((a).v, k)
