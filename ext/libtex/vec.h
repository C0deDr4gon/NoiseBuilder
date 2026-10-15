#pragma once

#if defined(__wasm_simd128__)
#include "vec_wasm.h"
#elif !defined(LIBTEX_SCALAR) && (defined(__SSE4_1__) || defined(__AVX__))
#include "vec_sse.h"
#else
#include "vec_scalar.h"
#endif

static inline ivec mask(int n) { return igt(isplat(n), imake(0, 1, 2, 3)); }

static inline float hsum(vec v) {
  v = add(v, vshuffle(v, 2, 3, 0, 1));
  v = add(v, vshuffle(v, 1, 0, 3, 2));
  return lane(v, 0);
}

static inline float dot(vec a, vec b) { return hsum(mul(a, b)); }

static inline vec log2_1p(vec v) {
  ivec b = bits(add(vabs(v), splat(1)));
  vec e = tofloat(isub(iushr(b, 23), isplat(127)));
  vec m = floats(ior(iand(b, isplat(0x007fffff)), isplat(0x3f800000)));
  vec t = vdiv(sub(m, splat(1)), add(m, splat(1)));
  vec t2 = mul(t, t);
  vec s = splat(1.0f / 11);
  s = add(mul(s, t2), splat(1.0f / 9));
  s = add(mul(s, t2), splat(1.0f / 7));
  s = add(mul(s, t2), splat(1.0f / 5));
  s = add(mul(s, t2), splat(1.0f / 3));
  s = add(mul(s, t2), splat(1));
  vec l = add(e, mul(mul(t, s), splat(2.8853900817779268f)));
  return floats(ior(bits(l), iand(bits(v), isplat(0x80000000))));
}
