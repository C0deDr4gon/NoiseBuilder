#pragma once

#include "platform.h"
#include "vec.h"

#define FAST 10
#define KIND(k) ((unsigned)(k) << 6)
#define KINDS KIND(7)


typedef struct {
  short count[16], symbol[288];
  u64 fast[1 << FAST];
} Huff;

typedef struct {
  const unsigned char *in;
  int length, pos, count;
  u64 bits;
  unsigned char *out;
  int cap, at;
} Flate;

static const short LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const unsigned char CLORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static void refill(Flate *s) {
  if (s->pos <= s->length - 8) {
    u64 v;
    copy(&v, s->in + s->pos, 8);
    s->bits |= v << s->count;
    s->pos += (63 - s->count) >> 3;
    s->count |= 56;
    return;
  }
  while (s->count <= 56) {
    s->bits |= (u64)(s->pos < s->length ? s->in[s->pos] : 0) << s->count;
    s->pos++;
    s->count += 8;
  }
}

static unsigned need(Flate *s, int n) {
  if (s->count < n) refill(s);
  unsigned v = (unsigned)(s->bits & (((u64)1 << n) - 1));
  s->bits >>= n;
  s->count -= n;
  return v;
}

static unsigned entry(int mode, int sym) {
  if (mode == 0) return (unsigned)sym << 16;
  if (mode == 2) return sym < 30 ? (unsigned)DBASE[sym] << 16 | DEXT[sym] << 9 | KIND(1) : KIND(3);
  if (sym < 256) return (unsigned)sym << 16;
  if (sym == 256) return KIND(2);
  return sym < 286 ? (unsigned)LBASE[sym - 257] << 16 | LEXT[sym - 257] << 9 | KIND(1) : KIND(3);
}

static int huff(Huff *h, const unsigned char *len, int n, int mode) {
  short offs[16];
  zero(h, sizeof *h);
  for (int i = 0; i < 1 << FAST; i++) h->fast[i] = KIND(4);
  for (int i = 0; i < n; i++) h->count[len[i]]++;
  h->count[0] = 0;
  int left = 1;
  for (int l = 1; l < 16; l++) {
    left = (left << 1) - h->count[l];
    if (left < 0) return 0;
  }
  offs[1] = 0;
  for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
  for (int i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = i;
  int code = 0, at = 0;
  for (int l = 1; l <= FAST; l++) {
    for (int k = 0; k < h->count[l]; k++, code++) {
      int r = 0;
      for (int b = 0; b < l; b++) r |= (code >> b & 1) << (l - 1 - b);
      unsigned e = entry(mode, h->symbol[at++]) | l;
      int x = e >> 9 & 15;
      if ((e & KINDS) == KIND(1) && l + x <= FAST) {
        for (int v = 0; v < 1 << x; v++) for (int q = r | v << l; q < 1 << FAST; q += 1 << (l + x)) h->fast[q] = ((e >> 16) + v) << 16 | KIND(5) | (l + x);
      } else {
        for (; r < 1 << FAST; r += 1 << l) h->fast[r] = e;
      }
    }
    code <<= 1;
  }
  return 1;
}

static int slow(u64 bits, const Huff *h) {
  int code = 0, first = 0, index = 0;
  for (int l = 1; l < 16; l++, bits >>= 1) {
    code |= (int)(bits & 1);
    int c = h->count[l];
    if (code - c < first) return h->symbol[index + code - first] << 4 | l;
    index += c;
    first = (first + c) << 1;
    code <<= 1;
  }
  return -1;
}

static int symbol(Flate *s, const Huff *h) {
  if (s->count < 16) refill(s);
  unsigned e = h->fast[s->bits & ((1 << FAST) - 1)];
  if ((e & KINDS) != KIND(4)) {
    need(s, e & 15);
    return e >> 16;
  }
  int c = slow(s->bits, h);
  if (c >= 0) need(s, c & 15);
  return c < 0 ? -1 : c >> 4;
}

static unsigned lookup(u64 bits, const Huff *h, int mode) {
  unsigned e = h->fast[bits & ((1 << FAST) - 1)];
  if ((e & KINDS) != KIND(4)) return e;
  int c = slow(bits, h);
  return c < 0 ? KIND(3) | 1 : entry(mode, c >> 4) | (c & 15);
}

static THREAD Huff LIT, DIST;

static int extra(Flate *s, u64 e) {
  int v = (int)(e >> 16);
  if ((e & KINDS) == KIND(1)) v += need(s, e >> 9 & 15);
  return v;
}

static int tail(Flate *s) {
  for (;;) {
    if (s->count < 48) refill(s);
    u64 e = lookup(s->bits, &LIT, 1), k = e & KINDS;
    need(s, e & 15);
    if (!k) {
      if (s->at >= s->cap) return 0;
      s->out[s->at++] = e >> 16;
      continue;
    }
    if (k != KIND(1) && k != KIND(5)) return k == KIND(2);
    int len = extra(s, e);
    e = lookup(s->bits, &DIST, 2), k = e & KINDS;
    need(s, e & 15);
    if (k != KIND(1) && k != KIND(5)) return 0;
    int back = extra(s, e);
    if (back > s->at || len > s->cap - s->at) return 0;
    for (int i = 0; i < len; i++, s->at++) s->out[s->at] = s->out[s->at - back];
  }
}

static int codes(Flate *s) {
  const Huff *lit = &LIT, *dist = &DIST;
  const unsigned char *ip = s->in + s->pos, *iend = s->in + s->length - 16;
  unsigned char *op = s->out + s->at, *start = s->out, *oend = s->out + s->cap;
  const u64 *lf = LIT.fast, *df = DIST.fast;
  u64 bits = s->bits, v;
  u64 count = (u64)s->count;
  int r = -1;
#define REFILL \
  copy(&v, ip, 8); \
  bits |= v << (count & 63); \
  ip += (63 - (count & 63)) >> 3; \
  count |= 56;
  while (ip <= iend && oend - op >= 274) {
    REFILL
    u64 e = lf[bits & ((1 << FAST) - 1)];
#define LITERAL \
    if (e & KINDS) goto match; \
    bits >>= e & 63, count -= e; \
    *op++ = e >> 16; \
    e = lf[bits & ((1 << FAST) - 1)];
    LITERAL
    LITERAL
    LITERAL
    LITERAL
#undef LITERAL
    continue;
  match:
    if ((e & KINDS) == KIND(4)) e = lookup(bits, lit, 1);
    bits >>= e & 63, count -= e;
    if (!(e & KINDS)) {
      *op++ = e >> 16;
      continue;
    }
    if ((e & KINDS) != KIND(1) && (e & KINDS) != KIND(5)) {
      r = (e & KINDS) == KIND(2);
      break;
    }
    REFILL
    int len = e >> 16;
    if ((e & KINDS) == KIND(1)) {
      int x = e >> 9 & 15;
      len += (int)(bits & ((1u << x) - 1));
      bits >>= x, count -= x;
    }
    e = df[bits & ((1 << FAST) - 1)];
    if ((e & KINDS) == KIND(4)) e = lookup(bits, dist, 2);
    bits >>= e & 63, count -= e;
    int back = e >> 16;
    if ((e & KINDS) == KIND(1)) {
      int x = e >> 9 & 15;
      back += (int)(bits & ((1u << x) - 1));
      bits >>= x, count -= x;
    } else if ((e & KINDS) != KIND(5)) {
      r = 0;
      break;
    }
    if (back > op - start) {
      r = 0;
      break;
    }
    unsigned char *o = op;
    op += len;
    if (back >= 16 || (len <= back && len <= 16)) {
      for (int k = 0; k < len; k += 16) istore(o + k, iload(o + k - back));
    } else if (back >= 8) {
      for (int k = 0; k < len; k += 8) copy(o + k, o + k - back, 8);
    } else if (back >= 4) {
      for (int k = 0; k < len; k += 4) copy(o + k, o + k - back, 4);
    } else {
      for (int k = 0; k < len; k++) o[k] = o[k - back];
    }
  }
#undef REFILL
  s->bits = bits, s->count = (int)(count & 63), s->pos = (int)(ip - s->in), s->at = (int)(op - start);
  return r >= 0 ? r : tail(s);
}

static int fixed(Flate *s) {
  unsigned char len[320];
  for (int i = 0; i < 288; i++) len[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
  for (int i = 0; i < 30; i++) len[288 + i] = 5;
  return huff(&LIT, len, 288, 1) && huff(&DIST, len + 288, 30, 2) && codes(s);
}

static int dynamic(Flate *s) {
  unsigned char len[320];
  Huff code;
  int nlen = need(s, 5) + 257, ndist = need(s, 5) + 1, ncode = need(s, 4) + 4;
  if (nlen > 286 || ndist > 30) return 0;
  zero(len, 19);
  for (int i = 0; i < ncode; i++) len[CLORDER[i]] = need(s, 3);
  if (!huff(&code, len, 19, 0)) return 0;
  for (int i = 0; i < nlen + ndist;) {
    int c = symbol(s, &code);
    if (c < 0) return 0;
    if (c < 16) {
      len[i++] = c;
      continue;
    }
    int v = 0, n;
    if (c == 16) {
      if (!i) return 0;
      v = len[i - 1];
      n = 3 + need(s, 2);
    } else n = c == 17 ? 3 + need(s, 3) : 11 + need(s, 7);
    if (i + n > nlen + ndist) return 0;
    while (n--) len[i++] = v;
  }
  if (!len[256]) return 0;
  return huff(&LIT, len, nlen, 1) && huff(&DIST, len + nlen, ndist, 2) && codes(s);
}

static int stored(Flate *s) {
  need(s, s->count & 7);
  int len = need(s, 16), nlen = need(s, 16);
  if (len != (~nlen & 0xffff) || len > s->cap - s->at) return 0;
  while (len--) s->out[s->at++] = need(s, 8);
  return 1;
}

static int unzlib(const unsigned char *in, int length, unsigned char *out, int cap) {
  if (length < 2 || (in[0] & 15) != 8 || (in[0] << 8 | in[1]) % 31) return -1;
  Flate s = { in + 2, length - 2, 0, 0, 0, out, cap, 0 };
  int last;
  do {
    last = need(&s, 1);
    int type = need(&s, 2);
    int ok = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : 0;
    if (!ok || s.pos - s.count / 8 > s.length) return -1;
  } while (!last);
  return s.at;
}
