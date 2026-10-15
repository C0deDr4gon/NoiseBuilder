#pragma once

#include "platform.h"
#include "vec.h"

typedef struct {
  ALIGN16 float c[4][16];
  int count;
} Pal;

typedef struct {
  vec v[4], w[4];
} Probe;

static inline void pal_set(Pal *pal, int i, vec v) {
  pal->c[0][i] = lane(v, 0);
  pal->c[1][i] = lane(v, 1);
  pal->c[2][i] = lane(v, 2);
  pal->c[3][i] = lane(v, 3);
}

static vec mean(const vec *pts, int count) {
  vec m = splat(0);
  for (int i = 0; i < count; i++) m = add(m, pts[i]);
  return mul(m, splat(1.0f / count));
}

static vec axis(const vec *pts, int count, vec m, ivec lanes) {
  vec c0 = splat(0), c1 = c0, c2 = c0, c3 = c0;
  for (int p = 0; p < count; p++) {
    vec d = keep(sub(pts[p], m), lanes);
    c0 = add(c0, mul(d, splat(lane(d, 0))));
    c1 = add(c1, mul(d, splat(lane(d, 1))));
    c2 = add(c2, mul(d, splat(lane(d, 2))));
    c3 = add(c3, mul(d, splat(lane(d, 3))));
  }
  vec v = keep(splat(1), lanes);
  for (int it = 0; it < 8; it++) {
    vec w = add(add(mul(c0, splat(lane(v, 0))), mul(c1, splat(lane(v, 1)))), add(mul(c2, splat(lane(v, 2))), mul(c3, splat(lane(v, 3)))));
    float len = dot(w, w);
    if (!len) break;
    vec n = mul(w, splat(1 / fsqrtf(len)));
    vec d = sub(n, v);
    v = n;
    if (dot(d, d) < 1e-6f) break;
  }
  return v;
}

static void endpoints(const vec *pts, int count, int n, vec *hi, vec *lo) {
  ivec lanes = mask(n);
  vec m = mean(pts, count);
  vec v = axis(pts, count, m, lanes);
  float a = (float)finf(), b = -(float)finf();
  for (int p = 0; p < count; p++) {
    float t = dot(sub(pts[p], m), v);
    if (t < a) a = t;
    if (t > b) b = t;
  }
  *hi = add(m, mul(v, splat(b)));
  *lo = add(m, mul(v, splat(a)));
}

static void pal_init(Pal *pal, int count) {
  pal->count = count;
  for (int i = count; i < ((count + 3) & ~3); i++) for (int k = 0; k < 4; k++) pal->c[k][i] = 1e18f;
}

static inline vec square(vec v) { return mul(v, v); }

static inline ivec fold(vec *d, ivec i, vec d2, ivec i2) {
  ivec take = ior(lt(d2, *d), iand(eq(d2, *d), ilt(i2, i)));
  *d = vselect(take, d2, *d);
  return iselect(take, i2, i);
}

static inline int closest(vec d, ivec i) {
  i = fold(&d, i, vshuffle(d, 2, 3, 0, 1), ishuffle(i, 2, 3, 0, 1));
  i = fold(&d, i, vshuffle(d, 1, 0, 3, 2), ishuffle(i, 1, 0, 3, 2));
  return ilane(i, 0);
}

static void probe(Probe *p, vec px, vec w) {
  p->v[0] = splat(lane(px, 0)), p->v[1] = splat(lane(px, 1)), p->v[2] = splat(lane(px, 2)), p->v[3] = splat(lane(px, 3));
  p->w[0] = splat(lane(w, 0)), p->w[1] = splat(lane(w, 1)), p->w[2] = splat(lane(w, 2)), p->w[3] = splat(lane(w, 3));
}

static int pick_p(const Probe *p, const Pal *pal, int n) {
  vec best = splat((float)finf());
  ivec index = isplat(0);
  for (int i = 0; i < pal->count; i += 4) {
    vec d = add(add(mul(p->w[0], square(sub(p->v[0], load(pal->c[0] + i)))), mul(p->w[1], square(sub(p->v[1], load(pal->c[1] + i))))), mul(p->w[2], square(sub(p->v[2], load(pal->c[2] + i)))));
    if (n == 4) d = add(d, mul(p->w[3], square(sub(p->v[3], load(pal->c[3] + i)))));
    ivec closer = lt(d, best);
    best = vselect(closer, d, best);
    index = iselect(closer, iadd(isplat(i), imake(0, 1, 2, 3)), index);
  }
  return closest(best, index);
}

static int pick_w(vec px, vec w, const Pal *pal, int n) {
  Probe p;
  probe(&p, px, w);
  return pick_p(&p, pal, n);
}

static int lsqf(const vec *px, const vec *w, const unsigned char *in, const unsigned char *idx, const float *ts, vec *a, vec *z) {
  vec aa = splat(0), bb = splat(0), ab = splat(0), ax = splat(0), bx = splat(0);
  for (int i = 0; i < 16; i++) {
    if (!in[i]) continue;
    float t = ts[idx[i]];
    vec k = w ? w[i] : splat(1);
    aa = add(aa, mul(k, splat((1 - t) * (1 - t))));
    bb = add(bb, mul(k, splat(t * t)));
    ab = add(ab, mul(k, splat((1 - t) * t)));
    ax = add(ax, mul(mul(px[i], k), splat(1 - t)));
    bx = add(bx, mul(mul(px[i], k), splat(t)));
  }
  vec det = sub(mul(aa, bb), mul(ab, ab));
  if (!all(gt(det, splat(1e-6f)))) return 0;
  *a = vdiv(sub(mul(ax, bb), mul(bx, ab)), det);
  *z = vdiv(sub(mul(bx, aa), mul(ax, ab)), det);
  return 1;
}

static int lsq(const vec *px, const vec *w, const unsigned char *in, const unsigned char *idx, const int *wt, vec *a, vec *z) {
  float t[16];
  for (int i = 0; i < 16; i++) t[idx[i]] = wt[idx[i]] / 64.0f;
  return lsqf(px, w, in, idx, t, a, z);
}

static int rank(float *errors, int *found, int cap, float e, void *items, int size) {
  if (*found == cap && e >= errors[cap - 1]) return -1;
  int at = *found < cap ? (*found)++ : cap - 1;
  char *p = items;
  for (; at > 0 && errors[at - 1] > e; at--) {
    errors[at] = errors[at - 1];
    copy(p + at * size, p + (at - 1) * size, size);
  }
  errors[at] = e;
  return at;
}

static void corner(const vec *pts, int count, int kind, vec *a, vec *z) {
  vec lo = splat(1e9f), hi = splat(-1e9f);
  for (int i = 0; i < count; i++) lo = vmin(lo, pts[i]), hi = vmax(hi, pts[i]);
  ivec flip = imake(0, -(kind & 1), -(kind >> 1 & 1), 0);
  *a = vselect(flip, hi, lo);
  *z = vselect(flip, lo, hi);
}
