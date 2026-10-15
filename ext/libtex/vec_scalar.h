#pragma once

typedef union { float f[4]; int i[4]; unsigned u[4]; double d[2]; unsigned long long q[2]; unsigned char b[16]; } v128;
typedef struct { v128 v; } vec;
typedef struct { v128 v; } ivec;
typedef struct { v128 v; } dvec;

static inline vec V(v128 v) { vec r = { v }; return r; }
static inline ivec I(v128 v) { ivec r = { v }; return r; }
static inline dvec D(v128 v) { dvec r = { v }; return r; }

static inline float fbits(unsigned u) { v128 t; t.u[0] = u; return t.f[0]; }
static inline unsigned ubits(float f) { v128 t; t.f[0] = f; return t.u[0]; }
static inline float fmin4(float a, float b) { return a != a || b != b ? a + b : a < b ? a : b < a ? b : fbits(ubits(a) | ubits(b)); }
static inline float fmax4(float a, float b) { return a != a || b != b ? a + b : a > b ? a : b > a ? b : fbits(ubits(a) & ubits(b)); }
static inline int sat32(double v) { return v != v ? 0 : v >= 2147483648.0 ? 2147483647 : v <= -2147483648.0 ? (int)(-2147483647 - 1) : (int)v; }

static inline vec load(const float *p) { v128 r; copy(&r, p, 16); return V(r); }
static inline void store(float *p, vec a) { copy(p, &a.v, 16); }
static inline vec vmake(float a, float b, float c, float d) { v128 r; r.f[0] = a; r.f[1] = b; r.f[2] = c; r.f[3] = d; return V(r); }
static inline vec splat(float v) { return vmake(v, v, v, v); }
static inline vec add(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = a.v.f[k] + b.v.f[k]; return a; }
static inline vec sub(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = a.v.f[k] - b.v.f[k]; return a; }
static inline vec mul(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = a.v.f[k] * b.v.f[k]; return a; }
static inline vec vdiv(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = a.v.f[k] / b.v.f[k]; return a; }
static inline vec vmin(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = fmin4(a.v.f[k], b.v.f[k]); return a; }
static inline vec vmax(vec a, vec b) { for (int k = 0; k < 4; k++) a.v.f[k] = fmax4(a.v.f[k], b.v.f[k]); return a; }
static inline vec vabs(vec a) { for (int k = 0; k < 4; k++) a.v.u[k] &= 0x7fffffffu; return a; }
static inline vec vneg(vec a) { for (int k = 0; k < 4; k++) a.v.u[k] ^= 0x80000000u; return a; }
static inline float lane(vec a, int k) { return a.v.f[k]; }
static inline vec with(vec a, int k, float x) { a.v.f[k] = x; return a; }
static inline vec vshuffle4(vec a, int x, int y, int z, int w) { return vmake(a.v.f[x], a.v.f[y], a.v.f[z], a.v.f[w]); }
#define vshuffle(a, x, y, z, w) vshuffle4(a, x, y, z, w)

static inline ivec imake(int a, int b, int c, int d) { v128 r; r.i[0] = a; r.i[1] = b; r.i[2] = c; r.i[3] = d; return I(r); }
static inline ivec isplat(int v) { return imake(v, v, v, v); }
static inline ivec lt(vec a, vec b) { ivec r; for (int k = 0; k < 4; k++) r.v.i[k] = a.v.f[k] < b.v.f[k] ? -1 : 0; return r; }
static inline ivec gt(vec a, vec b) { ivec r; for (int k = 0; k < 4; k++) r.v.i[k] = a.v.f[k] > b.v.f[k] ? -1 : 0; return r; }
static inline ivec eq(vec a, vec b) { ivec r; for (int k = 0; k < 4; k++) r.v.i[k] = a.v.f[k] == b.v.f[k] ? -1 : 0; return r; }
static inline ivec ne(vec a, vec b) { ivec r; for (int k = 0; k < 4; k++) r.v.i[k] = a.v.f[k] != b.v.f[k] ? -1 : 0; return r; }
static inline vec vselect(ivec m, vec a, vec b) { for (int k = 0; k < 4; k++) a.v.u[k] = (a.v.u[k] & m.v.u[k]) | (b.v.u[k] & ~m.v.u[k]); return a; }
static inline vec keep(vec a, ivec m) { for (int k = 0; k < 4; k++) a.v.u[k] &= m.v.u[k]; return a; }
static inline int all(ivec m) { return m.v.i[0] && m.v.i[1] && m.v.i[2] && m.v.i[3]; }

static inline ivec iload(const void *p) { v128 r; copy(&r, p, 16); return I(r); }
static inline void istore(void *p, ivec a) { copy(p, &a.v, 16); }
static inline ivec iload4(const void *p) { ivec r = isplat(0); copy(&r.v, p, 4); return r; }
static inline void istore4(void *p, ivec a) { copy(p, &a.v, 4); }
static inline ivec iadd(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] += b.v.u[k]; return a; }
static inline ivec isub(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] -= b.v.u[k]; return a; }
static inline ivec imul(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] *= b.v.u[k]; return a; }
static inline ivec iabs(ivec a) { for (int k = 0; k < 4; k++) a.v.u[k] = a.v.i[k] < 0 ? 0u - a.v.u[k] : a.v.u[k]; return a; }
static inline ivec ineg(ivec a) { for (int k = 0; k < 4; k++) a.v.u[k] = 0u - a.v.u[k]; return a; }
static inline ivec iand(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] &= b.v.u[k]; return a; }
static inline ivec ior(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] |= b.v.u[k]; return a; }
static inline ivec ilt(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.i[k] = a.v.i[k] < b.v.i[k] ? -1 : 0; return a; }
static inline ivec igt(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.i[k] = a.v.i[k] > b.v.i[k] ? -1 : 0; return a; }
static inline ivec ieq(ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.i[k] = a.v.i[k] == b.v.i[k] ? -1 : 0; return a; }
static inline ivec iselect(ivec m, ivec a, ivec b) { for (int k = 0; k < 4; k++) a.v.u[k] = (a.v.u[k] & m.v.u[k]) | (b.v.u[k] & ~m.v.u[k]); return a; }
static inline ivec ishr4(ivec a, int n) { for (int k = 0; k < 4; k++) a.v.i[k] = a.v.i[k] < 0 ? (int)~(~a.v.u[k] >> n) : (int)(a.v.u[k] >> n); return a; }
static inline ivec iushr4(ivec a, int n) { for (int k = 0; k < 4; k++) a.v.u[k] >>= n; return a; }
#define ishr(a, n) ishr4(a, (n) & 31)
#define iushr(a, n) iushr4(a, (n) & 31)
static inline int ilane(ivec a, int k) { return a.v.i[k]; }
static inline ivec ishuffle4(ivec a, int x, int y, int z, int w) { return imake(a.v.i[x], a.v.i[y], a.v.i[z], a.v.i[w]); }
#define ishuffle(a, x, y, z, w) ishuffle4(a, x, y, z, w)
static inline ivec bshuffle16(ivec a, const unsigned char *s) { ivec r; for (int k = 0; k < 16; k++) r.v.b[k] = a.v.b[s[k] & 15]; return r; }
#define bshuffle(a, ...) bshuffle16(a, (const unsigned char[16]){ __VA_ARGS__ })

static inline vec tofloat(ivec a) { vec r; for (int k = 0; k < 4; k++) r.v.f[k] = (float)a.v.i[k]; return r; }
static inline vec tofloatu(ivec a) { vec r; for (int k = 0; k < 4; k++) r.v.f[k] = (float)a.v.u[k]; return r; }
static inline ivec vtrunc(vec a) { ivec r; for (int k = 0; k < 4; k++) r.v.i[k] = sat32(a.v.f[k]); return r; }
static inline ivec bits(vec a) { return I(a.v); }
static inline vec floats(ivec a) { return V(a.v); }
static inline ivec low2(ivec a, ivec b) { return imake(a.v.i[0], a.v.i[1], b.v.i[0], b.v.i[1]); }
static inline vec vswizzle(vec a, ivec bytes) { vec r; for (int k = 0; k < 16; k++) r.v.b[k] = bytes.v.b[k] < 16 ? a.v.b[bytes.v.b[k]] : 0; return r; }
static inline ivec bytes4(ivec a) { return imake(a.v.b[0], a.v.b[1], a.v.b[2], a.v.b[3]); }
static inline int sat16(int v) { return v < 0 ? 0 : v > 65535 ? 65535 : v; }
static inline unsigned char sat8(int v) { short s = (short)(unsigned short)v; return (unsigned char)(s < 0 ? 0 : s > 255 ? 255 : s); }
static inline ivec narrow(ivec a, ivec b, ivec c, ivec d) {
  ivec r;
  for (int k = 0; k < 4; k++) {
    r.v.b[k] = sat8(sat16(a.v.i[k]));
    r.v.b[k + 4] = sat8(sat16(b.v.i[k]));
    r.v.b[k + 8] = sat8(sat16(c.v.i[k]));
    r.v.b[k + 12] = sat8(sat16(d.v.i[k]));
  }
  return r;
}

static inline double dmin2(double a, double b) { v128 x, y; x.d[0] = a; y.d[0] = b; if (a != a || b != b) return a + b; if (a < b) return a; if (b < a) return b; x.q[0] |= y.q[0]; return x.d[0]; }
static inline double dmax2(double a, double b) { v128 x, y; x.d[0] = a; y.d[0] = b; if (a != a || b != b) return a + b; if (a > b) return a; if (b > a) return b; x.q[0] &= y.q[0]; return x.d[0]; }
static inline dvec dmake(double a, double b) { v128 r; r.d[0] = a; r.d[1] = b; return D(r); }
static inline dvec dsplat(double v) { return dmake(v, v); }
static inline dvec dadd(dvec a, dvec b) { return dmake(a.v.d[0] + b.v.d[0], a.v.d[1] + b.v.d[1]); }
static inline dvec dmin(dvec a, dvec b) { return dmake(dmin2(a.v.d[0], b.v.d[0]), dmin2(a.v.d[1], b.v.d[1])); }
static inline dvec dmax(dvec a, dvec b) { return dmake(dmax2(a.v.d[0], b.v.d[0]), dmax2(a.v.d[1], b.v.d[1])); }
static inline dvec dfloor(dvec a) { return dmake(ffloor(a.v.d[0]), ffloor(a.v.d[1])); }
static inline dvec dlow(vec a) { return dmake(a.v.f[0], a.v.f[1]); }
static inline dvec dhigh(vec a) { return dmake(a.v.f[2], a.v.f[3]); }
static inline ivec dtrunc(dvec a) { return imake(sat32(a.v.d[0]), sat32(a.v.d[1]), 0, 0); }
static inline double dlane(dvec a, int k) { return a.v.d[k]; }
