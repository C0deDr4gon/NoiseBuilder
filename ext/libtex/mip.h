#pragma once

#include "platform.h"
#include "vec.h"

typedef struct {
  int at[3];
  float w[3];
  int n;
} Taps;

static void taps(int src, int dst, Taps *out) {
  double r = (double)src / dst;
  for (int x = 0; x < dst; x++) {
    double a = x * r, b = a + r;
    int end = (int)fceil(b);
    if (end > src) end = src;
    out[x].n = 0;
    for (int i = (int)ffloor(a); i < end && out[x].n < 3; i++) {
      double lo = a > i ? a : i, hi = b < i + 1 ? b : i + 1;
      out[x].at[out[x].n] = i;
      out[x].w[out[x].n++] = (hi - lo) / r;
    }
  }
}

static void halve(const float *src, int width, int height, float *out, int w, int h, Taps *xs) {
  Taps *ys = xs + w;
  taps(width, w, xs);
  taps(height, h, ys);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      vec weighted = splat(0), plain = splat(0);
      for (int j = 0; j < ys[y].n; j++) {
        const float *row = src + ys[y].at[j] * width * 4;
        for (int i = 0; i < xs[x].n; i++) {
          vec p = load(row + xs[x].at[i] * 4);
          float weight = xs[x].w[i] * ys[y].w[j];
          weighted = add(weighted, mul(with(p, 3, 1), splat(lane(p, 3) * weight)));
          plain = add(plain, mul(p, splat(weight)));
        }
      }
      float a = lane(weighted, 3);
      vec c = a > 0 ? vdiv(weighted, splat(a)) : plain;
      store(out + (y * w + x) * 4, with(c, 3, a));
    }
  }
}

int tex_mips_scratch(int width, int height) { return ((width > 1 ? width >> 1 : 1) + (height > 1 ? height >> 1 : 1)) * sizeof(Taps); }

void tex_mips(const float *src, int width, int height, float *out, void *scratch) {
  while (width > 1 || height > 1) {
    int w = width > 1 ? width >> 1 : 1, h = height > 1 ? height >> 1 : 1;
    halve(src, width, height, out, w, h, scratch);
    src = out;
    out += w * h * 4;
    width = w;
    height = h;
  }
}
