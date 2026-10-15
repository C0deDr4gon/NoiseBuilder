#pragma once

#include "platform.h"
#include "fit.h"
#include "bc7tables.h"

static ivec ONE[2][16][16], TWO[16][16];

static void partition_init(void) {
  for (int g = 0; g < 16; g++) {
    for (int i = 0; i < 16; i++) {
      const unsigned char *a = &P2[g * 4][i], *b = &P3[g * 4][i];
      ONE[0][g][i] = imake(-(a[0] == 1), -(a[16] == 1), -(a[32] == 1), -(a[48] == 1));
      ONE[1][g][i] = imake(-(b[0] == 1), -(b[16] == 1), -(b[32] == 1), -(b[48] == 1));
      TWO[g][i] = imake(-(b[0] == 2), -(b[16] == 2), -(b[32] == 2), -(b[48] == 2));
    }
  }
}

static vec part(const vec *s, vec n) {
  vec d = add(add(mul(s[0], s[0]), mul(s[1], s[1])), add(mul(s[2], s[2]), mul(s[3], s[3])));
  return vselect(gt(n, splat(0)), vdiv(d, n), splat(0));
}

static void spreads(const vec *px, int ns, int count, float *out) {
  ALIGN16 float p[64], c[4][16];
  vec total[4];
  for (int i = 0; i < 16; i++) store(p + i * 4, px[i]);
  for (int k = 0; k < 4; k++) {
    float t = 0;
    for (int i = 0; i < 16; i++) t += c[k][i] = p[i * 4 + k];
    total[k] = splat(t);
  }
  for (int g = 0; g < count / 4; g++) {
    vec s1[4] = { splat(0), splat(0), splat(0), splat(0) }, s2[4] = { splat(0), splat(0), splat(0), splat(0) }, s0[4];
    vec n1 = splat(0), n2 = splat(0);
    for (int i = 0; i < 16; i++) {
      ivec m = ONE[ns - 2][g][i];
      for (int k = 0; k < 4; k++) s1[k] = add(s1[k], keep(splat(c[k][i]), m));
      n1 = add(n1, keep(splat(1), m));
      if (ns < 3) continue;
      m = TWO[g][i];
      for (int k = 0; k < 4; k++) s2[k] = add(s2[k], keep(splat(c[k][i]), m));
      n2 = add(n2, keep(splat(1), m));
    }
    for (int k = 0; k < 4; k++) s0[k] = sub(sub(total[k], s1[k]), s2[k]);
    vec n0 = sub(sub(splat(16), n1), n2);
    vec score = add(add(part(s0, n0), part(s1, n1)), part(s2, n2));
    store(out + g * 4, vneg(score));
  }
}
