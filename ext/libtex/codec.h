#pragma once

#include "vec.h"
#include "libtex.h"

typedef struct {
  void (*encode)(const float *px, unsigned char *out, const tex_options *o);
  void (*decode)(const unsigned char *in, float *px);
  int size;
} Codec;

static const tex_options TEX_DEFAULT = { TEX_BEST, { 1, 1, 1 } };

static inline int is_fast(const tex_options *o) { return o->quality == TEX_FAST; }

static inline vec tint(const tex_options *o) { return vmake(o->weight[0], o->weight[1], o->weight[2], 1); }
