/* abs_audio.c -- com.rovio.fusion.AudioOutput, played through audout.
 *
 * Fusion mixes its own sound (mpg123 for the MP3 music and effects, its
 * audio::AudioMixer): the Java AudioOutput is only a sink. The engine creates
 * it through JNI -- new AudioOutput(mixer handle, rate, channels, bits,
 * buffer bytes) -- calls startOutput(), and from then on the Java's AudioTrack
 * thread PULLS: nativeMixData(handle, byte[], n) fills the array with the next
 * n bytes of mixed PCM, which is written to the AudioTrack.
 *
 * Here an audio thread does the pulling: it asks the mixer for 512 frames at
 * a time, resamples them to audout's 48 kHz stereo s16 when the mixer runs
 * at another rate (the game's is 16 kHz mono: a 4-point Hermite curve through
 * the samples, continuous across pulls, which keeps the highs cleaner than a
 * straight line), and queues 1024-frame buffers; it blocks while three are
 * queued (~64 ms ahead), which paces the mixer exactly as a blocking
 * AudioTrack.write does. While a video of the port's plays (abs_video.c), its
 * sound goes out instead of the game's, which is still pulled.
 *
 * audout's buffer descriptor is an IPC structure with 64-bit fields for every
 * client, while libnx32's AudioOutBuffer has 32-bit pointers, so append and
 * get-released are issued with the right layout here (from the Crossy Road
 * and PvZ ports, where the self-test below proved it on hardware). MIT.
 */
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "util.h"

typedef struct {
  u64 next, buffer, buffer_size, data_size, data_offset;
} AoBuf;
_Static_assert(sizeof(AoBuf) == 0x28, "audout buffer descriptor");

#define NBUF 3
#define FRAMES_PER_BUF 1024 /* 1024 * 4 bytes = one 0x1000 page */
#define BUF_BYTES (FRAMES_PER_BUF * 4)
#define PULL_FRAMES 512

static AoBuf g_bufs[NBUF] __attribute__((aligned(16)));
static int16_t *g_pcm[NBUF];
static int g_queued[NBUF];
static int g_ao_ready;
static u32 g_out_rate = 48000;

static Result ao_append(AoBuf *b) {
  u64 tag = (u64)(uintptr_t)b;
  const bool auto_ = hosversionAtLeast(3, 0, 0);
  return serviceDispatchIn(audoutGetServiceSession_AudioOut(), auto_ ? 7 : 3, tag,
                           .buffer_attrs = {auto_ ? (SfBufferAttr_HipcAutoSelect | SfBufferAttr_In)
                                                  : (SfBufferAttr_HipcMapAlias | SfBufferAttr_In)},
                           .buffers = {{b, sizeof(*b)}});
}

static Result ao_released(u64 *tags, u32 max, u32 *count) {
  const bool auto_ = hosversionAtLeast(3, 0, 0);
  return serviceDispatchOut(audoutGetServiceSession_AudioOut(), auto_ ? 8 : 5, *count,
                            .buffer_attrs = {auto_ ? (SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out)
                                                   : (SfBufferAttr_HipcMapAlias | SfBufferAttr_Out)},
                            .buffers = {{tags, max * sizeof(u64)}});
}

static void reap(void) {
  u64 tags[NBUF] = {0};
  u32 n = 0;
  if (R_SUCCEEDED(ao_released(tags, NBUF, &n)))
    for (u32 k = 0; k < n && k < NBUF; k++)
      for (int i = 0; i < NBUF; i++)
        if (tags[k] == (u64)(uintptr_t)&g_bufs[i])
          g_queued[i] = 0;
}

static int free_buffer(void) {
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < NBUF; i++)
      if (!g_queued[i])
        return i;
    reap();
  }
  return -1;
}

static int ao_open(void) {
  if (g_ao_ready)
    return 0;
  Result rc = audoutInitialize();
  if (R_FAILED(rc)) {
    debugPrintf("[audio] audoutInitialize failed 0x%x\n", rc);
    return -1;
  }
  rc = audoutStartAudioOut();
  if (R_FAILED(rc)) {
    debugPrintf("[audio] audoutStartAudioOut failed 0x%x\n", rc);
    audoutExit();
    return -1;
  }
  g_out_rate = audoutGetSampleRate() ? audoutGetSampleRate() : 48000;
  for (int i = 0; i < NBUF; i++) {
    g_pcm[i] = memalign(0x1000, BUF_BYTES);
    if (!g_pcm[i])
      return -1;
    memset(g_pcm[i], 0, BUF_BYTES);
    g_bufs[i].buffer = (u64)(uintptr_t)g_pcm[i];
    g_bufs[i].buffer_size = BUF_BYTES;
    g_bufs[i].data_size = BUF_BYTES;
  }
  g_ao_ready = 1;
  debugPrintf("[audio] audout open: %u Hz, %u ch\n", (unsigned)g_out_rate,
              (unsigned)audoutGetChannelCount());
  return 0;
}

static unsigned long g_underruns, g_append_fails, g_dropped, g_submits;
static volatile int g_stop_thread;

/* Queue one full buffer of 48 kHz stereo s16; blocks while all are in use. */
static void submit(const int16_t *frames) {
  int i;
  reap();
  int queued = 0;
  for (int k = 0; k < NBUF; k++)
    queued += g_queued[k];
  if (!queued && g_submits > NBUF)
    g_underruns++;
  while ((i = free_buffer()) < 0 && !g_stop_thread)
    svcSleepThread(2000000ll);
  if (i < 0)
    return;
  memcpy(g_pcm[i], frames, BUF_BYTES);
  armDCacheFlush(g_pcm[i], BUF_BYTES);
  g_bufs[i].data_size = BUF_BYTES;
  g_bufs[i].data_offset = 0;
  for (int attempt = 0; attempt < 5; attempt++) {
    Result rc = ao_append(&g_bufs[i]);
    if (R_SUCCEEDED(rc)) {
      g_queued[i] = 1;
      g_submits++;
      return;
    }
    if (g_append_fails++ < 3)
      debugPrintf("[audio] audout append failed 0x%x (retrying)\n", (unsigned)rc);
    svcSleepThread(2000000ll);
    reap();
  }
  g_dropped++;
}

/* Self-check of the descriptor layout, before the game runs: two buffers of
 * silence must come back from the audio server. */
void abs_audio_selftest(void) {
  if (ao_open() != 0)
    return;
  static int16_t silence[FRAMES_PER_BUF * 2];
  submit(silence);
  submit(silence);
  u64 t0 = armGetSystemTick();
  int back = 0;
  while (armTicksToNs(armGetSystemTick() - t0) < 500000000ull) {
    reap();
    back = 0;
    for (int i = 0; i < NBUF; i++)
      back += !g_queued[i];
    if (back == NBUF)
      break;
    svcSleepThread(5000000ll);
  }
  debugPrintf("[audio] self-test: %s (%d/%d buffers returned in %llu ms)\n",
              back == NBUF ? "OK" : "FAILED -- buffer descriptor not accepted", back, NBUF,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

/* ------------------------------------------------------ AudioOutput */
typedef void (*fn_mix)(void *env, void *thiz, jlong handle, void *array, jint len);

static struct {
  JObj *obj;       /* the AudioOutput (a reference held while it plays) */
  JObj *arr;       /* the byte[] the mixer fills */
  int64_t handle;
  int rate, ch, bits, frame_bytes;
  fn_mix mix;
} A;

static Thread g_thread;
static int g_thread_up;
static volatile int g_paused;
static volatile uint32_t g_mixes;
static int16_t g_hist[3][2]; /* the last three frames of the previous pull */
static double g_pos;

uint32_t abs_audio_mixes(void) { return g_mixes; }
void abs_audio_pause(int paused) { g_paused = paused; }

void abs_audio_new(JObj *obj, int64_t handle, int rate, int channels, int bits, int bufsize) {
  A.handle = handle;
  A.rate = rate;
  A.ch = channels;
  A.bits = bits;
  A.frame_bytes = (bits / 8) * channels;
  A.mix = (fn_mix)abs_native("Java_com_rovio_fusion_AudioOutput_nativeMixData");
  debugPrintf("[audio] new AudioOutput(mixer %p, %d Hz, %d ch, %d bit, %d bytes); nativeMixData %p\n",
              (void *)(uintptr_t)handle, rate, channels, bits, bufsize, (void *)A.mix);
}

/* frame i of this pull (i < 0: the end of the previous one) as s16 stereo */
static inline void frame_at(const uint8_t *in, int i, int16_t out[2]) {
  if (i < 0) {
    out[0] = g_hist[3 + i][0], out[1] = g_hist[3 + i][1];
    return;
  }
  if (A.bits == 16) {
    const int16_t *s = (const int16_t *)in + i * A.ch;
    out[0] = s[0];
    out[1] = A.ch == 2 ? s[1] : s[0];
  } else {
    const uint8_t *s = in + i * A.ch;
    out[0] = (int16_t)(((int)s[0] - 128) << 8);
    out[1] = A.ch == 2 ? (int16_t)(((int)s[1] - 128) << 8) : out[0];
  }
}

/* 4-point, 3rd-order Hermite between p1 and p2 */
static inline int16_t hermite(int p0, int p1, int p2, int p3, float t) {
  const float c1 = 0.5f * (float)(p2 - p0);
  const float c2 = (float)p0 - 2.5f * (float)p1 + 2.0f * (float)p2 - 0.5f * (float)p3;
  const float c3 = 0.5f * (float)(p3 - p0) + 1.5f * (float)(p1 - p2);
  float v = (((c3 * t + c2) * t + c1) * t) + (float)p1;
  return (int16_t)(v < -32768.0f ? -32768 : v > 32767.0f ? 32767 : v);
}

/* a full buffer out: the video's sound instead, while one plays */
static void emit(int16_t *out) {
  abs_video_mix(out, FRAMES_PER_BUF, (int)g_out_rate);
  submit(out);
}

static void audio_thread(void *arg) {
  static int16_t out[FRAMES_PER_BUF * 2];
  int out_n = 0;
  const double step = (double)A.rate / (double)g_out_rate;
  debugPrintf("[audio] mixer thread running (%d Hz -> %u Hz)\n", A.rate, (unsigned)g_out_rate);
  while (!g_stop_thread) {
    if (g_paused) {
      /* the game is in the background: nothing is pulled; audout plays out
       * what it has and then idles */
      svcSleepThread(10000000ll);
      continue;
    }
    const int frames = PULL_FRAMES;
    A.mix(g_jni_env, A.obj, A.handle, A.arr, frames * A.frame_bytes);
    g_mixes++;
    const uint8_t *in = A.arr->a.data;
    if (A.rate == (int)g_out_rate && A.ch == 2 && A.bits == 16) {
      /* the common case: straight copy */
      for (int f = 0; f < frames; f++) {
        memcpy(&out[out_n * 2], in + f * 4, 4);
        if (++out_n == FRAMES_PER_BUF) {
          emit(out);
          out_n = 0;
        }
      }
      continue;
    }
    /* positions run from -2 (the previous pull's end) to frames - 2, so the
     * four points around each are known */
    int16_t f0[2], f1[2], f2[2], f3[2];
    while (g_pos < (double)(frames - 2)) {
      int i1 = (int)floor(g_pos);
      float t = (float)(g_pos - (double)i1);
      frame_at(in, i1 - 1, f0);
      frame_at(in, i1, f1);
      frame_at(in, i1 + 1, f2);
      frame_at(in, i1 + 2, f3);
      out[out_n * 2] = hermite(f0[0], f1[0], f2[0], f3[0], t);
      out[out_n * 2 + 1] = hermite(f0[1], f1[1], f2[1], f3[1], t);
      if (++out_n == FRAMES_PER_BUF) {
        emit(out);
        out_n = 0;
      }
      g_pos += step;
    }
    g_pos -= (double)frames;
    for (int k = 0; k < 3; k++)
      frame_at(in, frames - 3 + k, g_hist[k]);
  }
  debugPrintf("[audio] mixer thread stopped (%lu pulls, %lu underruns, %lu failed submits)\n",
              (unsigned long)g_mixes, g_underruns, g_append_fails);
}

int abs_audio_start(JObj *obj) {
  if (g_thread_up)
    return 1;
  if (!A.mix || A.frame_bytes <= 0 || A.rate < 8000 || (A.bits != 8 && A.bits != 16) ||
      (A.ch != 1 && A.ch != 2)) {
    debugPrintf("[audio] startOutput: not a format this output takes\n");
    return 0;
  }
  if (ao_open() != 0)
    return 0;
  A.obj = jni_retain(obj);
  A.arr = jni_array('B', PULL_FRAMES * A.frame_bytes);
  g_stop_thread = 0;
  g_pos = 0.0;
  memset(g_hist, 0, sizeof g_hist);
  /* priority 0x28: above the game's threads (59) and the main thread (44),
   * so the mixer keeps up during loads */
  if (R_FAILED(threadCreate(&g_thread, audio_thread, NULL, NULL, 0x10000, 0x28, 2)) ||
      R_FAILED(threadStart(&g_thread))) {
    debugPrintf("[audio] could not start the mixer thread\n");
    return 0;
  }
  g_thread_up = 1;
  debugPrintf("[audio] startOutput\n");
  return 1;
}

void abs_audio_stop(JObj *obj) {
  if (!g_thread_up)
    return;
  g_stop_thread = 1;
  threadWaitForExit(&g_thread);
  threadClose(&g_thread);
  g_thread_up = 0;
  jni_release(A.arr);
  jni_release(A.obj);
  A.arr = A.obj = NULL;
  debugPrintf("[audio] stopOutput\n");
}
