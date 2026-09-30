/* host_shim/switch.h -- the few libnx calls abs_video.c makes, on a PC
 * (pthreads, the monotonic clock), for tools/test_video.c. MIT. */
#ifndef ABS_HOST_SWITCH_H
#define ABS_HOST_SWITCH_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 Result;
#define R_FAILED(r) ((r) != 0)
#define R_SUCCEEDED(r) ((r) == 0)
#define BIT(n) (1u << (n))

typedef struct VirtmemReservation VirtmemReservation;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t CondVar;
static inline void mutexInit(Mutex *m) { pthread_mutex_init(m, NULL); }
static inline void mutexLock(Mutex *m) { pthread_mutex_lock(m); }
static inline void mutexUnlock(Mutex *m) { pthread_mutex_unlock(m); }
static inline void condvarInit(CondVar *c) { pthread_cond_init(c, NULL); }
static inline void condvarWakeAll(CondVar *c) { pthread_cond_broadcast(c); }
static inline Result condvarWaitTimeout(CondVar *c, Mutex *m, u64 ns) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += (time_t)(ns / 1000000000u);
  ts.tv_nsec += (long)(ns % 1000000000u);
  if (ts.tv_nsec >= 1000000000L)
    ts.tv_sec++, ts.tv_nsec -= 1000000000L;
  return (Result)pthread_cond_timedwait(c, m, &ts);
}

typedef struct {
  pthread_t t;
  void (*fn)(void *);
  void *arg;
} Thread;
static void *abs_host_thread(void *p) {
  Thread *t = p;
  t->fn(t->arg);
  return NULL;
}
static inline Result threadCreate(Thread *t, void (*fn)(void *), void *arg, void *stack, size_t sz, int prio, int core) {
  (void)stack, (void)sz, (void)prio, (void)core;
  t->fn = fn, t->arg = arg;
  return 0;
}
static inline Result threadStart(Thread *t) { return (Result)pthread_create(&t->t, NULL, abs_host_thread, t); }
static inline Result threadWaitForExit(Thread *t) { return (Result)pthread_join(t->t, NULL); }
static inline Result threadClose(Thread *t) { (void)t; return 0; }
static inline void svcSleepThread(int64_t ns) {
  struct timespec ts = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
  nanosleep(&ts, NULL);
}
/* the Switch's 19.2 MHz tick */
static inline u64 armTicksToNs(u64 t) { return t * 625 / 12; }
static inline u64 armNsToTicks(u64 ns) { return ns * 12 / 625; }
static inline u64 armGetSystemTick(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return armNsToTicks((u64)ts.tv_sec * 1000000000u + (u64)ts.tv_nsec);
}

enum {
  HidNpadButton_A = BIT(0), HidNpadButton_B = BIT(1), HidNpadButton_X = BIT(2), HidNpadButton_Y = BIT(3),
  HidNpadButton_StickL = BIT(4), HidNpadButton_StickR = BIT(5), HidNpadButton_L = BIT(6), HidNpadButton_R = BIT(7),
  HidNpadButton_ZL = BIT(8), HidNpadButton_ZR = BIT(9), HidNpadButton_Plus = BIT(10), HidNpadButton_Minus = BIT(11),
  HidNpadButton_Left = BIT(12), HidNpadButton_Up = BIT(13), HidNpadButton_Right = BIT(14), HidNpadButton_Down = BIT(15),
};
#endif
