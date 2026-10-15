#pragma once

#include "codec.h"

static void none_encode(const float *px, unsigned char *out, const tex_options *o) {
  copy(out, px, 256);
}

static void none_decode(const unsigned char *in, float *px) {
  copy(px, in, 256);
}

static const Codec NONE = { none_encode, none_decode, 256 };
