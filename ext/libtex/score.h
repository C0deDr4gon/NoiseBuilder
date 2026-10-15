#pragma once

#include "platform.h"
#include "vec.h"
#include "codec.h"

void tex_score(const float *a, const float *b, int pixels, int channels, int hdr, const tex_options *o, double *out) {
  if (!o) o = &TEX_DEFAULT;
  ivec lanes = mask(channels);
  dvec sum = dsplat(0);
  vec peak = splat(0);
  for (int i = 0; i < pixels; i++) {
    vec x = load(a + i * 4), y = load(b + i * 4);
    if (hdr) {
      x = log2_1p(x);
      y = log2_1p(y);
      peak = vmax(peak, keep(vabs(x), lanes));
    }
    vec d = keep(sub(x, y), lanes);
    d = mul(mul(d, d), tint(o));
    sum = dadd(sum, dadd(dlow(d), dhigh(d)));
  }
  out[0] = dlane(sum, 0) + dlane(sum, 1);
  vec top = vmax(peak, vshuffle(peak, 2, 3, 0, 1));
  float p = hdr ? lane(top, 0) : 1;
  float q = hdr ? lane(top, 1) : 1;
  out[1] = p > q ? p : q;
}
