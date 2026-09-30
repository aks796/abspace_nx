/* host_shim/malloc.h -- memalign on a PC. */
#ifndef ABS_HOST_MALLOC_H
#define ABS_HOST_MALLOC_H
#include <stdlib.h>
static inline void *memalign(size_t a, size_t n) {
  void *p = NULL;
  return posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) ? NULL : p;
}
#endif
