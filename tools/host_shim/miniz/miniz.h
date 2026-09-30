/* tools/host_shim/miniz/miniz.h -- the two miniz calls source/abs_png.c
 * makes, over the system zlib, so the PC tests (tools/orbital, test_orbital)
 * can decode PNGs (libnx32's miniz is a prebuilt ARM library). Not miniz:
 * just enough of its interface. MIT. */
#ifndef HOST_MINIZ_H
#define HOST_MINIZ_H
#include <stdlib.h>
#include <zlib.h>

#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
static inline void mz_free(void *p) { free(p); }
static inline void *tinfl_decompress_mem_to_heap(const void *src, size_t src_len, size_t *out_len, int flags) {
  (void)flags;
  size_t cap = src_len * 4 + 1024;
  for (int tries = 0; tries < 8; tries++, cap *= 4) {
    unsigned char *out = malloc(cap);
    if (!out)
      return NULL;
    uLongf n = (uLongf)cap;
    int r = uncompress(out, &n, (const Bytef *)src, (uLong)src_len);
    if (r == Z_OK) {
      *out_len = (size_t)n;
      return out;
    }
    free(out);
    if (r != Z_BUF_ERROR)
      return NULL;
  }
  return NULL;
}
#endif
