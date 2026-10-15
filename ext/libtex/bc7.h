#pragma once

#include "platform.h"
#include "vec.h"
#include "bc7tables.h"
#include "bits.h"
#include "codec.h"

typedef struct {
  int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
} Mode;

typedef struct {
  int mode, partition, rotation, isel;
  int ep[6][4];
  int pbits[6];
  unsigned char idx[16], idx2[16];
} Block;

static inline int subset(const Mode *m, int p, int i) {
  return m->ns == 1 ? 0 : m->ns == 2 ? P2[p][i] : P3[p][i];
}

static inline int anchor(const Mode *m, int p, int s) {
  return !s ? 0 : m->ns == 2 ? A2[p] : s == 1 ? A3A[p] : A3B[p];
}

static const Mode MODES[8] = {
  { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
  { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
  { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
  { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
  { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
  { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
  { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
  { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

static int is_anchor(const Mode *m, int p, int i) {
  for (int s = 0; s < m->ns; s++) if (anchor(m, p, s) == i) return 1;
  return 0;
}

static int bits_of(const Mode *m, int c) { return c < 3 ? m->cb : m->ab; }

static int pbit_of(const Block *b, const Mode *m, int e) {
  return m->epb ? b->pbits[e] : m->spb ? b->pbits[e >> 1] : -1;
}

static int expand(int v, int bits) { return v << (8 - bits) | v >> (2 * bits - 8); }

static int unquant(int q, int bits, int p) { return p < 0 ? expand(q, bits) : expand(q << 1 | p, bits + 1); }

static void expand_endpoint(const int *e, const Mode *m, int p, int *out) {
  for (int c = 0; c < 4; c++) out[c] = c == 3 && !m->ab ? 255 : unquant(e[c], bits_of(m, c), p);
}

static vec rotate(vec c, int r) {
  if (r == 1) return vshuffle(c, 3, 1, 2, 0);
  if (r == 2) return vshuffle(c, 0, 3, 2, 1);
  if (r == 3) return vshuffle(c, 0, 1, 3, 2);
  return c;
}

static void colors(const Block *b, vec *out) {
  const Mode *m = &MODES[b->mode];
  int E[6][4];
  for (int e = 0; e < m->ns * 2; e++) expand_endpoint(b->ep[e], m, pbit_of(b, m, e), E[e]);
  const unsigned char *ci = b->idx, *ai = b->idx;
  int cb = m->ib, ab = m->ib;
  if (m->ib2 && b->isel) ci = b->idx2, cb = m->ib2;
  if (m->ib2 && !b->isel) ai = b->idx2, ab = m->ib2;
  for (int i = 0; i < 16; i++) {
    int s = subset(m, b->partition, i);
    int c[4];
    for (int k = 0; k < 4; k++) c[k] = lerp(E[s * 2][k], E[s * 2 + 1][k], k < 3 ? weights(cb)[ci[i]] : weights(ab)[ai[i]]);
    out[i] = rotate(vmake(c[0], c[1], c[2], c[3]), b->rotation);
  }
}

static void pack(const Block *b, unsigned char *out) {
  const Mode *m = &MODES[b->mode];
  int pos = put(out, 0, 1 << b->mode, b->mode + 1);
  pos = put(out, pos, b->partition, m->pb);
  pos = put(out, pos, b->rotation, m->rb);
  pos = put(out, pos, b->isel, m->isb);
  for (int c = 0; c < 4; c++) for (int e = 0; e < m->ns * 2; e++) pos = put(out, pos, b->ep[e][c], bits_of(m, c));
  int np = m->epb ? m->ns * 2 : m->spb ? m->ns : 0;
  for (int i = 0; i < np; i++) pos = put(out, pos, b->pbits[i], 1);
  for (int i = 0; i < 16; i++) pos = put(out, pos, b->idx[i], m->ib - is_anchor(m, b->partition, i));
  if (m->ib2) for (int i = 0; i < 16; i++) pos = put(out, pos, b->idx2[i], m->ib2 - !i);
}

static int unpack(const unsigned char *in, Block *b) {
  int pos = 0;
  b->mode = 0;
  while (b->mode < 8 && !get(in, pos++, 1)) b->mode++;
  if (b->mode == 8) return 0;
  const Mode *m = &MODES[b->mode];
  b->partition = get(in, pos, m->pb), pos += m->pb;
  b->rotation = get(in, pos, m->rb), pos += m->rb;
  b->isel = get(in, pos, m->isb), pos += m->isb;
  for (int c = 0; c < 4; c++) for (int e = 0; e < m->ns * 2; e++) {
    int n = bits_of(m, c);
    b->ep[e][c] = get(in, pos, n), pos += n;
  }
  int np = m->epb ? m->ns * 2 : m->spb ? m->ns : 0;
  for (int i = 0; i < np; i++) b->pbits[i] = get(in, pos++, 1);
  for (int i = 0; i < 16; i++) {
    int n = m->ib - is_anchor(m, b->partition, i);
    b->idx[i] = get(in, pos, n), pos += n;
  }
  if (m->ib2) for (int i = 0; i < 16; i++) {
    int n = m->ib2 - !i;
    b->idx2[i] = get(in, pos, n), pos += n;
  }
  return 1;
}

static void bc7_decode(const unsigned char *in, float *px) {
  Block b;
  vec out[16];
  if (!unpack(in, &b)) {
    zero(px, 256);
    return;
  }
  colors(&b, out);
  for (int i = 0; i < 16; i++) store(px + i * 4, vdiv(out[i], splat(255)));
}
