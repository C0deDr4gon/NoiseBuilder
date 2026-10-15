#pragma once

#include "platform.h"
#include "vec.h"
#include "bits.h"
#include "fit.h"
#include "codec.h"

typedef struct {
  vec weight[16];
  unsigned char opaque[16];
} Enc1;

typedef struct {
  int c0, c1, four;
  unsigned char idx[16];
  float error;
} Try;

static int q(float v, int max) { return (int)fround(clamp(v, 0, 1) * max); }

static int to565(vec c) { return q(lane(c, 0), 31) << 11 | q(lane(c, 1), 63) << 5 | q(lane(c, 2), 31); }

static vec from565(int c) {
  return vmake((c >> 11 & 31) / 31.0f, (c >> 5 & 63) / 63.0f, (c & 31) / 31.0f, 1);
}

static void palette(int c0, int c1, int four, vec *pal) {
  pal[0] = from565(c0);
  pal[1] = from565(c1);
  vec d = sub(pal[1], pal[0]);
  pal[2] = add(pal[0], mul(d, splat(four ? 1.0f / 3 : 1.0f / 2)));
  pal[3] = four ? add(pal[0], mul(d, splat(2.0f / 3))) : splat(0);
}

static Try make(Enc1 *x, const vec *pixels, int c0, int c1, int three, int punch) {
  Try t;
  t.c0 = c0;
  t.c1 = c1;
  if (three ? t.c0 > t.c1 : t.c0 < t.c1) {
    int s = t.c0;
    t.c0 = t.c1;
    t.c1 = s;
  }
  t.four = !punch || t.c0 > t.c1;
  vec pal[4];
  palette(t.c0, t.c1, t.four, pal);
  Pal set;
  pal_init(&set, t.four ? 4 : 3);
  for (int i = 0; i < set.count; i++) pal_set(&set, i, pal[i]);
  t.error = 0;
  for (int i = 0; i < 16; i++) {
    if (!x->opaque[i]) {
      t.idx[i] = 3;
      continue;
    }
    t.idx[i] = pick_w(pixels[i], x->weight[i], &set, 3);
    vec d = keep(sub(pixels[i], pal[t.idx[i]]), mask(3));
    t.error += hsum(mul(mul(d, d), x->weight[i]));
  }
  return t;
}

static Try color_fit(Enc1 *x, const vec *pixels, vec a, vec b, int three, int punch) {
  return make(x, pixels, to565(a), to565(b), three, punch);
}

static int refine(Enc1 *x, const vec *pixels, const Try *t, vec *a, vec *b) {
  float ts[4] = { 0, 1, t->four ? 1.0f / 3 : 1.0f / 2, 2.0f / 3 };
  return lsqf(pixels, x->weight, x->opaque, t->idx, ts, a, b);
}

static Try color_line(Enc1 *x, const vec *pixels, vec a, vec b, int three, int punch, int rounds) {
  Try best = color_fit(x, pixels, a, b, three, punch), last = best;
  for (int i = 0; i < rounds; i++) {
    if (!refine(x, pixels, &last, &a, &b)) break;
    last = color_fit(x, pixels, a, b, three, punch);
    if (last.error < best.error) best = last;
  }
  return best;
}

static int moved(int c, int k, int d) {
  static const int SHIFT[3] = { 11, 5, 0 }, MAX[3] = { 31, 63, 31 };
  int v = (c >> SHIFT[k] & MAX[k]) + d;
  if (v < 0 || v > MAX[k]) return -1;
  return (c & ~(MAX[k] << SHIFT[k])) | v << SHIFT[k];
}

static void color_nudge(Enc1 *x, const vec *pixels, Try *best, int three, int punch) {
  static const int PAIRS[4][2] = { { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
  for (int round = 0; round < 16; round++) {
    int better = 0;
    for (int e = 0; e < 2; e++) for (int k = 0; k < 3; k++) for (int d = -1; d <= 1; d += 2) {
      int c = moved(e ? best->c1 : best->c0, k, d);
      if (c < 0) continue;
      Try t = make(x, pixels, e ? best->c0 : c, e ? c : best->c1, three, punch);
      if (t.error < best->error) *best = t, better = 1;
    }
    for (int k = 0; k < 3; k++) for (int p = 0; p < 4; p++) {
      int u = moved(best->c0, k, PAIRS[p][0]), v = moved(best->c1, k, PAIRS[p][1]);
      if (u < 0 || v < 0) continue;
      Try t = make(x, pixels, u, v, three, punch);
      if (t.error < best->error) *best = t, better = 1;
    }
    if (!better) break;
  }
}

static void color_pack(const Try *t, unsigned char *out) {
  int pos = put(out, 0, t->c0, 16);
  pos = put(out, pos, t->c1, 16);
  for (int i = 0; i < 16; i++) pos = put(out, pos, t->idx[i], 2);
}

static void encode_color(const float *px, unsigned char *out, int punch, const tex_options *o) {
  Enc1 ctx, *x = &ctx;
  vec pixels[16], pts[16], a = splat(0), b = splat(0);
  int count = 0, transparent = 0;
  for (int i = 0; i < 16; i++) {
    pixels[i] = load(px + i * 4);
    transparent |= punch && px[i * 4 + 3] < 0.5f;
  }
  for (int i = 0; i < 16; i++) {
    x->opaque[i] = !transparent || px[i * 4 + 3] >= 0.5f;
    x->weight[i] = tint(o);
    if (x->opaque[i]) pts[count++] = pixels[i];
  }
  if (count) endpoints(pts, count, 3, &a, &b);
  Try best = color_line(x, pixels, a, b, transparent, punch, count ? 2 : 0);
  if (is_fast(o) || !count) {
    color_pack(&best, out);
    return;
  }
  Try seen[4];
  float errors[4];
  int found = 0;
  for (int three = transparent; three <= (punch ? 1 : transparent); three++) {
    for (int kind = 0; kind < 6; kind++) {
      vec s = a, z = b;
      if (kind == 1) {
        vec d = mul(sub(b, a), splat(1.0f / 16));
        s = add(a, d), z = sub(b, d);
      }
      if (kind > 1) corner(pts, count, kind - 2, &s, &z);
      Try t = color_line(x, pixels, s, z, three, punch, 2);
      int at = rank(errors, &found, 4, t.error, seen, sizeof(Try));
      if (at >= 0) seen[at] = t;
    }
  }
  color_nudge(x, pixels, &best, transparent, punch);
  for (int k = 0; k < found; k++) {
    int three = punch && !seen[k].four;
    color_nudge(x, pixels, &seen[k], three, punch);
    if (seen[k].error < best.error) best = seen[k];
  }
  color_pack(&best, out);
}

static void decode_color(const unsigned char *in, float *px, int punch) {
  int c0 = get(in, 0, 16);
  int c1 = get(in, 16, 16);
  vec pal[4];
  palette(c0, c1, !punch || c0 > c1, pal);
  for (int i = 0; i < 16; i++) store(px + i * 4, pal[get(in, 32 + i * 2, 2)]);
}

static void channel_ramp(int a0, int a1, int sign, double *r) {
  double scale = sign ? 127 : 255;
  double a = a0 / scale;
  double b = a1 / scale;
  r[0] = a;
  r[1] = b;
  if (a0 > a1) {
    for (int i = 1; i < 7; i++) r[1 + i] = (a * (7 - i) + b * i) / 7;
    return;
  }
  for (int i = 1; i < 5; i++) r[1 + i] = (a * (5 - i) + b * i) / 5;
  r[6] = sign ? -1 : 0;
  r[7] = 1;
}

static int channel_pick(const float *vs, const double *r, unsigned char *out, int pos) {
  for (int i = 0; i < 16; i += 4) {
    vec v = load(vs + i);
    vec best = splat((float)finf());
    ivec index = isplat(0);
    for (int j = 0; j < 8; j++) {
      vec d = vabs(sub(v, splat(r[j])));
      ivec closer = lt(d, best);
      best = vselect(closer, d, best);
      index = iselect(closer, isplat(j), index);
    }
    pos = put(out, pos, ilane(index, 0), 3);
    pos = put(out, pos, ilane(index, 1), 3);
    pos = put(out, pos, ilane(index, 2), 3);
    pos = put(out, pos, ilane(index, 3), 3);
  }
  return pos;
}

static float channel_error(const float *vs, int a0, int a1, int sign, unsigned char *idx) {
  double r[8];
  channel_ramp(a0, a1, sign, r);
  float total = 0;
  for (int i = 0; i < 16; i++) {
    float best = (float)finf();
    for (int j = 0; j < 8; j++) {
      float d = vs[i] - r[j];
      if (d * d < best) best = d * d, idx[i] = j;
    }
    total += best;
  }
  return total;
}

typedef struct {
  int a0, a1;
  float e;
} End;

static int ok(int c0, int c1, int eight, int sign) {
  int min = sign ? -127 : 0, max = sign ? 127 : 255;
  return c0 >= min && c0 <= max && c1 >= min && c1 <= max && (c0 > c1) == eight;
}

static void channel_consider(const float *vs, End *best, int c0, int c1, int sign) {
  unsigned char idx[16];
  if (!ok(c0, c1, best->a0 > best->a1, sign)) return;
  float e = channel_error(vs, c0, c1, sign, idx);
  if (e < best->e) best->a0 = c0, best->a1 = c1, best->e = e;
}

static void channel_refit(const float *vs, End *best, int sign) {
  double scale = sign ? 127 : 255;
  int eight = best->a0 > best->a1;
  for (int k = 0; k < 2; k++) {
    unsigned char idx[16];
    channel_error(vs, best->a0, best->a1, sign, idx);
    double aa = 0, bb = 0, ab = 0, ax = 0, bx = 0;
    for (int i = 0; i < 16; i++) {
      if (!eight && idx[i] > 5) continue;
      double t = idx[i] < 2 ? idx[i] : (idx[i] - 1) / (eight ? 7.0 : 5.0);
      aa += (1 - t) * (1 - t), bb += t * t, ab += (1 - t) * t, ax += (1 - t) * vs[i], bx += t * vs[i];
    }
    double det = aa * bb - ab * ab;
    if (det < 1e-9) return;
    int c0 = (int)fround((ax * bb - bx * ab) / det * scale), c1 = (int)fround((bx * aa - ax * ab) / det * scale);
    float before = best->e;
    channel_consider(vs, best, c0, c1, sign);
    if (best->e >= before) return;
  }
}

static void search(const float *vs, End *best, int sign) {
  static const int PAIRS[4][2] = { { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
  channel_refit(vs, best, sign);
  int b0 = best->a0, b1 = best->a1;
  for (int d0 = -4; d0 <= 4; d0++) for (int d1 = -4; d1 <= 4; d1++) channel_consider(vs, best, b0 + d0, b1 + d1, sign);
  for (int n = 0; n < 16; n++) {
    float before = best->e;
    int c0 = best->a0, c1 = best->a1;
    for (int k = 0; k < 4; k++) channel_consider(vs, best, c0 + (k == 0) - (k == 1), c1 + (k == 2) - (k == 3), sign);
    for (int k = 0; k < 4; k++) channel_consider(vs, best, c0 + PAIRS[k][0], c1 + PAIRS[k][1], sign);
    if (best->e >= before) break;
  }
}

static void encode_channel(const double *values, unsigned char *out, int sign, const tex_options *o) {
  float vs[16];
  double r[8], hi = -finf(), lo = finf();
  double scale = sign ? 127 : 255;
  for (int i = 0; i < 16; i++) {
    vs[i] = clamp(sign ? values[i] * 2 - 1 : values[i], sign ? -1 : 0, 1);
    if (vs[i] > hi) hi = vs[i];
    if (vs[i] < lo) lo = vs[i];
  }
  int a0 = (int)fround(hi * scale);
  int a1 = (int)fround(lo * scale);
  if (!is_fast(o) && a0 != a1) {
    unsigned char idx[16];
    End best = { a0, a1, channel_error(vs, a0, a1, sign, idx) };
    for (int flip = 0; flip < 2; flip++) {
      End e = { flip ? a1 : a0, flip ? a0 : a1, 0 };
      e.e = channel_error(vs, e.a0, e.a1, sign, idx);
      search(vs, &e, sign);
      if (e.e < best.e) best = e;
    }
    a0 = best.a0, a1 = best.a1;
  }
  channel_ramp(a0, a1, sign, r);
  int pos = put(out, 0, a0 & 255, 8);
  pos = put(out, pos, a1 & 255, 8);
  channel_pick(vs, r, out, pos);
}

static int byte(const unsigned char *in, int pos, int sign) {
  int v = get(in, pos, 8);
  if (!sign) return v;
  v = (signed char)v;
  return v < -127 ? -127 : v;
}

static void decode_channel(const unsigned char *in, double *values, int sign) {
  double r[8];
  channel_ramp(byte(in, 0, sign), byte(in, 8, sign), sign, r);
  for (int i = 0; i < 16; i++) {
    double v = r[get(in, 16 + i * 3, 3)];
    values[i] = sign ? (v + 1) / 2 : v;
  }
}

static void bc1_encode(const float *px, unsigned char *out, const tex_options *o) { encode_color(px, out, 1, o); }
static void bc1_decode(const unsigned char *in, float *px) { decode_color(in, px, 1); }

static const Codec BC1 = { bc1_encode, bc1_decode, 8 };

static void bc2_encode(const float *px, unsigned char *out, const tex_options *o) {
  for (int i = 0; i < 16; i++) out[i >> 1] |= (int)fround(clamp(px[i * 4 + 3], 0, 1) * 15) << (i & 1) * 4;
  encode_color(px, out + 8, 0, o);
}

static void bc2_decode(const unsigned char *in, float *px) {
  decode_color(in + 8, px, 0);
  for (int i = 0; i < 16; i++) px[i * 4 + 3] = (in[i >> 1] >> (i & 1) * 4 & 15) / 15.0;
}

static const Codec BC2 = { bc2_encode, bc2_decode, 16 };

static void bc3_encode(const float *px, unsigned char *out, const tex_options *o) {
  double alpha[16];
  for (int i = 0; i < 16; i++) alpha[i] = px[i * 4 + 3];
  encode_channel(alpha, out, 0, o);
  encode_color(px, out + 8, 0, o);
}

static void bc3_decode(const unsigned char *in, float *px) {
  double alpha[16];
  decode_color(in + 8, px, 0);
  decode_channel(in, alpha, 0);
  for (int i = 0; i < 16; i++) px[i * 4 + 3] = alpha[i];
}

static const Codec BC3 = { bc3_encode, bc3_decode, 16 };

static void bc4_encode(const float *px, unsigned char *out, int sign, const tex_options *o) {
  double r[16];
  for (int i = 0; i < 16; i++) r[i] = px[i * 4];
  encode_channel(r, out, sign, o);
}

static void bc4_decode(const unsigned char *in, float *px, int sign) {
  double r[16];
  decode_channel(in, r, sign);
  for (int i = 0; i < 16; i++) {
    px[i * 4] = r[i];
    px[i * 4 + 1] = px[i * 4 + 2] = 0;
    px[i * 4 + 3] = 1;
  }
}

static void bc4_encode_u(const float *px, unsigned char *out, const tex_options *o) { bc4_encode(px, out, 0, o); }
static void bc4_decode_u(const unsigned char *in, float *px) { bc4_decode(in, px, 0); }
static void bc4_encode_s(const float *px, unsigned char *out, const tex_options *o) { bc4_encode(px, out, 1, o); }
static void bc4_decode_s(const unsigned char *in, float *px) { bc4_decode(in, px, 1); }

static const Codec BC4 = { bc4_encode_u, bc4_decode_u, 8 };
static const Codec BC4_SNORM = { bc4_encode_s, bc4_decode_s, 8 };

static void bc5_encode(const float *px, unsigned char *out, int sign, const tex_options *o) {
  double r[16], g[16];
  for (int i = 0; i < 16; i++) {
    r[i] = px[i * 4];
    g[i] = px[i * 4 + 1];
  }
  encode_channel(r, out, sign, o);
  encode_channel(g, out + 8, sign, o);
}

static void bc5_decode(const unsigned char *in, float *px, int sign) {
  double r[16], g[16];
  decode_channel(in, r, sign);
  decode_channel(in + 8, g, sign);
  for (int i = 0; i < 16; i++) {
    px[i * 4] = r[i];
    px[i * 4 + 1] = g[i];
    px[i * 4 + 2] = 0;
    px[i * 4 + 3] = 1;
  }
}

static void bc5_encode_u(const float *px, unsigned char *out, const tex_options *o) { bc5_encode(px, out, 0, o); }
static void bc5_decode_u(const unsigned char *in, float *px) { bc5_decode(in, px, 0); }
static void bc5_encode_s(const float *px, unsigned char *out, const tex_options *o) { bc5_encode(px, out, 1, o); }
static void bc5_decode_s(const unsigned char *in, float *px) { bc5_decode(in, px, 1); }

static const Codec BC5 = { bc5_encode_u, bc5_decode_u, 16 };
static const Codec BC5_SNORM = { bc5_encode_s, bc5_decode_s, 16 };
