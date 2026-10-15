#pragma once

#include "platform.h"
#include "half.h"
#include "inflate.h"

#define DECBITS 14
#define DECSIZE (1 << DECBITS)

typedef struct {
  u64 code[65537];
  int symbol[DECSIZE], head[DECSIZE], next[65537];
  unsigned short lut[65536];
  unsigned char len[65537], quick[DECSIZE], bitmap[8192];
} Piz;

typedef struct {
  const unsigned char *data;
  int p, length;
  u64 c;
  int lc;
} Bits;

static void fill(Bits *b) {
  b->c = b->c << 8 | (b->p < b->length ? b->data[b->p] : 0);
  b->p++;
  b->lc += 8;
}

static unsigned piz_take(Bits *b, int n) {
  b->lc -= n;
  unsigned v = b->c >> b->lc & ((1ull << n) - 1);
  b->c &= (1ull << b->lc) - 1;
  return v;
}

static unsigned piz_bits(Bits *b, int n) {
  while (b->lc < n) fill(b);
  return piz_take(b, n);
}

static int emit(Bits *b, unsigned short *out, int *o, int count, int s, int im) {
  if (s != im) {
    if (*o < count) out[(*o)++] = s;
    return 1;
  }
  if (b->lc < 8) fill(b);
  int run = piz_take(b, 8);
  if (!*o || *o + run > count) return 0;
  for (int k = 0; k < run; k++) out[*o + k] = out[*o - 1];
  *o += run;
  return 1;
}

static int huffman(Piz *z, const unsigned char *data, int length, unsigned short *out, int count) {
  if (length < 20) return 0;
  int im = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
  int iM = data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
  int nbits = data[12] | data[13] << 8 | data[14] << 16 | data[15] << 24;
  if (im < 0 || iM > 65536 || im > iM) return 0;
  Bits b = { data, 20, length, 0, 0 };
  zero(z->len, sizeof z->len);
  for (int i = im; i <= iM; i++) {
    int l = piz_bits(&b, 6);
    if (l == 63) i += piz_bits(&b, 8) + 5;
    else if (l >= 59) i += l - 58;
    else z->len[i] = l;
  }
  u64 counts[59] = { 0 };
  for (int i = im; i <= iM; i++) counts[z->len[i]]++;
  counts[0] = 0;
  u64 n = 0;
  for (int l = 58; l > 0; l--) {
    u64 m = (n + counts[l]) >> 1;
    counts[l] = n;
    n = m;
  }
  for (int i = im; i <= iM; i++) if (z->len[i]) z->code[i] = counts[z->len[i]]++;
  zero(z->quick, sizeof z->quick);
  for (int k = 0; k < DECSIZE; k++) z->head[k] = -1;
  for (int i = iM; i >= im; i--) {
    int l = z->len[i];
    if (l > DECBITS) {
      int k = z->code[i] >> (l - DECBITS) & (DECSIZE - 1);
      z->next[i] = z->head[k];
      z->head[k] = i;
    } else if (l) {
      int start = z->code[i] << (DECBITS - l);
      for (int k = start; k < start + (1 << (DECBITS - l)); k++) z->quick[k] = l, z->symbol[k] = i;
    }
  }
  b.c = 0;
  b.lc = 0;
  int end = b.p + (nbits + 7) / 8, o = 0;
  while (b.p < end) {
    fill(&b);
    while (b.lc >= DECBITS) {
      int k = b.c >> (b.lc - DECBITS) & (DECSIZE - 1);
      if (z->quick[k]) {
        piz_take(&b, z->quick[k]);
        if (!emit(&b, out, &o, count, z->symbol[k], iM)) return 0;
        continue;
      }
      int s = z->head[k];
      for (; s >= 0; s = z->next[s]) {
        while (b.lc < z->len[s] && b.p < end) fill(&b);
        if (b.lc >= z->len[s] && (b.c >> (b.lc - z->len[s]) & ((1ull << z->len[s]) - 1)) == z->code[s]) break;
      }
      if (s < 0) return 0;
      piz_take(&b, z->len[s]);
      if (!emit(&b, out, &o, count, s, iM)) return 0;
    }
  }
  int drop = (8 - nbits) & 7;
  b.c >>= drop;
  b.lc -= drop;
  while (b.lc > 0) {
    int k = b.c << (DECBITS - b.lc) & (DECSIZE - 1);
    if (!z->quick[k] || z->quick[k] > b.lc) return 0;
    piz_take(&b, z->quick[k]);
    if (!emit(&b, out, &o, count, z->symbol[k], iM)) return 0;
  }
  return 1;
}

static void dec14(unsigned short *x, unsigned short *y) {
  int hs = (short)*y;
  int a = (short)*x + (hs & 1) + (hs >> 1);
  *x = a;
  *y = a - hs;
}

static void dec16(unsigned short *x, unsigned short *y) {
  int l = *x, h = *y;
  int b = (l - (h >> 1)) & 0xffff;
  *x = (h + b - 32768) & 0xffff;
  *y = b;
}

static void wavelet(unsigned short *buf, int start, int nx, int ox, int ny, int oy, int max) {
  void (*dec)(unsigned short *, unsigned short *) = max < 1 << 14 ? dec14 : dec16;
  int p = 1;
  while (p <= (nx < ny ? nx : ny)) p <<= 1;
  p >>= 1;
  int p2 = p;
  p >>= 1;
  while (p >= 1) {
    int ox1 = ox * p, oy1 = oy * p, ox2 = ox * p2, oy2 = oy * p2;
    int py = start;
    for (; py <= start + oy * (ny - p2); py += oy2) {
      int px = py;
      for (; px <= py + ox * (nx - p2); px += ox2) {
        int p01 = px + ox1, p10 = px + oy1, p11 = p10 + ox1;
        dec(&buf[px], &buf[p10]);
        dec(&buf[p01], &buf[p11]);
        dec(&buf[px], &buf[p01]);
        dec(&buf[p10], &buf[p11]);
      }
      if (nx & p) dec(&buf[px], &buf[px + oy1]);
    }
    if (ny & p) for (int px = py; px <= py + ox * (nx - p2); px += ox2) dec(&buf[px], &buf[px + ox1]);
    p2 = p;
    p >>= 1;
  }
}

static void rows(const unsigned short *buf, unsigned char *out, int width, int lines, const int *chans, int count) {
  int o = 0;
  for (int y = 0; y < lines; y++) {
    int at = 0;
    for (int c = 0; c < count; c++) {
      int n = chans[c * 2] == 1 ? 1 : 2;
      for (int i = 0; i < width * n; i++) {
        int v = buf[at + y * width * n + i];
        out[o++] = v;
        out[o++] = v >> 8;
      }
      at += width * lines * n;
    }
  }
}

static int piz(Piz *z, const unsigned char *data, int length, unsigned short *buf, unsigned char *out, int size, int width, int lines, const int *chans, int count) {
  int min = data[0] | data[1] << 8, max = data[2] | data[3] << 8, p = 4;
  zero(z->bitmap, sizeof z->bitmap);
  if (min <= max) {
    if (max >= 8192 || p + max - min + 1 > length) return 0;
    for (int i = min; i <= max; i++) z->bitmap[i] = data[p++];
  }
  int k = 0;
  for (int i = 0; i < 65536; i++) if (!i || z->bitmap[i >> 3] & 1 << (i & 7)) z->lut[k++] = i;
  int n = data[p] | data[p + 1] << 8 | data[p + 2] << 16 | data[p + 3] << 24;
  if (n < 0 || p + 4 + n > length) return 0;
  zero(buf, size);
  if (!huffman(z, data + p + 4, n, buf, size / 2)) return 0;
  int at = 0;
  for (int c = 0; c < count; c++) {
    int w = chans[c * 2] == 1 ? 1 : 2;
    for (int j = 0; j < w; j++) wavelet(buf, at + j, width, w, lines, width * w, k - 1);
    at += width * lines * w;
  }
  for (int i = 0; i < size / 2; i++) buf[i] = z->lut[buf[i]];
  rows(buf, out, width, lines, chans, count);
  return 1;
}

static const unsigned char ORDER[15] = { 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };

static int block(const unsigned char *data, int length, int p, unsigned short *s) {
  if (p + 3 > length) return 0;
  s[0] = data[p] << 8 | data[p + 1];
  if (data[p + 2] >= 52) {
    for (int i = 1; i < 16; i++) s[i] = s[0];
  } else {
    if (p + 14 > length) return 0;
    int shift = data[p + 2] >> 2, bias = 0x20 << shift;
    for (int i = 0; i < 15; i++) {
      int bit = 22 + i * 6, d = ORDER[i];
      int v = (data[p + (bit >> 3)] << 8 | data[p + (bit >> 3) + 1]) >> (10 - (bit & 7)) & 63;
      s[d] = s[d % 4 ? d - 1 : d - 4] + (v << shift) - bias;
    }
  }
  for (int i = 0; i < 16; i++) s[i] = s[i] & 0x8000 ? s[i] & 0x7fff : ~s[i] & 0xffff;
  return data[p + 2] >= 52 ? 3 : 14;
}

static int b44(const unsigned char *data, int length, unsigned short *buf, unsigned char *out, int width, int lines, const int *chans, int count) {
  int p = 0;
  unsigned short *plane = buf;
  for (int c = 0; c < count; c++) {
    if (chans[c * 2] != 1) {
      for (int i = 0; i < width * lines * 2; i++, p += 2) plane[i] = p + 1 < length ? data[p] | data[p + 1] << 8 : 0;
      plane += width * lines * 2;
      continue;
    }
    for (int y = 0; y < lines; y += 4) {
      for (int x = 0; x < width; x += 4) {
        unsigned short s[16];
        int used = block(data, length, p, s);
        if (!used) return 0;
        p += used;
        for (int i = 0; i < 16; i++) {
          int px = x + (i & 3), py = y + (i >> 2);
          if (px < width && py < lines) plane[py * width + px] = s[i];
        }
      }
    }
    plane += width * lines;
  }
  rows(buf, out, width, lines, chans, count);
  return 1;
}

static const int SIZE[3] = { 4, 2, 4 };

static void exr_unrle(const unsigned char *in, int length, unsigned char *out, int size) {
  int i = 0, o = 0;
  while (i < length && o < size) {
    int n = (signed char)in[i++];
    if (n < 0) {
      for (int k = 0; k < -n && o < size; k++) out[o++] = in[i + k];
      i -= n;
    } else {
      for (int k = 0; k <= n && o < size; k++) out[o++] = in[i];
      i++;
    }
  }
}

static void unpredict(unsigned char *t, int length, unsigned char *out) {
  for (int i = 1; i < length; i++) t[i] = t[i - 1] + t[i] - 128;
  int half = (length + 1) >> 1;
  for (int i = 0; i < length; i++) out[i] = i & 1 ? t[half + (i >> 1)] : t[i >> 1];
}

static void pxr24(const unsigned char *tmp, int length, unsigned char *out, int width, int rows, const int *chans, int count) {
  int t = 0, o = 0;
  for (int y = 0; y < rows; y++) {
    for (int c = 0; c < count; c++) {
      int type = chans[c * 2], bytes = type == 0 ? 4 : type == 1 ? 2 : 3;
      unsigned pixel = 0;
      for (int x = 0; x < width; x++) {
        unsigned diff = 0;
        for (int b = 0; b < bytes; b++) diff = diff << 8 | (t + b * width + x < length ? tmp[t + b * width + x] : 0);
        if (type == 2) diff <<= 8;
        pixel += diff;
        out[o++] = pixel;
        out[o++] = pixel >> 8;
        if (type == 1) {
          pixel &= 0xffff;
          continue;
        }
        out[o++] = pixel >> 16;
        out[o++] = pixel >> 24;
      }
      t += bytes * width;
    }
  }
}

static float read(const unsigned char *p, int type, const float *half) {
  if (type == 2) {
    float v;
    copy(&v, p, 4);
    return v;
  }
  if (type == 0) return (unsigned)(p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24);
  return half[p[0] | p[1] << 8];
}

static void exr_fill(float *out, int count) {
  for (int i = 0; i < count; i++) {
    out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0;
    out[i * 4 + 3] = 1;
  }
}

static int exr_chunk(int kind, const unsigned char *src, int length, unsigned char *tmp, unsigned char *dst, int size, const int *chans, int count, float *out, int full, int x0, int y0, int width, int rows, Piz *z) {
  const unsigned char *data = src;
  if (kind == 1) {
    zero(tmp, size);
    exr_unrle(src, length, tmp, size);
    unpredict(tmp, size, dst);
    data = dst;
  }
  if (kind == 2) {
    unpredict(tmp, length, dst);
    data = dst;
  }
  if (kind == 3) {
    if (!piz(z, src, length, (unsigned short *)tmp, dst, size, width, rows, chans, count)) return 0;
    data = dst;
  }
  if (kind == 4) {
    pxr24(tmp, length, dst, width, rows, chans, count);
    data = dst;
  }
  if (kind == 5) {
    if (!b44(src, length, (unsigned short *)tmp, dst, width, rows, chans, count)) return 0;
    data = dst;
  }
  const float *half = HALF;
  int pos = 0;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < count; c++) {
      int type = chans[c * 2], mask = chans[c * 2 + 1];
      if (!mask) {
        pos += SIZE[type] * width;
        continue;
      }
      for (int x = 0; x < width; x++, pos += SIZE[type]) {
        float v = read(data + pos, type, half);
        float *o = out + ((y0 + r) * full + x0 + x) * 4;
        for (int k = 0; k < 4; k++) if (mask >> k & 1) o[k] = v;
      }
    }
  }
  return 1;
}

void tex_half_plane(const float *px, int count, int k, unsigned short *out) {
  for (int i = 0; i < count; i++) out[i] = to_half(px[i * 4 + k], 0);
}

static const int LINES[10] = { 1, 1, 1, 16, 32, 16, 32, 32, 32, 256 };

typedef struct {
  const unsigned char *b;
  int length, multipart, tiled, deep, compression, chlist, count, x0, y0, width, height, tx, ty, chunks, table, stride, cap;
} Exr;

typedef struct {
  int x0, y0, w, rows, size, length, kind;
  const unsigned char *data;
} Chunk;

static int i32(const unsigned char *p) { return (int)(p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24); }

static int fits(const Exr *e, int pos, int n) { return pos >= 0 && n >= 0 && pos <= e->length - n; }

static int text(const Exr *e, int pos) {
  while (pos < e->length && e->b[pos]) pos++;
  return pos < e->length ? pos + 1 : -1;
}

static int named(const Exr *e, int pos, const char *s) {
  while (*s && e->b[pos] == (unsigned char)*s) pos++, s++;
  return !*s && !e->b[pos];
}

static int starts(const Exr *e, int pos, int size, const char *s) {
  int n = 0;
  while (s[n] && n < size && e->b[pos + n] == (unsigned char)s[n]) n++;
  return !s[n];
}

static int chlist(Exr *e, int pos, int end) {
  e->chlist = pos;
  e->count = 0;
  e->stride = 0;
  while (pos < end && e->b[pos]) {
    int next = text(e, pos);
    if (next < 0 || next + 16 > end) return TEX_ERROR_DATA;
    int type = i32(e->b + next);
    if (type < 0 || type > 2) return TEX_ERROR_DATA;
    if (i32(e->b + next + 8) != 1 || i32(e->b + next + 12) != 1) return TEX_ERROR_SUBSAMPLED;
    e->stride += SIZE[type];
    e->count++;
    pos = next + 16;
  }
  return pos < end ? TEX_OK : TEX_ERROR_DATA;
}

static int attributes(Exr *e, int pos, int first, int *end) {
  while (pos < e->length && e->b[pos]) {
    int a = text(e, pos), b = a < 0 ? -1 : text(e, a);
    if (b < 0 || !fits(e, b, 4)) return TEX_ERROR_DATA;
    int size = i32(e->b + b), at = b + 4;
    if (!fits(e, at, size)) return TEX_ERROR_DATA;
    if (first && named(e, pos, "channels")) {
      int r = chlist(e, at, at + size);
      if (r) return r;
    }
    if (first && named(e, pos, "compression") && size >= 1) e->compression = e->b[at];
    if (first && named(e, pos, "dataWindow") && size >= 16) {
      e->x0 = i32(e->b + at), e->y0 = i32(e->b + at + 4);
      e->width = i32(e->b + at + 8) - e->x0 + 1, e->height = i32(e->b + at + 12) - e->y0 + 1;
    }
    if (first && named(e, pos, "tiles") && size >= 8) e->tx = i32(e->b + at), e->ty = i32(e->b + at + 4);
    if (first && named(e, pos, "type")) {
      e->deep |= starts(e, at, size, "deep");
      if (e->multipart) e->tiled = size == 10 && starts(e, at, size, "tiledimage");
    }
    pos = at + size;
  }
  if (pos >= e->length) return TEX_ERROR_DATA;
  *end = pos + 1;
  return TEX_OK;
}

static int chunk(const Exr *e, int i, Chunk *c) {
  int lo = i32(e->b + e->table + i * 8), hi = i32(e->b + e->table + i * 8 + 4);
  int at = lo + (e->multipart ? 4 : 0);
  if (hi || !fits(e, at, e->tiled ? 20 : 8)) return 0;
  c->x0 = e->tiled ? i32(e->b + at) * e->tx : 0;
  c->y0 = e->tiled ? i32(e->b + at + 4) * e->ty : i32(e->b + at) - e->y0;
  int p = at + (e->tiled ? 16 : 4);
  c->w = e->tiled ? (e->tx < e->width - c->x0 ? e->tx : e->width - c->x0) : e->width;
  int lines = e->tiled ? e->ty : LINES[e->compression];
  c->rows = lines < e->height - c->y0 ? lines : e->height - c->y0;
  if (c->x0 < 0 || c->y0 < 0 || c->w <= 0 || c->rows <= 0) return 0;
  c->size = c->rows * c->w * e->stride;
  c->length = i32(e->b + p);
  c->data = e->b + p + 4;
  if (!fits(e, p + 4, c->length)) return 0;
  int k = e->compression;
  c->kind = k == 0 || c->length == c->size ? 0 : k == 1 ? 1 : k <= 3 ? 2 : k == 4 ? 3 : k == 5 ? 4 : 5;
  return c->kind || c->length >= c->size;
}

static int exr_open(Exr *e, const unsigned char *b, int length) {
  zero(e, sizeof *e);
  e->b = b, e->length = length;
  if (length < 8 || i32(b) != 20000630) return TEX_ERROR_FORMAT;
  int flags = i32(b + 4), pos;
  e->multipart = !!(flags & 0x1000);
  e->tiled = !e->multipart && (flags & 0x200);
  e->deep = !!(flags & 0x800);
  int r = attributes(e, 8, 1, &pos);
  if (r) return r;
  while (e->multipart && pos < length && b[pos]) if ((r = attributes(e, pos, 0, &pos))) return r;
  if (e->multipart) pos++;
  if (e->deep) return TEX_ERROR_DEEP;
  if (e->compression > 9 || e->width <= 0 || e->height <= 0 || e->width > 65536 || e->height > 65536 || !e->count) return TEX_ERROR_DATA;
  if (e->compression >= 8) return TEX_ERROR_COMPRESSION;
  if (e->tiled && (e->tx <= 0 || e->ty <= 0)) return TEX_ERROR_DATA;
  e->chunks = e->tiled ? ((e->width + e->tx - 1) / e->tx) * ((e->height + e->ty - 1) / e->ty) : (e->height + LINES[e->compression] - 1) / LINES[e->compression];
  e->table = pos;
  if (!fits(e, pos, e->chunks * 8)) return TEX_ERROR_DATA;
  e->cap = 1;
  for (int i = 0; i < e->chunks; i++) {
    Chunk c;
    if (!chunk(e, i, &c)) return TEX_ERROR_DATA;
    if (c.size > e->cap) e->cap = c.size;
    if (c.length > e->cap) e->cap = c.length;
  }
  return TEX_OK;
}

static int exr_scratch(const Exr *e) {
  int piz = e->compression == 4 ? ((int)sizeof(Piz) + 15) & ~15 : 0;
  return piz + ((e->count * 8 + 15) & ~15) + ((e->cap + 15) & ~15) * 2;
}

static int find(const Exr *e, char c) {
  int best = -1, pos = e->chlist;
  for (int i = 0; i < e->count; i++) {
    int next = text(e, pos), n = next - 1 - pos;
    const unsigned char *s = e->b + pos;
    if (n == 1 && s[0] == c) return i;
    if (best < 0 && n >= 2 && s[n - 1] == c && s[n - 2] == '.') best = i;
    pos = next + 16;
  }
  return best;
}

int tex_exr_info(const unsigned char *bytes, int length, tex_info *info) {
  Exr e;
  int r = exr_open(&e, bytes, length);
  zero(info, sizeof *info);
  info->compression = e.compression;
  if (r) return r;
  info->width = e.width, info->height = e.height, info->scratch = exr_scratch(&e);
  info->bits = 16;
  for (int i = 0, pos = e.chlist; i < e.count; i++, pos = text(&e, pos) + 16) if (i32(bytes + text(&e, pos)) != 1) info->bits = 32;
  return TEX_OK;
}

int tex_exr_read(const unsigned char *bytes, int length, float *out, void *scratch) {
  Exr e;
  int r = exr_open(&e, bytes, length);
  if (r) return r;
  unsigned char *at = scratch;
  Piz *z = e.compression == 4 ? (Piz *)at : 0;
  if (z) at += ((int)sizeof(Piz) + 15) & ~15;
  int *chans = (int *)at;
  at += (e.count * 8 + 15) & ~15;
  unsigned char *tmp = at, *dst = at + ((e.cap + 15) & ~15);
  for (int i = 0, pos = e.chlist; i < e.count; i++, pos = text(&e, pos) + 16) chans[i * 2] = i32(bytes + text(&e, pos)), chans[i * 2 + 1] = 0;
  int r0 = find(&e, 'R'), y = find(&e, 'Y');
  int rgba[4] = { r0, find(&e, 'G'), find(&e, 'B'), find(&e, 'A') };
  if (r0 < 0 && y >= 0) rgba[0] = rgba[1] = rgba[2] = y;
  for (int k = 0; k < 4; k++) if (rgba[k] >= 0) chans[rgba[k] * 2 + 1] |= 1 << k;
  exr_fill(out, e.width * e.height);
  for (int i = 0; i < e.chunks; i++) {
    Chunk c;
    chunk(&e, i, &c);
    int len = c.length;
    if (c.kind == 2 || c.kind == 4) len = unzlib(c.data, c.length, tmp, e.cap);
    if (len < 0 || !exr_chunk(c.kind, c.data, len, tmp, dst, c.size, chans, e.count, out, e.width, c.x0, c.y0, c.w, c.rows, z)) return TEX_ERROR_DATA;
  }
  return TEX_OK;
}
