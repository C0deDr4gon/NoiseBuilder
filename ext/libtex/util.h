#pragma once

#include "platform.h"
#include "vec.h"

int tex_to_bytes(const float *in, int count, unsigned char *out) {
  int i = 0;
  ivec same = isplat(-1);
  for (; i + 16 <= count; i += 16) {
    ivec q[4];
    for (int k = 0; k < 4; k++) {
      vec v = load(in + i + k * 4);
      q[k] = vtrunc(add(mul(vmin(vmax(v, splat(0)), splat(1)), splat(255)), splat(0.5f)));
      same = iand(same, ieq(bits(v), bits(vdiv(tofloat(q[k]), splat(255)))));
    }
    istore(out + i, narrow(q[0], q[1], q[2], q[3]));
  }
  int exact = all(same);
  for (; i < count; i++) {
    out[i] = (int)(clamp(in[i], 0, 1) * 255 + 0.5f);
    float back = out[i] / 255.0f;
    unsigned a, b;
    copy(&a, in + i, 4);
    copy(&b, &back, 4);
    exact &= a == b;
  }
  return exact;
}

void tex_from_bytes(const unsigned char *in, int count, float *out) {
  int i = 0;
  for (; i + 4 <= count; i += 4) store(out + i, vdiv(tofloatu(bytes4(iload4(in + i))), splat(255)));
  for (; i < count; i++) out[i] = in[i] / 255.0f;
}

void tex_premultiply(float *px, int count, int inverse) {
  ivec rgb = imake(-1, -1, -1, 0);
  for (int i = 0; i < count; i++) {
    vec v = load(px + i * 4), a = splat(lane(v, 3));
    vec out = inverse ? vdiv(v, a) : mul(v, a);
    ivec m = inverse ? iand(rgb, ne(a, splat(0))) : rgb;
    store(px + i * 4, vselect(m, out, v));
  }
}

void tex_swizzle(float *px, int count, const int *src, const float *scale, const float *offset) {
  unsigned char bytes[16];
  int fixed[4];
  for (int k = 0; k < 4; k++) {
    int s = src[k] < 0 ? 0 : src[k];
    for (int b = 0; b < 4; b++) bytes[k * 4 + b] = s * 4 + b;
    fixed[k] = src[k] < 0 ? -1 : 0;
  }
  ivec pick = iload(bytes), fix = iload(fixed);
  vec m = load(scale), o = load(offset);
  for (int i = 0; i < count; i++) {
    vec v = add(mul(vswizzle(load(px + i * 4), pick), m), o);
    store(px + i * 4, vselect(fix, o, v));
  }
}

void tex_orient(const float *src, int w, int h, float *dst, int op) {
  int ow = op == 2 || op == 4 ? h : w, oh = op == 2 || op == 4 ? w : h;
  for (int y = 0; y < oh; y++) {
    for (int x = 0; x < ow; x++) {
      int sx = op == 0 || op == 3 ? w - 1 - x : op == 2 ? y : op == 4 ? w - 1 - y : x;
      int sy = op == 1 || op == 3 ? h - 1 - y : op == 2 ? h - 1 - x : op == 4 ? x : y;
      store(dst + (y * ow + x) * 4, load(src + (sy * w + sx) * 4));
    }
  }
}
