#pragma once

#include "platform.h"
#include "vec.h"

static void tga_unrle(const unsigned char *buf, int length, int pos, unsigned char *out, int size, int n) {
  int o = 0;
  while (o < size && pos < length) {
    int h = buf[pos++];
    int len = (h & 127) + 1;
    for (int j = 0; j < len * n && o + j < size; j++) {
      int at = pos + (h & 128 ? j % n : j);
      if (at < length) out[o + j] = buf[at];
    }
    pos += h & 128 ? n : len * n;
    o += len * n;
  }
}

static int row(int r, int height, int step) {
  for (int s = 0; s < step; s++) {
    int count = (height - s + step - 1) / step;
    if (r < count) return s + r * step;
    r -= count;
  }
  return 0;
}

typedef struct {
  int alpha, premultiplied;
} Tga;

static float UNIT[256];

static void tga_init(void) {
  for (int v = 0; v < 256; v++) UNIT[v] = v / 255.0;
}

static void tga_fast(float *out, const unsigned char *s, int n, int gray, int alpha) {
  out[0] = UNIT[s[gray ? 0 : n >= 3 ? 2 : 0]];
  out[1] = UNIT[s[gray ? 0 : n >= 3 ? 1 : 0]];
  out[2] = UNIT[s[0]];
  out[3] = alpha && n == (gray ? 2 : 4) ? UNIT[s[n - 1]] : 1;
}

static vec unit(ivec b) {
  return vdiv(tofloatu(bytes4(b)), splat(255));
}

static int tga_line(float *o, const unsigned char *p, int width, int n, int alpha, int room) {
  int x = 0;
  for (; x + 4 <= width && x * n + 16 <= room; x += 4, p += 4 * n, o += 16) {
    ivec b = iload(p);
    if (n == 4) b = bshuffle(b, 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
    else if (n == 3) b = bshuffle(b, 2, 1, 0, 0, 5, 4, 3, 0, 8, 7, 6, 0, 11, 10, 9, 0);
    else b = bshuffle(b, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
    vec c[4] = { unit(b), unit(ishuffle(b, 1, 1, 1, 1)), unit(ishuffle(b, 2, 2, 2, 2)), unit(ishuffle(b, 3, 3, 3, 3)) };
    for (int k = 0; k < 4; k++) store(o + k * 4, n == 4 && alpha ? c[k] : with(c[k], 3, 1));
  }
  return x;
}

static void tga_pixel(const Tga *t, float *out, const unsigned char *s, int n, int gray) {
  double r = s[0], g = s[0], b = s[0];
  double a = n == 2 && t->alpha ? s[1] : 255;
  if (!gray && n >= 3) {
    r = s[2];
    g = s[1];
    a = n == 4 && t->alpha ? s[3] : 255;
  } else if (!gray) {
    int v = s[0] | s[1] << 8;
    r = (v >> 10 & 31) * 255 / 31.0;
    g = (v >> 5 & 31) * 255 / 31.0;
    b = (v & 31) * 255 / 31.0;
    a = t->alpha && !(v & 0x8000) ? 0 : 255;
  }
  double m = t->premultiplied && a > 0 ? 1 / a : 1 / 255.0;
  out[0] = r * m < 1 ? r * m : 1;
  out[1] = g * m < 1 ? g * m : 1;
  out[2] = b * m < 1 ? b * m : 1;
  out[3] = a / 255;
}

static void tga_decode(const unsigned char *buf, int length, int pos, int rle, unsigned char *scratch, int width, int height, int n, int kind, int desc, const unsigned char *cmap, int cmap_bytes, int cmap_start, int cmap_count, int alpha, int premultiplied, float *out) {
  static const unsigned char none[4] = { 0 };
  Tga t = { alpha, premultiplied };
  int quick = !premultiplied && (kind == 3 || n >= 3);
  int count = width * height;
  int size = count * n;
  const unsigned char *src = buf + pos;
  if (rle || pos + size > length) {
    zero(scratch, size);
    if (rle) tga_unrle(buf, length, pos, scratch, size, n);
    else for (int i = 0; pos + i < length; i++) scratch[i] = buf[pos + i];
    src = scratch;
  }
  int step = desc >> 6 == 1 ? 2 : desc >> 6 == 2 ? 4 : 1;
  if (quick && kind != 1 && n != 2 && step == 1 && !(desc & 0x10)) {
    for (int y = 0; y < height; y++) {
      float *o = out + (desc & 0x20 ? y : height - 1 - y) * width * 4;
      const unsigned char *p = src + y * width * n;
      int x = tga_line(o, p, width, n, alpha, size - y * width * n);
      for (; x < width; x++) tga_fast(o + x * 4, p + x * n, n, kind == 3, alpha);
    }
    return;
  }
  for (int i = 0; i < count; i++) {
    int x = i % width;
    int y = row(i / width, height, step);
    int dx = desc & 0x10 ? width - 1 - x : x;
    int dy = desc & 0x20 ? y : height - 1 - y;
    float *o = out + (dy * width + dx) * 4;
    const unsigned char *p = src + i * n;
    if (kind == 1) {
      int k = (n == 1 ? p[0] : p[0] | p[1] << 8) - cmap_start;
      tga_pixel(&t, o, k >= 0 && k < cmap_count ? cmap + k * cmap_bytes : none, cmap_bytes, 0);
    }
    else if (quick) tga_fast(o, p, n, kind == 3, alpha);
    else tga_pixel(&t, o, p, n, kind == 3);
  }
}

static unsigned tga_pack(const float *p) {
  ivec q = vtrunc(add(mul(vmin(vmax(load(p), splat(0)), splat(1)), splat(255)), splat(0.5f)));
  return ilane(narrow(q, q, q, q), 0);
}

int tex_tga_gray(const float *px, int count) {
  for (int i = 0; i < count; i++) {
    unsigned p = tga_pack(px + i * 4);
    if ((p & 255) != (p >> 8 & 255) || (p & 255) != (p >> 16 & 255)) return 0;
  }
  return 1;
}

static void bytes(const float *px, int w, int n, unsigned char *row) {
  for (int x = 0; x < w; x++) {
    unsigned p = tga_pack(px + x * 4);
    unsigned char *o = row + x * n;
    if (n < 3) o[0] = p, o[n - 1] = n == 2 ? p >> 24 : p;
    else o[0] = p >> 16, o[1] = p >> 8, o[2] = p;
    if (n == 4) o[3] = p >> 24;
  }
}

static int same(const unsigned char *row, int a, int b, int n) {
  for (int k = 0; k < n; k++) if (row[a * n + k] != row[b * n + k]) return 0;
  return 1;
}

int tex_tga_pixels(const float *px, int w, int h, int n, int rle, unsigned char *out, unsigned char *row) {
  int at = 0;
  for (int y = 0; y < h; y++) {
    bytes(px + y * w * 4, w, n, row);
    if (!rle) {
      for (int i = 0; i < w * n; i++) out[at++] = row[i];
      continue;
    }
    for (int x = 0; x < w;) {
      int run = 1;
      while (x + run < w && run < 128 && same(row, x, x + run, n)) run++;
      if (run > 1) {
        out[at++] = 0x80 | (run - 1);
        for (int k = 0; k < n; k++) out[at++] = row[x * n + k];
        x += run;
        continue;
      }
      int start = x, count = 0;
      while (x < w && count < 128 && !(x + 1 < w && same(row, x, x + 1, n))) x++, count++;
      out[at++] = count - 1;
      for (int i = start * n; i < x * n; i++) out[at++] = row[i];
    }
  }
  return at;
}

int tex_tga_header(int w, int h, int n, int rle, unsigned char *out) {
  zero(out, 18);
  out[2] = (n < 3 ? 3 : 2) + (rle ? 8 : 0);
  out[12] = w & 255, out[13] = w >> 8 & 255;
  out[14] = h & 255, out[15] = h >> 8 & 255;
  out[16] = n * 8;
  out[17] = (n == 2 || n == 4 ? 8 : 0) | 0x20;
  return 18;
}

typedef struct {
  int type, kind, width, height, n, desc, pos, cmap, cmap_bytes, cmap_start, cmap_count, alpha, premultiplied;
} TgaFile;

static int le16(const unsigned char *p) { return p[0] | p[1] << 8; }

static int tga_open(TgaFile *f, const unsigned char *b, int length) {
  zero(f, sizeof *f);
  if (length < 18) return TEX_ERROR_DATA;
  f->type = b[2], f->kind = b[2] & 7;
  if (f->type > 11 || f->kind < 1 || f->kind > 3) return TEX_ERROR_COMPRESSION;
  f->width = le16(b + 12), f->height = le16(b + 14);
  f->n = (b[16] + 7) / 8, f->desc = b[17];
  f->cmap_start = le16(b + 3), f->cmap_bytes = (b[7] + 7) / 8;
  f->cmap = 18 + b[0];
  f->cmap_count = b[1] ? le16(b + 5) : 0;
  f->pos = f->cmap + f->cmap_count * f->cmap_bytes;
  if (!f->width || !f->height || f->n < 1 || f->n > 4 || f->pos > length) return TEX_ERROR_DATA;
  if (f->kind == 1 && (!b[1] || f->n > 2 || f->cmap_bytes < 2 || f->cmap_bytes > 4)) return TEX_ERROR_DATA;
  int bytes = f->kind == 1 ? f->cmap_bytes : f->n;
  f->alpha = (f->desc & 15) > 0 || bytes == 4 || (f->kind == 3 && bytes == 2);
  if (length >= 26) {
    const unsigned char *sig = b + length - 18;
    static const char SIGNATURE[18] = "TRUEVISION-XFILE.";
    int match = 1;
    for (int i = 0; i < 18; i++) match &= sig[i] == (unsigned char)SIGNATURE[i];
    int ext = (int)(b[length - 26] | b[length - 25] << 8 | b[length - 24] << 16 | (unsigned)b[length - 23] << 24);
    if (match && ext > 0 && ext <= length - 495) {
      int a = b[ext + 494];
      f->alpha = a == 3 || a == 4;
      f->premultiplied = a == 4;
    }
  }
  return TEX_OK;
}

int tex_tga_info(const unsigned char *bytes, int length, tex_info *info) {
  TgaFile f;
  int r = tga_open(&f, bytes, length);
  zero(info, sizeof *info);
  info->compression = f.type;
  if (r) return r;
  info->width = f.width, info->height = f.height, info->bits = 8;
  info->scratch = f.width * f.height * f.n;
  return TEX_OK;
}

int tex_tga_read(const unsigned char *bytes, int length, float *out, void *scratch) {
  TgaFile f;
  int r = tga_open(&f, bytes, length);
  if (r) return r;
  tga_decode(bytes, length, f.pos, f.type & 8 ? 1 : 0, scratch, f.width, f.height, f.n, f.kind, f.desc, bytes + f.cmap, f.cmap_bytes, f.cmap_start, f.cmap_count, f.alpha, f.premultiplied, out);
  return TEX_OK;
}
