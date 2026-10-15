#pragma once


#include "platform.h"

static double pow2(int e) {
  unsigned long long bits = (unsigned long long)(e + 1023) << 52;
  double v;
  copy(&v, &bits, 8);
  return v;
}

static int exponent(double v) {
  unsigned long long bits;
  copy(&bits, &v, 8);
  return (int)(bits >> 52 & 0x7ff) - 1023;
}

static int half(double v) {
  if (!(v > 0)) return 0;
  int e = exponent(v);
  if (e < -14) return (int)fround(v / pow2(-24));
  if (e > 15) return 0x7bff;
  int h = (e + 15) * 1024 + (int)fround((v / pow2(e) - 1) * 1024);
  return h < 0x7bff ? h : 0x7bff;
}

static double unhalf(int h) {
  int e = h >> 10;
  int m = h & 1023;
  if (e == 31) return m ? fnan() : finf();
  return e ? pow2(e - 15) * (1 + m / 1024.0) : m * pow2(-24);
}

static float HALF[65536];

static void half_init(void) {
  for (int h = 0; h < 65536; h++) HALF[h] = h & 0x8000 ? -unhalf(h & 0x7fff) : unhalf(h);
}

static int to_half(double v, int sign) {
  return sign && v < 0 ? -half(-v) : half(v);
}

static double from_half(int h) {
  return h < 0 ? -unhalf(-h) : unhalf(h);
}
