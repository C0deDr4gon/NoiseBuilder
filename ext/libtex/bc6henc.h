#pragma once

#include "platform.h"
#include "bits.h"
#include "codec.h"
#include "fit.h"
#include "half.h"
#include "bc6h.h"
#include "partition.h"
#include "bc7tables.h"

typedef struct {
  vec weight[16];
  Probe probes[16];
  Pal held[2];
  unsigned char held_idx[16];
  float held_d[16];
  Block6 seen[8];
  float errors[8];
  int found;
} Enc6;

static int value(int q, int bits, int sign) { return finish6(unquant6(q, bits, sign), sign); }

static int low(int bits, int sign) { return sign ? 1 - (1 << (bits - 1)) : 0; }
static int high(int bits, int sign) { return sign ? (1 << (bits - 1)) - 1 : (1 << bits) - 1; }

static int quantize6(float h, int bits, int sign) {
  int min = low(bits, sign), max = high(bits, sign);
  float scale = sign ? 32.0f / 31 * (1 << (bits - 1)) / 32768 : 64.0f / 31 * (1 << bits) / 65536;
  int q = (int)clamp(h * scale, min, max);
  while (q < max && value(q, bits, sign) < h) q++;
  while (q > min && value(q - 1, bits, sign) >= h) q--;
  if (q > min) {
    float a = h - value(q - 1, bits, sign);
    float b = h - value(q, bits, sign);
    if (a * a <= b * b) return q - 1;
  }
  return q;
}

static int valid(const Mode6 *m, int q[4][3], int sign) {
  for (int e = 0; e < m->ns * 2; e++) for (int k = 0; k < 3; k++) {
    if (q[e][k] < low(m->base, sign) || q[e][k] > high(m->base, sign)) return 0;
    if (!m->transformed || !e) continue;
    int d = q[e][k] - q[0][k], lim = 1 << (m->delta[k] - 1);
    if (d < -lim || d >= lim) return 0;
  }
  return 1;
}

static void members(const Block6 *b, int s, unsigned char *in) {
  for (int i = 0; i < 16; i++) in[i] = subset6(b->mode, b->partition, i) == s;
}

static void ramp6(const Mode6 *m, const int (*q)[3], int s, int sign, Pal *pal) {
  int n = m->ns == 2 ? 8 : 16;
  const int *w = weights(m->ns == 2 ? 3 : 4);
  pal->count = n;
  for (int k = 0; k < 3; k++) {
    ivec a = isplat(unquant6(q[s * 2][k], m->base, sign));
    ivec z = isplat(unquant6(q[s * 2 + 1][k], m->base, sign));
    for (int i = 0; i < n; i += 4) {
      ivec c = lerp4(a, z, iload(w + i));
      if (sign) {
        ivec v = ishr(imul(iabs(c), isplat(31)), 5);
        c = iselect(ilt(c, isplat(0)), ineg(v), v);
      } else {
        c = ishr(imul(c, isplat(31)), 6);
      }
      store(pal->c[k] + i, tofloat(c));
    }
  }
}

static float miss6(Enc6 *x, const Pal *pal, int j, vec p, int i) {
  vec d = sub(vmake(pal->c[0][j], pal->c[1][j], pal->c[2][j], 0), p);
  return hsum(mul(mul(d, d), x->weight[i]));
}

static void hold6(Enc6 *x, const Block6 *b, const vec *px, int sign) {
  for (int s = 0; s < b->mode->ns; s++) ramp6(b->mode, b->q, s, sign, &x->held[s]);
  for (int i = 0; i < 16; i++) {
    const Pal *pal = &x->held[subset6(b->mode, b->partition, i)];
    x->held_idx[i] = pick_p(&x->probes[i], pal, 3);
    x->held_d[i] = miss6(x, pal, x->held_idx[i], px[i], i);
  }
}

static float assign6(Enc6 *x, Block6 *b, const vec *px, int sign, float limit, int fixed) {
  const Mode6 *m = b->mode;
  int bits = m->ns == 2 ? 3 : 4, top = (1 << (bits - 1)) - 1;
  Pal own[2];
  const Pal *pals[2];
  for (int s = 0; s < m->ns; s++) {
    pals[s] = s == fixed ? &x->held[s] : &own[s];
    if (s != fixed) ramp6(m, b->q, s, sign, &own[s]);
  }
  float e = 0;
  for (int i = 0; i < 16; i++) {
    int s = subset6(m, b->partition, i);
    if (s == fixed) {
      b->idx[i] = x->held_idx[i];
      e += x->held_d[i];
    } else {
      b->idx[i] = pick_p(&x->probes[i], pals[s], 3);
      e += miss6(x, pals[s], b->idx[i], px[i], i);
    }
    if (e > limit) return e;
  }
  int flipped[2] = { 0, 0 }, clamped = 0;
  for (int s = 0; s < m->ns; s++) {
    int a = s ? A2[b->partition] : 0;
    if (b->idx[a] <= top) continue;
    int t[3];
    copy(t, b->q[s * 2], sizeof t);
    copy(b->q[s * 2], b->q[s * 2 + 1], sizeof t);
    copy(b->q[s * 2 + 1], t, sizeof t);
    if (!valid(m, b->q, sign)) {
      copy(b->q[s * 2 + 1], b->q[s * 2], sizeof t);
      copy(b->q[s * 2], t, sizeof t);
      b->idx[a] = top;
      clamped = 1;
      continue;
    }
    unsigned char in[16];
    members(b, s, in);
    for (int i = 0; i < 16; i++) if (in[i]) b->idx[i] = (1 << bits) - 1 - b->idx[i];
    flipped[s] = 1;
  }
  for (int i = 0; i < 12; i++) b->vals[i] = 0;
  for (int v = 0; v < m->ns * 2; v++) for (int k = 0; k < 3; k++) b->vals[k * 4 + v] = m->transformed && v ? b->q[v][k] - b->q[0][k] : b->q[v][k];
  if (!clamped) return e;
  e = 0;
  for (int i = 0; i < 16; i++) {
    int s = subset6(m, b->partition, i), j = flipped[s] ? (1 << bits) - 1 - b->idx[i] : b->idx[i];
    e += miss6(x, pals[s], j, px[i], i);
  }
  return e;
}

static float build6(Enc6 *x, Block6 *b, const Mode6 *m, int partition, const vec *px, int sign, vec ends[2][2], float limit) {
  b->mode = m;
  b->partition = partition;
  for (int s = 0; s < m->ns; s++) {
    for (int j = 0; j < 2; j++) {
      float v[3] = { lane(ends[s][j], 0), lane(ends[s][j], 1), lane(ends[s][j], 2) };
      for (int k = 0; k < 3; k++) b->q[s * 2 + j][k] = quantize6(v[k], m->base, sign);
    }
  }
  if (m->transformed) {
    for (int e = 1; e < m->ns * 2; e++) for (int k = 0; k < 3; k++) {
      int lim = 1 << (m->delta[k] - 1);
      b->q[e][k] = b->q[0][k] + (int)clamp(b->q[e][k] - b->q[0][k], -lim, lim - 1);
    }
  }
  return assign6(x, b, px, sign, limit, -1);
}

static void lines6(const Mode6 *m, int partition, const vec *px, vec ends[2][2]) {
  for (int s = 0; s < m->ns; s++) {
    vec pts[16], hi, lo;
    int count = 0;
    for (int i = 0; i < 16; i++) if (subset6(m, partition, i) == s) pts[count++] = px[i];
    endpoints(pts, count, 3, &hi, &lo);
    vec a = px[s ? A2[partition] : 0];
    vec dl = sub(lo, a), dh = sub(hi, a);
    int swap = dot(dl, dl) > dot(dh, dh);
    ends[s][0] = swap ? hi : lo;
    ends[s][1] = swap ? lo : hi;
  }
}

static void consider6(Enc6 *x, Block6 *best, float *least, const Mode6 *m, int partition, const vec *px, int sign, vec ends[2][2]) {
  Block6 b;
  float e = build6(x, &b, m, partition, px, sign, ends, x->found == 8 ? x->errors[7] : (float)finf());
  int at = rank(x->errors, &x->found, 8, e, x->seen, sizeof(Block6));
  if (at >= 0) x->seen[at] = b;
  if (e < *least) *least = e, *best = b;
}

static void refit6(Enc6 *x, Block6 *b, float *e, const vec *px, int sign) {
  const int *w = weights(b->mode->ns == 2 ? 3 : 4);
  for (int r = 0; r < 2; r++) {
    vec ends[2][2];
    unsigned char in[16];
    int ok = 1;
    for (int s = 0; s < b->mode->ns && ok; s++) {
      members(b, s, in);
      ok = lsq(px, x->weight, in, b->idx, w, &ends[s][0], &ends[s][1]);
    }
    if (!ok) return;
    Block6 c;
    float f = build6(x, &c, b->mode, b->partition, px, sign, ends, *e);
    if (f >= *e) return;
    *b = c, *e = f;
  }
}

static void start6(const Block6 *b, const vec *px, int kind, vec ends[2][2]) {
  const Mode6 *m = b->mode;
  lines6(m, b->partition, px, ends);
  for (int s = 0; s < m->ns; s++) {
    if (kind == 0) {
      vec d = mul(sub(ends[s][1], ends[s][0]), splat(1.0f / 16));
      ends[s][0] = add(ends[s][0], d);
      ends[s][1] = sub(ends[s][1], d);
      continue;
    }
    vec pts[16];
    int count = 0;
    for (int i = 0; i < 16; i++) if (subset6(m, b->partition, i) == s) pts[count++] = px[i];
    corner(pts, count, kind, &ends[s][0], &ends[s][1]);
  }
}

static void polish6(Enc6 *x, Block6 *best, float *least, const vec *px, int sign) {
  for (int k = 0; k < x->found; k++) {
    refit6(x, &x->seen[k], &x->errors[k], px, sign);
    for (int kind = 0; kind < 5; kind++) {
      vec ends[2][2];
      Block6 c;
      start6(&x->seen[k], px, kind, ends);
      float f = build6(x, &c, x->seen[k].mode, x->seen[k].partition, px, sign, ends, (float)finf());
      refit6(x, &c, &f, px, sign);
      if (f < x->errors[k]) x->seen[k] = c, x->errors[k] = f;
    }
    if (x->errors[k] < *least) *least = x->errors[k], *best = x->seen[k];
  }
}

static int try(Enc6 *x, Block6 *best, float *least, Block6 *t, const vec *px, int sign, int s) {
  if (!valid(t->mode, t->q, sign)) return 0;
  float f = assign6(x, t, px, sign, *least, t->mode->ns == 2 ? 1 - s : -1);
  if (f >= *least) return 0;
  *least = f, *best = *t;
  hold6(x, best, px, sign);
  return 1;
}

static int step6(Enc6 *x, Block6 *best, float *least, const vec *px, int sign) {
  static const int PAIRS[4][2] = { { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
  int ns = best->mode->ns, better = 0;
  hold6(x, best, px, sign);
  for (int e = 0; e < ns * 2; e++) for (int k = 0; k < 3; k++) for (int d = -1; d <= 1; d += 2) {
    Block6 t = *best;
    t.q[e][k] += d;
    better |= try(x, best, least, &t, px, sign, e >> 1);
  }
  for (int s = 0; s < ns; s++) for (int k = 0; k < 3; k++) for (int p = 0; p < 4; p++) {
    Block6 t = *best;
    t.q[s * 2][k] += PAIRS[p][0];
    t.q[s * 2 + 1][k] += PAIRS[p][1];
    better |= try(x, best, least, &t, px, sign, s);
  }
  return better;
}

static void deep6(Enc6 *x, Block6 *best, float *least, const vec *px, int sign) {
  for (int k = 0; k < x->found; k++) {
    Block6 b = x->seen[k];
    float e = x->errors[k];
    for (int r = 0; r < 12 && step6(x, &b, &e, px, sign); r++) {}
    if (e < *least) *least = e, *best = b;
  }
}

static float slope(float h) {
  int a = (int)(h < 0 ? -h : h);
  if (a > 0x7bfe) a = 0x7bfe;
  vec l = log2_1p(vmake(from_half(a), from_half(a + 1), 0, 0));
  return lane(l, 1) - lane(l, 0);
}

static float peak3(vec v) {
  float t = lane(v, 0);
  if (lane(v, 1) > t) t = lane(v, 1);
  if (lane(v, 2) > t) t = lane(v, 2);
  return t;
}

static void weigh(Enc6 *x, const vec *px, const tex_options *o) {
  vec top = splat(0);
  for (int i = 0; i < 16; i++) {
    vec g = vmake(slope(lane(px[i], 0)), slope(lane(px[i], 1)), slope(lane(px[i], 2)), 0);
    x->weight[i] = mul(mul(g, g), tint(o));
    top = vmax(top, x->weight[i]);
  }
  float t = peak3(top);
  if (t == 0) {
    for (int i = 0; i < 16; i++) x->weight[i] = tint(o);
    t = peak3(tint(o));
  }
  for (int i = 0; i < 16; i++) x->weight[i] = mul(x->weight[i], splat(1 / t));
  for (int i = 0; i < 16; i++) probe(&x->probes[i], px[i], x->weight[i]);
}

static void encode6(const float *in, unsigned char *out, int sign, const tex_options *o) {
  Enc6 ctx, *x = &ctx;
  vec px[16], ends[2][2];
  for (int i = 0; i < 16; i++) px[i] = vmake(to_half(in[i * 4], sign), to_half(in[i * 4 + 1], sign), to_half(in[i * 4 + 2], sign), 0);
  weigh(x, px, o);
  Block6 best;
  float least = (float)finf();
  x->found = 0;
  if (is_fast(o)) {
    float scores[32];
    spreads(px, 2, 32, scores);
    int p = 0;
    for (int t = 1; t < 32; t++) if (scores[t] < scores[p]) p = t;
    static const int FAST[7] = { 0, 5, 9, 10, 11, 12, 13 };
    for (int k = 0; k < 7; k++) {
      const Mode6 *m = &MODES6[FAST[k]];
      lines6(m, m->ns == 2 ? p : 0, px, ends);
      consider6(x, &best, &least, m, m->ns == 2 ? p : 0, px, sign, ends);
    }
    pack6(&best, out);
    return;
  }
  lines6(&MODES6[10], 0, px, ends);
  for (int i = 10; i < 14; i++) consider6(x, &best, &least, &MODES6[i], 0, px, sign, ends);
  for (int p = 0; p < 32; p++) {
    lines6(&MODES6[0], p, px, ends);
    for (int i = 0; i < 10; i++) consider6(x, &best, &least, &MODES6[i], p, px, sign, ends);
  }
  polish6(x, &best, &least, px, sign);
  deep6(x, &best, &least, px, sign);
  pack6(&best, out);
}

static void bc6h_encode_u(const float *px, unsigned char *out, const tex_options *o) { encode6(px, out, 0, o); }
static void bc6h_decode_u(const unsigned char *in, float *px) { bc6h_decode(in, px, 0); }
static void bc6h_encode_s(const float *px, unsigned char *out, const tex_options *o) { encode6(px, out, 1, o); }
static void bc6h_decode_s(const unsigned char *in, float *px) { bc6h_decode(in, px, 1); }

static const Codec BC6H_UF16 = { bc6h_encode_u, bc6h_decode_u, 16 };
static const Codec BC6H_SF16 = { bc6h_encode_s, bc6h_decode_s, 16 };
