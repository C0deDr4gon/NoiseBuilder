#pragma once

#include "platform.h"
#include "bits.h"
#include "codec.h"
#include "fit.h"
#include "bc7.h"
#include "partition.h"

typedef struct {
  vec weight[16];
  Probe probes[4][16];
  unsigned char held_idx[16];
  float held_e[16];
  Block seen[8];
  float errors[8];
  int found;
  vec two[64][3][2], three[16][3][2];
} Enc7;

static dvec rounded(dvec x, dvec max) {
  return dmin(dmax(dfloor(dadd(x, dsplat(0.5))), dsplat(0)), max);
}

static void quantize(vec e, const Mode *m, int p, int *q) {
  int cm = (1 << m->cb) - 1, am = m->ab ? (1 << m->ab) - 1 : 0;
  vec max = vmake(cm, cm, cm, am);
  vec v = vdiv(e, splat(255));
  vec x = p < 0 ? mul(v, max) : vdiv(sub(mul(v, add(mul(max, splat(2)), splat(1))), splat(p)), splat(2));
  dvec lo = rounded(dlow(x), dmake(cm, cm));
  dvec hi = rounded(dhigh(x), dmake(cm, am));
  istore(q, low2(dtrunc(lo), dtrunc(hi)));
}

static float error(vec e, const int *q, const Mode *m, int p) {
  int x[4];
  expand_endpoint(q, m, p, x);
  vec d = sub(vmake(x[0], x[1], x[2], x[3]), e);
  return dot(d, d);
}

static const int EPB[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
static const int SPB[2][2] = { { 0, 0 }, { 1, 1 } };
static const int NOP[1][2] = { { -1, -1 } };

static void quantize_pair(Block *b, int s, vec lo, vec hi, const Mode *m) {
  const int (*pairs)[2] = m->epb ? EPB : m->spb ? SPB : NOP;
  int count = m->epb ? 4 : m->spb ? 2 : 1, n = count > 1 ? 2 : 1;
  int qa[2][4], qz[2][4];
  float ea[2], ez[2];
  for (int p = 0; p < n; p++) {
    int pb = n > 1 ? p : -1;
    quantize(lo, m, pb, qa[p]);
    quantize(hi, m, pb, qz[p]);
    ea[p] = error(lo, qa[p], m, pb);
    ez[p] = error(hi, qz[p], m, pb);
  }
  int best = 0;
  float least = (float)finf();
  for (int i = 0; i < count; i++) {
    float c = ea[n > 1 ? pairs[i][0] : 0] + ez[n > 1 ? pairs[i][1] : 0];
    if (c < least) least = c, best = i;
  }
  int pa = pairs[best][0], pz = pairs[best][1];
  copy(b->ep[s * 2], qa[n > 1 ? pa : 0], sizeof qa[0]);
  copy(b->ep[s * 2 + 1], qz[n > 1 ? pz : 0], sizeof qz[0]);
  if (m->epb) b->pbits[s * 2] = pa, b->pbits[s * 2 + 1] = pz;
  if (m->spb) b->pbits[s] = pa;
}

static void line(const vec *pts, int count, const Mode *m, vec *hi, vec *lo) {
  endpoints(pts, count, m->ab && !m->ib2 ? 4 : 3, hi, lo);
  float amax = 0, amin = 255;
  for (int i = 0; i < count; i++) {
    float a = lane(pts[i], 3);
    if (a > amax) amax = a;
    if (a < amin) amin = a;
  }
  if (m->ib2) {
    *hi = with(*hi, 3, amax);
    *lo = with(*lo, 3, amin);
  } else if (!m->ab) {
    *hi = with(*hi, 3, 255);
    *lo = with(*lo, 3, 255);
  }
}

static void flip(Block *b, int s, int c0, int c1, unsigned char *list, int bits, const Mode *m) {
  for (int c = c0; c < c1; c++) {
    int t = b->ep[s * 2][c];
    b->ep[s * 2][c] = b->ep[s * 2 + 1][c];
    b->ep[s * 2 + 1][c] = t;
  }
  for (int i = 0; i < 16; i++) if (subset(m, b->partition, i) == s) list[i] = (1 << bits) - 1 - list[i];
}

static void ramp(const int *a, const int *z, int bits, Pal *out) {
  const int *w = weights(bits);
  out->count = 1 << bits;
  for (int k = 0; k < 4; k++) for (int i = 0; i < 1 << bits; i += 4) store(out->c[k] + i, tofloat(lerp4(isplat(a[k]), isplat(z[k]), iload(w + i))));
}

static void lines(const Mode *m, int partition, const vec *px, vec ends[3][2]) {
  for (int s = 0; s < m->ns; s++) {
    vec pts[16];
    int count = 0;
    for (int i = 0; i < 16; i++) if (subset(m, partition, i) == s) pts[count++] = px[i];
    line(pts, count, m, &ends[s][0], &ends[s][1]);
  }
}

static vec weights_of(Enc7 *x, int rotation, int i) { return rotate(x->weight[i], rotation); }


static float miss(Enc7 *x, vec c, vec p, int rotation, int i) {
  vec d = sub(rotate(c, rotation), rotate(p, rotation));
  return hsum(mul(mul(d, d), x->weight[i]));
}

static float term(Enc7 *x, const Pal *r, int j, vec p, int rotation, int i) {
  return miss(x, vmake(r->c[0][j], r->c[1][j], r->c[2][j], r->c[3][j]), p, rotation, i);
}

static void hold(Enc7 *x, const Block *b, const vec *px) {
  const Mode *m = &MODES[b->mode];
  int E[6][4];
  Pal ramps[3];
  for (int e = 0; e < m->ns * 2; e++) expand_endpoint(b->ep[e], m, pbit_of(b, m, e), E[e]);
  for (int s = 0; s < m->ns; s++) ramp(E[s * 2], E[s * 2 + 1], m->ib, &ramps[s]);
  for (int i = 0; i < 16; i++) {
    const Pal *r = &ramps[subset(m, b->partition, i)];
    x->held_idx[i] = pick_p(&x->probes[b->rotation][i], r, 4);
    x->held_e[i] = term(x, r, x->held_idx[i], px[i], b->rotation, i);
  }
}

static float assign(Enc7 *x, Block *b, const vec *px, float limit, int only) {
  const Mode *m = &MODES[b->mode];
  int partition = b->partition, isel = b->isel;
  int E[6][4];
  for (int e = 0; e < m->ns * 2; e++) if (only < 0 || e >> 1 == only) expand_endpoint(b->ep[e], m, pbit_of(b, m, e), E[e]);
  int cb = m->ib2 && isel ? m->ib2 : m->ib;
  int ab = m->ib2 && !isel ? m->ib2 : m->ib;
  Pal ramps[3];
  for (int s = 0; s < m->ns; s++) if (only < 0 || s == only) ramp(E[s * 2], E[s * 2 + 1], cb, &ramps[s]);
  unsigned char color[16], alpha[16];
  if (!m->ib2) {
    float e = 0;
    for (int i = 0; i < 16; i++) {
      int s = subset(m, partition, i);
      if (only >= 0 && s != only) {
        color[i] = x->held_idx[i];
        e += x->held_e[i];
      } else {
        color[i] = pick_p(&x->probes[b->rotation][i], &ramps[s], 4);
        e += term(x, &ramps[s], color[i], px[i], b->rotation, i);
      }
      if (e > limit) return e;
    }
    for (int s = 0; s < m->ns; s++) {
      if (color[anchor(m, partition, s)] < 1 << (cb - 1)) continue;
      flip(b, s, 0, 4, color, cb, m);
      if (m->epb) {
        int t = b->pbits[s * 2];
        b->pbits[s * 2] = b->pbits[s * 2 + 1];
        b->pbits[s * 2 + 1] = t;
      }
    }
    copy(b->idx, color, 16);
    return e;
  }
  const int *w = weights(ab);
  int av[8];
  for (int j = 0; j < 1 << ab; j++) av[j] = lerp(E[0][3], E[1][3], w[j]);
  for (int i = 0; i < 16; i++) {
    float a = lane(px[i], 3), dist = (float)finf();
    for (int j = 0; j < 1 << ab; j++) {
      float d = a - av[j];
      if (d * d < dist) dist = d * d, alpha[i] = j;
    }
  }
  float e = 0;
  for (int i = 0; i < 16; i++) {
    int j = color[i] = pick_p(&x->probes[b->rotation][i], &ramps[0], 3);
    e += miss(x, vmake(ramps[0].c[0][j], ramps[0].c[1][j], ramps[0].c[2][j], av[alpha[i]]), px[i], b->rotation, i);
    if (e > limit) return e;
  }
  if (color[0] >= 1 << (cb - 1)) flip(b, 0, 0, 3, color, cb, m);
  if (alpha[0] >= 1 << (ab - 1)) flip(b, 0, 3, 4, alpha, ab, m);
  copy(b->idx, isel ? alpha : color, 16);
  copy(b->idx2, isel ? color : alpha, 16);
  return e;
}

static float build(Enc7 *x, Block *b, int mode, int partition, int rotation, int isel, const vec *px, vec ends[3][2], float limit) {
  const Mode *m = &MODES[mode];
  b->mode = mode, b->partition = partition, b->rotation = rotation, b->isel = isel;
  for (int s = 0; s < m->ns; s++) quantize_pair(b, s, ends[s][1], ends[s][0], m);
  return assign(x, b, px, limit, -1);
}

static int solve(Enc7 *x, const Mode *m, int partition, int s, int rotation, const vec *px, const unsigned char *idx, int bits, vec *a, vec *z) {
  vec w[16];
  unsigned char in[16];
  for (int i = 0; i < 16; i++) w[i] = weights_of(x, rotation, i), in[i] = subset(m, partition, i) == s;
  return lsq(px, w, in, idx, weights(bits), a, z);
}

static int fit(Enc7 *x, const Block *b, const vec *px, vec ends[3][2]) {
  const Mode *m = &MODES[b->mode];
  for (int s = 0; s < m->ns; s++) {
    if (!m->ib2) {
      if (!solve(x, m, b->partition, s, b->rotation, px, b->idx, m->ib, &ends[s][1], &ends[s][0])) return 0;
      if (!m->ab) {
        ends[s][0] = with(ends[s][0], 3, 255);
        ends[s][1] = with(ends[s][1], 3, 255);
      }
      continue;
    }
    const unsigned char *ci = b->isel ? b->idx2 : b->idx, *ai = b->isel ? b->idx : b->idx2;
    vec ca, cz, aa, az;
    if (!solve(x, m, 0, 0, b->rotation, px, ci, b->isel ? m->ib2 : m->ib, &ca, &cz)) return 0;
    if (!solve(x, m, 0, 0, b->rotation, px, ai, b->isel ? m->ib : m->ib2, &aa, &az)) return 0;
    ends[0][1] = with(ca, 3, lane(aa, 3));
    ends[0][0] = with(cz, 3, lane(az, 3));
  }
  return 1;
}

static void consider(Enc7 *x, Block *best, float *least, int mode, int partition, int rotation, int isel, const vec *px, vec ends[3][2], const vec *pixels) {
  Block b;
  float e = build(x, &b, mode, partition, rotation, isel, px, ends, x->found == 8 ? x->errors[7] : (float)finf());
  int at = rank(x->errors, &x->found, 8, e, x->seen, sizeof(Block));
  if (at >= 0) x->seen[at] = b;
  if (e < *least) *least = e, *best = b;
}

static int refit(Enc7 *x, Block *b, float *e, const vec *pixels) {
  vec px[16], again[3][2];
  for (int i = 0; i < 16; i++) px[i] = rotate(pixels[i], b->rotation);
  int better = 0;
  for (int r = 0; r < 2; r++) {
    Block c;
    if (!fit(x, b, px, again)) break;
    float f = build(x, &c, b->mode, b->partition, b->rotation, b->isel, px, again, *e);
    if (f >= *e) break;
    *b = c, *e = f, better = 1;
  }
  return better;
}

static void start(const Block *b, const vec *px, int kind, vec ends[3][2]) {
  const Mode *m = &MODES[b->mode];
  for (int s = 0; s < m->ns; s++) {
    vec pts[16], a, z;
    int count = 0;
    for (int i = 0; i < 16; i++) if (subset(m, b->partition, i) == s) pts[count++] = px[i];
    line(pts, count, m, &ends[s][0], &ends[s][1]);
    if (kind == 0) {
      vec d = mul(sub(ends[s][0], ends[s][1]), splat(1.0f / 16));
      ends[s][0] = sub(ends[s][0], d);
      ends[s][1] = add(ends[s][1], d);
      continue;
    }
    corner(pts, count, kind, &a, &z);
    ends[s][0] = with(z, 3, lane(ends[s][0], 3));
    ends[s][1] = with(a, 3, lane(ends[s][1], 3));
  }
}

static void polish(Enc7 *x, Block *best, float *least, const vec *pixels) {
  for (int k = 0; k < 8 && k < x->found; k++) {
    refit(x, &x->seen[k], &x->errors[k], pixels);
    vec px[16], ends[3][2];
    for (int i = 0; i < 16; i++) px[i] = rotate(pixels[i], x->seen[k].rotation);
    for (int kind = 0; kind < 5; kind++) {
      Block c;
      start(&x->seen[k], px, kind, ends);
      float f = build(x, &c, x->seen[k].mode, x->seen[k].partition, x->seen[k].rotation, x->seen[k].isel, px, ends, (float)finf());
      refit(x, &c, &f, pixels);
      if (f < x->errors[k]) x->seen[k] = c, x->errors[k] = f;
    }
    if (x->errors[k] < *least) *least = x->errors[k], *best = x->seen[k];
  }
}

static int take(Enc7 *x, Block *best, float *least, Block *t, const vec *px, int only) {
  float f = assign(x, t, px, *least, only);
  if (f >= *least) return 0;
  *least = f, *best = *t;
  if (only >= 0) hold(x, best, px);
  return 1;
}

static int step(Enc7 *x, Block *best, float *least, const vec *px, const vec *pixels) {
  const Mode *m = &MODES[best->mode];
  int better = 0, many = m->ns > 1;
  if (many) hold(x, best, px);
  for (int e = 0; e < m->ns * 2; e++) {
    for (int c = 0; c < 4; c++) {
      int max = (1 << bits_of(m, c)) - 1;
      for (int d = -1; d <= 1 && max > 0; d += 2) {
        Block t = *best;
        int v = t.ep[e][c] + d;
        if (v < 0 || v > max) continue;
        t.ep[e][c] = v;
        better |= take(x, best, least, &t, px, many ? e >> 1 : -1);
      }
    }
  }
  static const int PAIRS[4][2] = { { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
  for (int s = 0; s < m->ns; s++) {
    for (int c = 0; c < 4; c++) {
      int max = (1 << bits_of(m, c)) - 1;
      for (int k = 0; k < 4 && max > 0; k++) {
        Block t = *best;
        int u = t.ep[s * 2][c] + PAIRS[k][0], v = t.ep[s * 2 + 1][c] + PAIRS[k][1];
        if (u < 0 || u > max || v < 0 || v > max) continue;
        t.ep[s * 2][c] = u;
        t.ep[s * 2 + 1][c] = v;
        better |= take(x, best, least, &t, px, many ? s : -1);
      }
    }
  }
  int np = m->epb ? m->ns * 2 : m->spb ? m->ns : 0;
  for (int k = 0; k < np; k++) {
    Block t = *best;
    t.pbits[k] ^= 1;
    better |= take(x, best, least, &t, px, many ? (m->epb ? k >> 1 : k) : -1);
  }
  return better;
}

static void nudge(Enc7 *x, Block *best, float *least, const vec *pixels) {
  vec px[16];
  for (int i = 0; i < 16; i++) px[i] = rotate(pixels[i], best->rotation);
  for (int k = 0; k < 12 && step(x, best, least, px, pixels); k++) {}
}

static void deep(Enc7 *x, Block *best, float *least, const vec *pixels) {
  for (int k = 0; k < 8 && k < x->found; k++) {
    int at = k;
    for (int i = k + 1; i < x->found; i++) if (x->errors[i] < x->errors[at]) at = i;
    Block b = x->seen[at];
    float e = x->errors[at];
    x->seen[at] = x->seen[k], x->errors[at] = x->errors[k];
    nudge(x, &b, &e, pixels);
    if (e < *least) *least = e, *best = b;
  }
}

static void bc7_encode(const float *in, unsigned char *out, const tex_options *o) {
  Enc7 ctx, *x = &ctx;
  vec pixels[16], px[16], ends[3][2];
  for (int i = 0; i < 16; i++) pixels[i] = mul(load(in + i * 4), splat(255));
  for (int i = 0; i < 16; i++) x->weight[i] = tint(o);
  for (int r = 0; r < (is_fast(o) ? 1 : 4); r++) for (int i = 0; i < 16; i++) probe(&x->probes[r][i], rotate(pixels[i], r), weights_of(x, r, i));
  Block best;
  float least = (float)finf();
  float s2[64];
  x->found = 0;
  spreads(pixels, 2, 64, s2);
  int q2 = 0;
  for (int p = 1; p < 64; p++) if (s2[p] < s2[q2]) q2 = p;
  lines(&MODES[1], q2, pixels, ends);
  consider(x, &best, &least, 1, q2, 0, 0, pixels, ends, pixels);
  lines(&MODES[6], 0, pixels, ends);
  consider(x, &best, &least, 6, 0, 0, 0, pixels, ends, pixels);
  lines(&MODES[4], 0, pixels, ends);
  consider(x, &best, &least, 5, 0, 0, 0, pixels, ends, pixels);
  if (is_fast(o)) {
    pack(&best, out);
    return;
  }
  for (int p = 0; p < 16; p++) {
    lines(&MODES[0], p, pixels, x->three[p]);
    consider(x, &best, &least, 0, p, 0, 0, pixels, x->three[p], pixels);
  }
  for (int p = 0; p < 64; p++) {
    lines(&MODES[1], p, pixels, x->two[p]);
    consider(x, &best, &least, 1, p, 0, 0, pixels, x->two[p], pixels);
  }
  for (int p = 0; p < 64; p++) {
    if (p >= 16) lines(&MODES[2], p, pixels, ends);
    consider(x, &best, &least, 2, p, 0, 0, pixels, p < 16 ? x->three[p] : ends, pixels);
  }
  for (int p = 0; p < 64; p++) consider(x, &best, &least, 3, p, 0, 0, pixels, x->two[p], pixels);
  for (int p = 0; p < 64; p++) {
    lines(&MODES[7], p, pixels, ends);
    consider(x, &best, &least, 7, p, 0, 0, pixels, ends, pixels);
  }
  for (int r = 0; r < 4; r++) {
    for (int i = 0; i < 16; i++) px[i] = rotate(pixels[i], r);
    lines(&MODES[4], 0, px, ends);
    consider(x, &best, &least, 4, 0, r, 0, px, ends, pixels);
    consider(x, &best, &least, 4, 0, r, 1, px, ends, pixels);
    if (r) consider(x, &best, &least, 5, 0, r, 0, px, ends, pixels);
  }
  polish(x, &best, &least, pixels);
  deep(x, &best, &least, pixels);
  pack(&best, out);
}

static const Codec BC7 = { bc7_encode, bc7_decode, 16 };
