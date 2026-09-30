/* abs_png.c -- PNG files into RGBA8: the hand cursor's pictures (built into
 * the program, abs_cursor.c) and the Orbital Escapade mod's.
 *
 * 8-bit greyscale, grey+alpha, RGB, RGBA and palette images (with tRNS), 16-bit
 * ones reduced to 8, not interlaced: what image editors write. The IDAT
 * stream is inflated with miniz (already in the build, abs_assets.c) and the
 * five PNG filters are undone. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <miniz/miniz.h>

#include "abs.h"

static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c) {
  int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  if (pa <= pb && pa <= pc)
    return a;
  return pb <= pc ? b : c;
}

uint8_t *abs_png_decode(const uint8_t *data, size_t len, int *out_w, int *out_h) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (!data || len < 33 || memcmp(data, sig, 8))
    return NULL;
  uint32_t w = 0, h = 0;
  int depth = 0, ctype = 0, interlace = 0;
  uint8_t pal[256][4];
  int npal = 0;
  for (int i = 0; i < 256; i++)
    pal[i][0] = pal[i][1] = pal[i][2] = 0, pal[i][3] = 255;
  int trns_grey = -1, trns_rgb[3] = {-1, -1, -1};

  /* the chunks: IHDR, PLTE, tRNS, the IDATs glued together */
  uint8_t *idat = NULL;
  size_t idat_len = 0, idat_cap = 0;
  size_t pos = 8;
  while (pos + 12 <= len) {
    uint32_t n = be32(data + pos);
    const uint8_t *type = data + pos + 4, *body = data + pos + 8;
    if (n > len - pos - 12)
      break;
    if (!memcmp(type, "IHDR", 4) && n >= 13) {
      w = be32(body), h = be32(body + 4);
      depth = body[8], ctype = body[9], interlace = body[12];
    } else if (!memcmp(type, "PLTE", 4)) {
      npal = (int)(n / 3);
      if (npal > 256)
        npal = 256;
      for (int i = 0; i < npal; i++)
        pal[i][0] = body[3 * i], pal[i][1] = body[3 * i + 1], pal[i][2] = body[3 * i + 2];
    } else if (!memcmp(type, "tRNS", 4)) {
      if (ctype == 3)
        for (uint32_t i = 0; i < n && i < 256; i++)
          pal[i][3] = body[i];
      else if (ctype == 0 && n >= 2)
        trns_grey = body[0] << 8 | body[1];
      else if (ctype == 2 && n >= 6)
        for (int c = 0; c < 3; c++)
          trns_rgb[c] = body[2 * c] << 8 | body[2 * c + 1];
    } else if (!memcmp(type, "IDAT", 4)) {
      if (idat_len + n > idat_cap) {
        size_t cap = (idat_len + n) * 2;
        uint8_t *p = realloc(idat, cap);
        if (!p) {
          free(idat);
          return NULL;
        }
        idat = p, idat_cap = cap;
      }
      memcpy(idat + idat_len, body, n);
      idat_len += n;
    } else if (!memcmp(type, "IEND", 4)) {
      break;
    }
    pos += 12 + n;
  }
  /* channels per pixel, as stored */
  int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
  if (!idat || !w || !h || w > 4096 || h > 4096 || !ch || interlace || (depth != 8 && depth != 16) ||
      (ctype == 3 && depth != 8)) {
    free(idat);
    return NULL;
  }
  const int bpp = ch * depth / 8;          /* bytes per pixel */
  const size_t row = (size_t)w * bpp;      /* bytes per row, without the filter byte */
  size_t raw_len = 0;
  uint8_t *raw = tinfl_decompress_mem_to_heap(idat, idat_len, &raw_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
  free(idat);
  if (!raw || raw_len < (row + 1) * h) {
    mz_free(raw);
    return NULL;
  }
  /* undo the filters in place: each row starts with its filter's number */
  for (uint32_t y = 0; y < h; y++) {
    uint8_t *cur = raw + y * (row + 1) + 1;
    const uint8_t *prev = y ? raw + (y - 1) * (row + 1) + 1 : NULL;
    int f = cur[-1];
    for (size_t x = 0; x < row; x++) {
      int a = x >= (size_t)bpp ? cur[x - bpp] : 0;
      int b = prev ? prev[x] : 0;
      int c = prev && x >= (size_t)bpp ? prev[x - bpp] : 0;
      switch (f) {
      case 1: cur[x] = (uint8_t)(cur[x] + a); break;
      case 2: cur[x] = (uint8_t)(cur[x] + b); break;
      case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
      case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
      default: break;
      }
    }
  }
  uint8_t *out = malloc((size_t)w * h * 4);
  if (!out) {
    mz_free(raw);
    return NULL;
  }
  const int step = depth / 8; /* 16-bit samples: the high byte */
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t *s = raw + y * (row + 1) + 1;
    uint8_t *d = out + (size_t)y * w * 4;
    for (uint32_t x = 0; x < w; x++, s += bpp, d += 4) {
      switch (ctype) {
      case 0: {
        int g = s[0];
        int full = step == 2 ? (s[0] << 8 | s[1]) : s[0];
        d[0] = d[1] = d[2] = (uint8_t)g;
        d[3] = full == trns_grey ? 0 : 255;
        break;
      }
      case 2: {
        d[0] = s[0], d[1] = s[step], d[2] = s[2 * step];
        int r = step == 2 ? (s[0] << 8 | s[1]) : s[0];
        int g = step == 2 ? (s[2] << 8 | s[3]) : s[1];
        int b = step == 2 ? (s[4] << 8 | s[5]) : s[2];
        d[3] = (r == trns_rgb[0] && g == trns_rgb[1] && b == trns_rgb[2]) ? 0 : 255;
        break;
      }
      case 3:
        memcpy(d, pal[s[0]], 4);
        break;
      case 4:
        d[0] = d[1] = d[2] = s[0], d[3] = s[step];
        break;
      default: /* 6 */
        d[0] = s[0], d[1] = s[step], d[2] = s[2 * step], d[3] = s[3 * step];
        break;
      }
    }
  }
  mz_free(raw);
  *out_w = (int)w, *out_h = (int)h;
  return out;
}
