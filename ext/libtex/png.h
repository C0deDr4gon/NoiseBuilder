#pragma once

#include "platform.h"
#include "vec.h"
#include "inflate.h"

static const int CHANNELS[7] = { 1, 0, 3, 1, 2, 0, 4 };
static const int ADAM7[7][4] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };

static int paeth(int a, int b, int c) {
  int p = a + b - c;
  int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static void paeth_row(unsigned char *line, const unsigned char *prev, int stride, int bpp) {
  int i = 0;
  for (; i < bpp && i < stride; i++) line[i] += prev[i];
  int g = bpp == 3 ? 3 : 4;
  ivec keep = imake(-1, -1, -1, g == 4 ? -1 : 0);
  for (; bpp >= 3 && i + 4 <= stride; i += g) {
    ivec a = bytes4(iload4(line + i - bpp)), b = bytes4(iload4(prev + i)), c = bytes4(iload4(prev + i - bpp));
    ivec pa = iabs(isub(b, c)), pb = iabs(isub(a, c)), pc = iabs(isub(iadd(a, b), iadd(c, c)));
    ivec pred = iand(iselect(ior(igt(pa, pb), igt(pa, pc)), iselect(igt(pb, pc), c, b), a), keep);
    ivec r = iand(iadd(bytes4(iload4(line + i)), pred), isplat(255));
    istore4(line + i, narrow(r, r, r, r));
  }
  for (; i < stride; i++) line[i] += paeth(line[i - bpp], prev[i], prev[i - bpp]);
}

static void unfilter(int f, unsigned char *line, const unsigned char *prev, int stride, int bpp) {
  int first = bpp < stride ? bpp : stride;
  if (f == 1 || (f == 4 && !prev)) {
    for (int i = bpp; i < stride; i++) line[i] += line[i - bpp];
  } else if (f == 2 && prev) {
    for (int i = 0; i < stride; i++) line[i] += prev[i];
  } else if (f == 3) {
    for (int i = 0; i < first; i++) line[i] += (prev ? prev[i] : 0) >> 1;
    for (int i = bpp; i < stride; i++) line[i] += (line[i - bpp] + (prev ? prev[i] : 0)) >> 1;
  } else if (f == 4) {
    paeth_row(line, prev, stride, bpp);
  }
}

static int sample(const unsigned char *line, int i, int depth) {
  if (depth == 8) return line[i];
  if (depth == 16) return line[i * 2] << 8 | line[i * 2 + 1];
  int bit = i * depth;
  return line[bit >> 3] >> (8 - depth - (bit & 7)) & ((1 << depth) - 1);
}

typedef struct {
  int color, depth, n;
  double max;
  float unit[256];
  const unsigned char *palette, *trns;
  int trns_length;
} Png;

static void png_pixel(const Png *p, float *out, const unsigned char *line, int x) {
  if (p->color == 3) {
    int i = sample(line, x, p->depth);
    for (int k = 0; k < 3; k++) out[k] = p->palette[i * 3 + k] / 255.0;
    out[3] = p->trns && i < p->trns_length ? p->trns[i] / 255.0 : 1;
    return;
  }
  int gray = p->color == 0 || p->color == 4;
  int key = p->trns != 0;
  for (int k = 0; k < 3; k++) {
    int v = sample(line, x * p->n + (gray ? 0 : k), p->depth);
    out[k] = p->depth <= 8 ? p->unit[v] : v / p->max;
    if (key) key = v == (p->trns[gray ? 0 : k * 2] << 8 | p->trns[(gray ? 0 : k * 2) + 1]);
  }
  int a = p->color == 4 || p->color == 6 ? sample(line, x * p->n + p->n - 1, p->depth) : -1;
  out[3] = a < 0 ? !key : p->depth <= 8 ? p->unit[a] : a / p->max;
}

static int png_value(const unsigned char *line, int i, int wide) { return wide ? line[i * 2] << 8 | line[i * 2 + 1] : line[i]; }

static void png_row(const Png *p, float *out, const unsigned char *line, int width) {
  int wide = p->depth == 16, m = (int)p->max;
  vec max = splat(p->max);
  if (p->n == 1) for (int x = 0; x < width; x++) {
    int v = png_value(line, x, wide);
    store(out + x * 4, vdiv(tofloatu(imake(v, v, v, m)), max));
  }
  if (p->n == 2) for (int x = 0; x < width; x++) {
    int v = png_value(line, x * 2, wide);
    store(out + x * 4, vdiv(tofloatu(imake(v, v, v, png_value(line, x * 2 + 1, wide))), max));
  }
  if (p->n == 3) for (int x = 0; x < width; x++) store(out + x * 4, vdiv(tofloatu(imake(png_value(line, x * 3, wide), png_value(line, x * 3 + 1, wide), png_value(line, x * 3 + 2, wide), m)), max));
  if (p->n == 4) for (int x = 0; x < width; x++) store(out + x * 4, vdiv(tofloatu(imake(png_value(line, x * 4, wide), png_value(line, x * 4 + 1, wide), png_value(line, x * 4 + 2, wide), png_value(line, x * 4 + 3, wide))), max));
}

static void png_decode(unsigned char *raw, int width, int height, int depth, int color, int interlace, const unsigned char *palette, const unsigned char *trns, int trns_length, float *out) {
  Png p = { color, depth, CHANNELS[color], (1 << depth) - 1, { 0 }, palette, trns, trns_length };
  for (int v = 0; v < 256; v++) p.unit[v] = v / p.max;
  int bpp = p.n * depth >> 3;
  if (bpp < 1) bpp = 1;
  int off = 0, quick = depth >= 8 && color != 3 && !trns;
  for (int pass = 0; pass < (interlace ? 7 : 1); pass++) {
    int x0 = interlace ? ADAM7[pass][0] : 0, y0 = interlace ? ADAM7[pass][1] : 0;
    int dx = interlace ? ADAM7[pass][2] : 1, dy = interlace ? ADAM7[pass][3] : 1;
    int pw = (width - x0 + dx - 1) / dx, ph = (height - y0 + dy - 1) / dy;
    if (pw <= 0 || ph <= 0) continue;
    int stride = (pw * p.n * depth + 7) / 8;
    unsigned char *prev = 0;
    for (int y = 0; y < ph; y++) {
      unsigned char *line = raw + off + 1;
      unfilter(raw[off], line, prev, stride, bpp);
      off += 1 + stride;
      if (quick && !interlace) png_row(&p, out + y * width * 4, line, pw);
      else for (int x = 0; x < pw; x++) png_pixel(&p, out + ((y0 + y * dy) * width + x0 + x * dx) * 4, line, x);
      prev = line;
    }
  }
}

static int filter(int f, const unsigned char *cur, const unsigned char *prev, int i, int bpp) {
  int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
  int p = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) >> 1 : f == 4 ? paeth(a, b, c) : 0;
  return (cur[i] - p) & 255;
}

void tex_png_rows(const float *px, int w, int h, int n, int depth, int mode, unsigned char *out, unsigned char *scratch) {
  int size = depth / 8, stride = w * n * size, bpp = n * size;
  double max = depth == 16 ? 65535 : 255;
  unsigned char *prev = scratch, *cur = scratch + stride;
  for (int i = 0; i < stride; i++) prev[i] = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      for (int k = 0; k < n; k++) {
        int v = (int)fround(clamp(px[(y * w + x) * 4 + k], 0, 1) * max), at = (x * n + k) * size;
        if (size == 2) cur[at] = v >> 8, cur[at + 1] = v & 255;
        else cur[at] = v;
      }
    }
    int best = mode < 0 ? 0 : mode;
    unsigned least = ~0u;
    for (int f = 0; f < 5 && mode < 0; f++) {
      unsigned sum = 0;
      for (int i = 0; i < stride && sum < least; i++) {
        int d = filter(f, cur, prev, i, bpp);
        sum += d < 128 ? d : 256 - d;
      }
      if (sum < least) least = sum, best = f;
    }
    unsigned char *row = out + y * (stride + 1);
    row[0] = best;
    for (int i = 0; i < stride; i++) row[i + 1] = filter(best, cur, prev, i, bpp);
    unsigned char *t = prev;
    prev = cur, cur = t;
  }
}

typedef struct {
  const unsigned char *b, *palette, *trns;
  int length, width, height, depth, color, interlace, palette_length, trns_length, idat, raw;
} PngFile;

static int be32(const unsigned char *p) { return (int)((unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

static int is(const unsigned char *p, const char *s) { return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3]; }

static int depth_ok(int color, int depth) {
  if (color == 0) return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
  if (color == 3) return depth == 1 || depth == 2 || depth == 4 || depth == 8;
  return (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
}

static long long raw_size(const PngFile *f) {
  long long total = 0;
  int n = CHANNELS[f->color];
  for (int pass = 0; pass < (f->interlace ? 7 : 1); pass++) {
    int x0 = f->interlace ? ADAM7[pass][0] : 0, y0 = f->interlace ? ADAM7[pass][1] : 0;
    int dx = f->interlace ? ADAM7[pass][2] : 1, dy = f->interlace ? ADAM7[pass][3] : 1;
    long long pw = (f->width - x0 + dx - 1) / dx, ph = (f->height - y0 + dy - 1) / dy;
    if (pw > 0 && ph > 0) total += ph * (1 + (pw * n * f->depth + 7) / 8);
  }
  return total;
}

static int png_open(PngFile *f, const unsigned char *b, int length) {
  zero(f, sizeof *f);
  f->b = b, f->length = length;
  if (length < 8 || be32(b) != (int)0x89504e47 || be32(b + 4) != 0x0d0a1a0a) return TEX_ERROR_FORMAT;
  int pos = 8, header = 0;
  while (pos <= length - 8) {
    int len = be32(b + pos);
    const unsigned char *type = b + pos + 4, *data = b + pos + 8;
    if (len < 0 || len > length - pos - 8) return TEX_ERROR_DATA;
    if (is(type, "IHDR") && len >= 13) {
      f->width = be32(data), f->height = be32(data + 4);
      f->depth = data[8], f->color = data[9], f->interlace = data[12];
      header = data[10] == 0 && data[11] == 0 && f->interlace <= 1;
    } else if (is(type, "PLTE")) f->palette = data, f->palette_length = len;
    else if (is(type, "tRNS")) f->trns = data, f->trns_length = len;
    else if (is(type, "IDAT")) f->idat += len;
    else if (is(type, "IEND")) break;
    pos += 12 + len;
  }
  if (!header || !depth_ok(f->color, f->depth) || f->width <= 0 || f->height <= 0 || f->width > 65536 || f->height > 65536 || !f->idat) return TEX_ERROR_DATA;
  if (f->color == 3 && !f->palette) return TEX_ERROR_DATA;
  if (f->trns && f->trns_length < (f->color == 0 ? 2 : f->color == 2 ? 6 : 0)) f->trns = 0;
  if (f->trns && (f->color == 4 || f->color == 6)) f->trns = 0;
  long long raw = raw_size(f);
  if (raw > 0x40000000) return TEX_ERROR_DATA;
  f->raw = (int)raw;
  return TEX_OK;
}

int tex_png_info(const unsigned char *bytes, int length, tex_info *info) {
  PngFile f;
  int r = png_open(&f, bytes, length);
  zero(info, sizeof *info);
  if (r) return r;
  info->width = f.width, info->height = f.height, info->bits = f.depth;
  info->scratch = 1024 + f.idat + f.raw;
  return TEX_OK;
}

int tex_png_read(const unsigned char *bytes, int length, float *out, void *scratch) {
  PngFile f;
  int r = png_open(&f, bytes, length);
  if (r) return r;
  unsigned char *palette = scratch, *trns = palette + 768, *idat = trns + 256, *raw = idat + f.idat;
  zero(palette, 1024);
  if (f.palette) copy(palette, f.palette, f.palette_length < 768 ? f.palette_length : 768);
  int trns_length = f.trns_length < 256 ? f.trns_length : 256;
  if (f.trns) copy(trns, f.trns, trns_length);
  int at = 0;
  for (int pos = 8; pos <= length - 8;) {
    int len = be32(bytes + pos);
    if (is(bytes + pos + 4, "IDAT")) copy(idat + at, bytes + pos + 8, len), at += len;
    if (is(bytes + pos + 4, "IEND")) break;
    pos += 12 + len;
  }
  zero(raw, f.raw);
  if (unzlib(idat, f.idat, raw, f.raw) < 0) return TEX_ERROR_DATA;
  png_decode(raw, f.width, f.height, f.depth, f.color, f.interlace, palette, f.trns ? trns : 0, trns_length, out);
  return TEX_OK;
}
