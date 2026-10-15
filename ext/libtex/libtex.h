#ifndef LIBTEX_H
#define LIBTEX_H

typedef enum { TEX_NONE, TEX_BC1, TEX_BC2, TEX_BC3, TEX_BC4, TEX_BC4_SNORM, TEX_BC5, TEX_BC5_SNORM, TEX_BC6H_UF16, TEX_BC6H_SF16, TEX_BC7 } tex_format;
typedef enum { TEX_BEST, TEX_FAST } tex_quality;
typedef struct { tex_quality quality; float weight[3]; } tex_options;

typedef enum { TEX_OK, TEX_ERROR_FORMAT, TEX_ERROR_DATA, TEX_ERROR_COMPRESSION, TEX_ERROR_DEEP, TEX_ERROR_SUBSAMPLED } tex_error;
typedef struct { int width, height, bits, compression, scratch; } tex_info;

void tex_init(void);

int tex_block_size(tex_format id);
int tex_encode(tex_format id, const tex_options *o, const float *src, int width, int height, unsigned char *out, float *dst);
void tex_decode(tex_format id, const unsigned char *in, int width, int height, float *dst);
void tex_decode_blocks(tex_format id, const unsigned char *in, int count, float *out);
void tex_score(const float *a, const float *b, int pixels, int channels, int hdr, const tex_options *o, double *out);

int tex_mips_scratch(int width, int height);
void tex_mips(const float *src, int width, int height, float *out, void *scratch);
void tex_premultiply(float *px, int count, int inverse);
void tex_swizzle(float *px, int count, const int *src, const float *scale, const float *offset);
void tex_orient(const float *src, int w, int h, float *dst, int op);
int tex_resize_scratch(int w, int h, int nw, int nh, int filter);
void tex_resize(const float *src, int w, int h, float *dst, int nw, int nh, int filter, void *scratch);
int tex_to_bytes(const float *in, int count, unsigned char *out);
void tex_from_bytes(const unsigned char *in, int count, float *out);

int tex_png_info(const unsigned char *bytes, int length, tex_info *info);
int tex_png_read(const unsigned char *bytes, int length, float *out, void *scratch);
void tex_png_rows(const float *px, int w, int h, int n, int depth, int mode, unsigned char *out, unsigned char *scratch);
int tex_tga_info(const unsigned char *bytes, int length, tex_info *info);
int tex_tga_read(const unsigned char *bytes, int length, float *out, void *scratch);
int tex_tga_gray(const float *px, int count);
int tex_tga_header(int w, int h, int n, int rle, unsigned char *out);
int tex_tga_pixels(const float *px, int w, int h, int n, int rle, unsigned char *out, unsigned char *row);
int tex_exr_info(const unsigned char *bytes, int length, tex_info *info);
int tex_exr_read(const unsigned char *bytes, int length, float *out, void *scratch);
void tex_half_plane(const float *px, int count, int k, unsigned short *out);

#endif

#if defined(LIBTEX_IMPLEMENTATION) && !defined(LIBTEX_DONE)
#define LIBTEX_DONE
#include "roundtrip.h"
#include "score.h"
#include "mip.h"
#include "util.h"
#include "resize.h"
#include "png.h"
#include "tga.h"
#include "exr.h"
#include "init.h"
#endif
