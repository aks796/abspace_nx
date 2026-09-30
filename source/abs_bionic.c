/* abs_bionic.c -- the bionic imports Angry Birds Space needs that the shared
 * runtime (from the Crossy Road and PvZ ports) did not: reader/writer locks,
 * condition attributes and the old bionic's monotonic timed wait, readdir_r,
 * memrchr, dup and getauxval. Built on the existing shims, so they share the
 * same mutex/condvar/fd models. MIT.
 */
#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "bionic.h"
#include "bionic_pthread.h"
#include "util.h"

int b_pthread_mutex_lock(b_pthread_mutex_t *pm);
int b_pthread_mutex_unlock(b_pthread_mutex_t *pm);
int b_pthread_cond_wait(b_pthread_cond_t *c, b_pthread_mutex_t *m);
int b_pthread_cond_broadcast(b_pthread_cond_t *c);
int b_pthread_cond_timedwait_relative_np(b_pthread_cond_t *c, b_pthread_mutex_t *m,
                                         const struct b_timespec *rel);
int b_clock_gettime(int clk, struct b_timespec *ts);
struct b_dirent *b_readdir(void *h);
int b_fcntl(int fd, int cmd, ...);

/* ======================== pthread_condattr_* (a long) ======================= */
int b_pthread_condattr_init(int32_t *a) {
  if (a)
    *a = 0;
  return 0;
}
int b_pthread_condattr_destroy(int32_t *a) { return 0; }

/* Android before 4.3's pthread_cond_timedwait_monotonic: an ABSOLUTE time on
 * CLOCK_MONOTONIC. */
int b_pthread_cond_timedwait_monotonic(b_pthread_cond_t *c, b_pthread_mutex_t *m,
                                       const struct b_timespec *abstime) {
  struct b_timespec now, rel = {0, 0};
  b_clock_gettime(L_CLOCK_MONOTONIC, &now);
  if (abstime) {
    int64_t d = ((int64_t)abstime->tv_sec - now.tv_sec) * 1000000000ll +
                ((int64_t)abstime->tv_nsec - now.tv_nsec);
    if (d < 0)
      d = 0;
    rel.tv_sec = (int32_t)(d / 1000000000ll);
    rel.tv_nsec = (int32_t)(d % 1000000000ll);
  }
  return b_pthread_cond_timedwait_relative_np(c, m, &rel);
}

/* ============================ pthread_rwlock_* ============================
 * Old bionic's pthread_rwlock_t is 40 bytes, a mutex, a condvar and counters;
 * PTHREAD_RWLOCK_INITIALIZER is all zeros, which is also a valid state here.
 * No writer preference: a thread that holds a read lock and takes it again
 * while a writer waits must not deadlock (the engine's resource locks do). */
typedef struct {
  b_pthread_mutex_t lock;
  b_pthread_cond_t cond;
  int32_t readers;   /* read locks held */
  int32_t writer;    /* thread tag of the writer, 0 = none */
  int32_t unused[6];
} BRwlock;
BIONIC_STATIC_ASSERT(sizeof(BRwlock) == 40, "pthread_rwlock_t");

static int32_t self_tag(void) { return (int32_t)(uintptr_t)b_thread_self(); }

int b_pthread_rwlock_init(BRwlock *rw, const void *attr) {
  if (!rw)
    return L_EINVAL;
  memset(rw, 0, sizeof *rw);
  return 0;
}

int b_pthread_rwlock_destroy(BRwlock *rw) { return 0; }

int b_pthread_rwlock_rdlock(BRwlock *rw) {
  b_pthread_mutex_lock(&rw->lock);
  while (rw->writer && rw->writer != self_tag())
    b_pthread_cond_wait(&rw->cond, &rw->lock);
  rw->readers++;
  b_pthread_mutex_unlock(&rw->lock);
  return 0;
}

int b_pthread_rwlock_wrlock(BRwlock *rw) {
  b_pthread_mutex_lock(&rw->lock);
  while (rw->writer || rw->readers)
    b_pthread_cond_wait(&rw->cond, &rw->lock);
  rw->writer = self_tag();
  b_pthread_mutex_unlock(&rw->lock);
  return 0;
}

int b_pthread_rwlock_tryrdlock(BRwlock *rw) {
  b_pthread_mutex_lock(&rw->lock);
  int ok = !rw->writer;
  if (ok)
    rw->readers++;
  b_pthread_mutex_unlock(&rw->lock);
  return ok ? 0 : L_EBUSY;
}

int b_pthread_rwlock_trywrlock(BRwlock *rw) {
  b_pthread_mutex_lock(&rw->lock);
  int ok = !rw->writer && !rw->readers;
  if (ok)
    rw->writer = self_tag();
  b_pthread_mutex_unlock(&rw->lock);
  return ok ? 0 : L_EBUSY;
}

int b_pthread_rwlock_unlock(BRwlock *rw) {
  b_pthread_mutex_lock(&rw->lock);
  if (rw->writer == self_tag())
    rw->writer = 0;
  else if (rw->readers > 0)
    rw->readers--;
  b_pthread_cond_broadcast(&rw->cond);
  b_pthread_mutex_unlock(&rw->lock);
  return 0;
}

/* ================================ readdir_r ================================ */
int b_readdir_r(void *dir, struct b_dirent *entry, struct b_dirent **result) {
  struct b_dirent *e = b_readdir(dir);
  if (!e) {
    if (result)
      *result = NULL;
    return 0;
  }
  if (entry)
    memcpy(entry, e, sizeof *entry);
  if (result)
    *result = entry;
  return 0;
}

/* ================================== misc =================================== */
void *b_memrchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s + n;
  while (n--)
    if (*--p == (unsigned char)c)
      return (void *)p;
  return NULL;
}

int b_dup(int fd) { return b_fcntl(fd, L_F_DUPFD, 0); }

/* BoringSSL's CPU capability probe (AT_HWCAP / AT_HWCAP2): a Cortex-A57 in
 * AArch32 state -- NEON, VFPv4, idiv; AES, PMULL, SHA1, SHA2, CRC32. The same
 * values the synthetic /proc/self/auxv carries (bionic_io.c). */
unsigned long b_getauxval(unsigned long type) {
  switch (type) {
  case 16: return 0x003FB0D6ul; /* AT_HWCAP */
  case 26: return 0x1Ful;       /* AT_HWCAP2 */
  case 6: return 0x1000ul;      /* AT_PAGESZ */
  default: return 0;
  }
}
