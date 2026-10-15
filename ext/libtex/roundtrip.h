#pragma once

#include "platform.h"
#include "codec.h"
#include "none.h"
#include "bc.h"
#include "bc6henc.h"
#include "bc7enc.h"

static const Codec *CODECS[] = { &NONE, &BC1, &BC2, &BC3, &BC4, &BC4_SNORM, &BC5, &BC5_SNORM, &BC6H_UF16, &BC6H_SF16, &BC7 };

static void load_block(const float *src, int width, int height, int bx, int by, float *px) {
  for (int i = 0; i < 16; i++) {
    int x = bx * 4 + (i & 3);
    int y = by * 4 + (i >> 2);
    if (x > width - 1) x = width - 1;
    if (y > height - 1) y = height - 1;
    for (int k = 0; k < 4; k++) px[i * 4 + k] = src[(y * width + x) * 4 + k];
  }
}

static void store_block(float *dst, int width, int height, int bx, int by, const float *px) {
  for (int i = 0; i < 16; i++) {
    int x = bx * 4 + (i & 3);
    int y = by * 4 + (i >> 2);
    if (x < width && y < height) for (int k = 0; k < 4; k++) dst[(y * width + x) * 4 + k] = px[i * 4 + k];
  }
}

int tex_encode(tex_format id, const tex_options *o, const float *src, int width, int height, unsigned char *out, float *dst) {
  if (!o) o = &TEX_DEFAULT;
  const Codec *codec = CODECS[id];
  float px[64];
  unsigned char bytes[256];
  int bw = (width + 3) / 4;
  int bh = (height + 3) / 4;
  for (int by = 0; by < bh; by++) {
    for (int bx = 0; bx < bw; bx++) {
      load_block(src, width, height, bx, by, px);
      zero(bytes, 16);
      codec->encode(px, bytes, o);
      codec->decode(bytes, px);
      if (out) copy(out + (by * bw + bx) * codec->size, bytes, codec->size);
      store_block(dst, width, height, bx, by, px);
    }
  }
  return bw * bh;
}

void tex_decode_blocks(tex_format id, const unsigned char *in, int count, float *out) {
  const Codec *codec = CODECS[id];
  for (int b = 0; b < count; b++) codec->decode(in + b * codec->size, out + b * 64);
}

void tex_decode(tex_format id, const unsigned char *in, int width, int height, float *dst) {
  const Codec *codec = CODECS[id];
  float px[64];
  int bw = (width + 3) / 4;
  for (int by = 0; by < (height + 3) / 4; by++) {
    for (int bx = 0; bx < bw; bx++) {
      codec->decode(in + (by * bw + bx) * codec->size, px);
      store_block(dst, width, height, bx, by, px);
    }
  }
}

int tex_block_size(tex_format id) { return CODECS[id]->size; }
