/* abs_video.c -- the links' videos, inside the game's popup about them.
 *
 * A video comes from YouTube, streamed (abs_yt.c finds its streams,
 * abs_net.c fetches them: 720p H.264 video and AAC audio as two files in
 * 1 MiB ranges, or the 360p file with both), or from the SD card
 * (videos/<topic>.mp4). Nothing waits on the game's thread: a loader thread
 * resolves the video, starts the downloads (a thread each, into memory) and
 * opens FFmpeg on them (tools/ffmpeg: MP4, H.264, AAC), then decodes: one
 * thread for both files, taking sound while it runs low and pictures while
 * there is room for them. Reads wait for the downloads.
 *
 * The pictures stay YUV 4:2:0 (three textures, the overlay's shader makes them
 * RGB: abs_ov_yuv) and follow a clock started with the first; they are drawn
 * in the rectangle the popup leaves for them (abs_ctl.lua reports it), so the
 * video plays inside the game's own popup, its title and X around it. While
 * it loads the popup shows the game's spinner (the states go back to the
 * script). The sound goes through a ring and replaces the game's in its
 * output (abs_audio.c asks abs_video_mix).
 *
 * A pauses, B stops (on the touchscreen: a tap on the picture pauses, the
 * popup's X stops), the left stick (or the D-pad) goes 10 s back or on:
 * the split streams are fetched from wherever the reader is, so a jump ahead
 * fetches that part first; while the download is behind, the clock waits
 * (a spinner over the picture) instead of running on. A time bar shows
 * while paused, waiting or just after a seek. A picture goes up to the GPU
 * once, however many frames show it. The CPU is boosted while a video plays.
 *
 * This file is compiled with -fno-short-enums, as FFmpeg is. MIT.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "abs_net.h"
#include "dcr_config.h"
#include "util.h"

#if !ABS_VIDEO /* built without FFmpeg: the popups offer no videos */
int abs_video_available(void) { return 0; }
int abs_video_play(const char *path) {
  debugPrintf("[video] %s: this build has no video decoder\n", path);
  return 0;
}
int abs_video_play_youtube(const char *id) { return abs_video_play(id); }
int abs_video_state(void) { return ABS_VS_IDLE; }
int abs_video_active(void) { return 0; }
void abs_video_stop(void) {}
void abs_video_set_rect(float x, float y, float w, float h) { (void)x, (void)y, (void)w, (void)h; }
void abs_video_input(uint64_t down, uint64_t held, float lsx) { (void)down, (void)held, (void)lsx; }
int abs_video_tap(float x, float y) { (void)x, (void)y; return 0; }
void abs_video_draw(void) {}
int abs_video_mix(int16_t *out, int frames, int rate) {
  (void)out, (void)frames, (void)rate;
  return 0;
}
#else

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

void dcr_boost_hold(int on); /* dcr_boost.c */

/* FFmpeg 7.1's h2645_sei.c resets the AOM film grain sets unconditionally,
 * but aom_film_grain.o is only built with HEVC, which this build leaves out;
 * without HEVC nothing ever fills them (their parser sits behind IS_HEVC(),
 * constant false), so there is nothing to reset. */
void ff_aom_uninit_film_grain_params(void *s);
void ff_aom_uninit_film_grain_params(void *s) { (void)s; }

#define NSLOT 6        /* pictures decoded ahead */
#define RING_SECONDS 6 /* sound decoded ahead */
#define RANGE (1 << 20)
#define YT_UA "User-Agent: com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip\r\n"

/* ------------------------------------------------------------ byte sources */
/* A split YouTube stream (its size known) is fetched in RANGE-sized pieces,
 * the next one always the first missing from where the reader is, so a
 * jump ahead (a seek) fetches that part first; the pieces fill a buffer the
 * size of the file. The 360p file comes in one request, from its start. */
typedef struct {
  FILE *file;               /* a card file, or ... */
  char *url;                /* ... a download into buf */
  int ranged;
  uint8_t *buf;
  int64_t size, have, cap;  /* size -1 until known; have: bytes fetched */
  uint8_t *piece;           /* ranged: which pieces are in */
  int npiece, cur;          /* cur: the piece being fetched (-1: none) */
  int64_t cur_have;         /* its bytes so far */
  volatile int done, failed;
  int64_t pos;
  Mutex lock;
  CondVar cv;
  Thread th;
  int th_on;
} Src;

typedef struct {
  uint8_t *plane[3];
  int stride[3];
  double pts;
  volatile int ready;
} Pic;

static struct {
  Mutex lock;
  volatile int state, stop, eof;
  char want[300];           /* the file path, or the video id */
  int from_yt;
  Thread loader;
  int loader_on;
  Src src[2];               /* [0] video (or both), [1] audio of a split stream */
  int nsrc;
  AVFormatContext *fmt[2];
  AVIOContext *io[2];
  AVCodecContext *vdec, *adec;
  int vs, as;               /* the streams, in fmt[vfmt] / fmt[afmt] */
  int vfmt, afmt;
  double vtb, atb;
  double duration;          /* seconds (0: not known) */
  int w, h;
  Pic pic[NSLOT];
  int shown, uploaded;      /* the slot on screen; the one in the textures */
  double shown_pts;
  int started;
  u64 t0;
  u64 freeze_at;            /* the clock stopped (paused or waiting): since this tick */
  int paused, stalled;
  /* seeking: asked (input thread), carried out (loader), done (the clock
   * starts again at the first picture from there) */
  volatile int seek_req, seek_pending;
  double seek_to, seek_pts;
  u64 bar_until;            /* the time bar shows until this tick */
  volatile int aeof;
  int16_t *ring;
  size_t ring_cap, ring_r, ring_w;
  int arate;
  double apos;
  volatile int audio_go;
  float rx, ry, rw, rh;     /* the popup's rectangle (rw <= 0: full screen) */
  int boosted;
  int shown_count, dropped, stalls, seeks;
} V = {.shown = -1, .uploaded = -1, .vs = -1, .as = -1, .seek_pts = -1};

int abs_video_available(void) { return 1; }
int abs_video_state(void) { return V.state; }
int abs_video_active(void) {
  int s = V.state;
  return s >= ABS_VS_CONNECTING && s <= ABS_VS_PAUSED;
}

void abs_video_set_rect(float x, float y, float w, float h) {
  V.rx = x, V.ry = y, V.rw = w, V.rh = h;
}

static void src_signal(Src *s) {
  mutexLock(&s->lock);
  condvarWakeAll(&s->cv);
  mutexUnlock(&s->lock);
}

/* room for n more bytes (the size is known for ranged files) */
static int src_grow(Src *s, int64_t need) {
  if (need <= s->cap)
    return 1;
  int64_t cap = s->cap ? s->cap : (1 << 22);
  while (cap < need)
    cap *= 2;
  mutexLock(&s->lock);
  uint8_t *b = realloc(s->buf, (size_t)cap);
  if (b)
    s->buf = b, s->cap = cap;
  mutexUnlock(&s->lock);
  return b != NULL;
}

/* (lock held) the next piece to fetch: the first missing one from where the
 * reader is, then any before it; -1 when all are in */
static int next_piece(Src *s) {
  int from = s->pos < s->size ? (int)(s->pos / RANGE) : 0;
  for (int i = from; i < s->npiece; i++)
    if (!s->piece[i])
      return i;
  for (int i = 0; i < from; i++)
    if (!s->piece[i])
      return i;
  return -1;
}

/* YouTube refuses a stream link now and then (403), or it expires: fresh
 * ones from the player API, the same file (its size), as switch-newpipe does */
static Mutex g_refresh_lock;
static int refresh_url(Src *s) {
  if (!V.from_yt)
    return 0;
  mutexLock(&g_refresh_lock);
  AbsYtStreams yt;
  memset(&yt, 0, sizeof yt);
  char err[160];
  int ok = !V.stop && abs_yt_resolve(V.want, &yt, err, sizeof err) == 0 && yt.video_url;
  if (ok) {
    const int video = s == &V.src[0];
    const char *u = video ? yt.video_url : yt.audio_url;
    ok = u && (video ? yt.video_len : yt.audio_len) == s->size;
    char *copy = ok ? strdup(u) : NULL;
    if (copy) {
      free(s->url);
      s->url = copy;
    } else {
      ok = 0;
    }
  }
  abs_yt_free(&yt);
  mutexUnlock(&g_refresh_lock);
  debugPrintf("[video] fresh stream links%s\n", ok ? "" : ": none");
  return ok;
}

static void download_ranged(Src *s, uint8_t *tmp) {
  int tries = 0, refreshed = 0;
  while (!V.stop) {
    mutexLock(&s->lock);
    const int c = next_piece(s);
    s->cur = c, s->cur_have = 0;
    mutexUnlock(&s->lock);
    if (c < 0) {
      s->done = 1;
      break;
    }
    const int64_t start = (int64_t)c * RANGE;
    const int64_t len = (start + RANGE <= s->size ? RANGE : s->size - start);
    char url[8400];
    snprintf(url, sizeof url, "%s&range=%lld-%lld", s->url, (long long)start, (long long)(start + len - 1));
    int status = 0;
    AbsHttp *h = abs_http_open("GET", url, YT_UA, NULL, 0, &status);
    int64_t got = 0;
    if (h && (status == 200 || status == 206)) {
      int r;
      while (!V.stop && got < len && (r = abs_http_read(h, tmp, 65536)) > 0) {
        if (r > len - got)
          r = (int)(len - got);
        memcpy(s->buf + start + got, tmp, (size_t)r);
        mutexLock(&s->lock);
        got += r;
        s->cur_have = got;
        condvarWakeAll(&s->cv);
        mutexUnlock(&s->lock);
      }
    }
    abs_http_close(h);
    if (got == len) {
      mutexLock(&s->lock);
      s->piece[c] = 1;
      s->have += len;
      s->cur = -1;
      condvarWakeAll(&s->cv);
      mutexUnlock(&s->lock);
      tries = 0;
      continue;
    }
    /* broken off: the piece again (what came of it is the same bytes) */
    if (V.stop)
      break;
    if ((status == 403 || status == 410) && refreshed < 4 && refresh_url(s)) {
      refreshed++;
      continue;
    }
    if (++tries >= 4) {
      debugPrintf("[video] download failed at %lld (HTTP %d)\n", (long long)(start + got), status);
      s->failed = 1;
      break;
    }
    svcSleepThread(300000000ll * tries);
  }
}

static void download_whole(Src *s, uint8_t *tmp) {
  int tries = 0;
  while (!V.stop && !s->done) {
    /* broken off: the rest of it */
    char hdr[256];
    if (s->have > 0)
      snprintf(hdr, sizeof hdr, YT_UA "Range: bytes=%lld-\r\n", (long long)s->have);
    else
      snprintf(hdr, sizeof hdr, YT_UA);
    int status = 0;
    AbsHttp *h = abs_http_open("GET", s->url, hdr, NULL, 0, &status);
    if (h && s->have > 0 && status == 200) {
      /* the server sent the whole file again: it cannot go on from there */
      abs_http_close(h);
      s->failed = 1;
      break;
    }
    if (!h || (status != 200 && status != 206)) {
      abs_http_close(h);
      if (V.stop)
        break;
      if (++tries >= 4) {
        debugPrintf("[video] download failed at %lld (HTTP %d)\n", (long long)s->have, status);
        s->failed = 1;
        break;
      }
      svcSleepThread(300000000ll * tries);
      continue;
    }
    if (s->size < 0 && abs_http_length(h) > 0) {
      s->size = abs_http_length(h);
      src_grow(s, s->size);
    }
    int r = 0;
    while (!V.stop && (r = abs_http_read(h, tmp, 65536)) > 0) {
      if (!src_grow(s, s->have + r)) {
        s->failed = 1;
        break;
      }
      memcpy(s->buf + s->have, tmp, (size_t)r);
      mutexLock(&s->lock);
      s->have += r;
      condvarWakeAll(&s->cv);
      mutexUnlock(&s->lock);
    }
    abs_http_close(h);
    if (s->failed)
      break;
    if (r < 0 && ++tries < 4) /* a broken connection: again from where it stopped */
      continue;
    if (r < 0) {
      s->failed = 1;
      break;
    }
    if (s->size < 0)
      s->size = s->have;
    s->done = 1;
  }
}

static void download(void *arg) {
  Src *s = arg;
  uint8_t *tmp = malloc(65536);
  if (tmp)
    s->ranged ? download_ranged(s, tmp) : download_whole(s, tmp);
  else
    s->failed = 1;
  free(tmp);
  src_signal(s);
}

static int src_start(Src *s, const char *url, int64_t size, int ranged) {
  memset(s, 0, sizeof *s);
  mutexInit(&s->lock);
  condvarInit(&s->cv);
  s->cur = -1;
  size_t n = strlen(url) + 1;
  s->url = malloc(n);
  if (!s->url)
    return 0;
  memcpy(s->url, url, n);
  s->size = size > 0 ? size : -1;
  s->ranged = ranged && size > 0;
  if (s->size > 0 && !src_grow(s, s->size))
    return 0;
  if (s->ranged) {
    s->npiece = (int)((s->size + RANGE - 1) / RANGE);
    if (!(s->piece = calloc((size_t)s->npiece, 1)))
      return 0;
  }
  /* a core the game does not use; below the sound mixer */
  if (R_FAILED(threadCreate(&s->th, download, s, NULL, 0x10000, 0x2C, 2)) || R_FAILED(threadStart(&s->th)))
    return 0;
  s->th_on = 1;
  return 1;
}

static void src_free(Src *s) {
  if (s->th_on) {
    src_signal(s);
    threadWaitForExit(&s->th);
    threadClose(&s->th);
  }
  if (s->file)
    fclose(s->file);
  free(s->buf);
  free(s->url);
  free(s->piece);
  memset(s, 0, sizeof *s);
}

/* (lock held) bytes that can be read from p on, up to n */
static int64_t src_avail(const Src *s, int64_t p, int n) {
  if (!s->ranged)
    return s->have - p;
  if (p >= s->size)
    return 0;
  int c = (int)(p / RANGE);
  int64_t end;
  if (s->piece[c]) {
    end = (int64_t)(c + 1) * RANGE;
    while (end < s->size && end - p < n && s->piece[end / RANGE])
      end += RANGE;
  } else if (c == s->cur) {
    end = (int64_t)c * RANGE + s->cur_have;
  } else {
    return 0;
  }
  if (end > s->size)
    end = s->size;
  return end - p;
}

static int io_read(void *opaque, uint8_t *buf, int n) {
  Src *s = opaque;
  if (s->file) {
    size_t got = fread(buf, 1, (size_t)n, s->file);
    return got ? (int)got : AVERROR_EOF;
  }
  mutexLock(&s->lock);
  int64_t left;
  while ((left = src_avail(s, s->pos, n)) <= 0 && !(s->size >= 0 && s->pos >= s->size) &&
         !(s->done && !s->ranged) && !s->failed && !V.stop)
    condvarWaitTimeout(&s->cv, &s->lock, 100000000ll);
  int got = 0;
  if (left > 0) {
    got = left < n ? (int)left : n;
    memcpy(buf, s->buf + s->pos, (size_t)got);
    s->pos += got;
  }
  mutexUnlock(&s->lock);
  if (got)
    return got;
  return s->failed ? AVERROR(EIO) : AVERROR_EOF;
}

static int64_t io_seek(void *opaque, int64_t off, int whence) {
  Src *s = opaque;
  if (s->file) {
    if (whence == AVSEEK_SIZE) {
      long here = ftell(s->file);
      fseek(s->file, 0, SEEK_END);
      long size = ftell(s->file);
      fseek(s->file, here, SEEK_SET);
      return size;
    }
    whence &= ~AVSEEK_FORCE;
    if (fseek(s->file, (long)off, whence) != 0)
      return -1;
    return ftell(s->file);
  }
  /* a download: no size (the MP4 reader would look for an index at the end
   * of a fragmented file; the one at its start is enough) */
  if (whence == AVSEEK_SIZE)
    return -1;
  whence &= ~AVSEEK_FORCE;
  mutexLock(&s->lock);
  int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? s->pos + off : (s->size >= 0 ? s->size + off : -1);
  if (p < 0 || (s->size >= 0 && p > s->size)) {
    mutexUnlock(&s->lock);
    return -1;
  }
  s->pos = p;
  condvarWakeAll(&s->cv); /* the downloader picks its next piece from here */
  mutexUnlock(&s->lock);
  return p;
}

/* ------------------------------------------------------------ pictures */
static int free_slot(void) {
  for (int i = 0; i < NSLOT; i++)
    if (!V.pic[i].ready && i != V.shown)
      return i;
  return -1;
}

static void put_picture(const AVFrame *f) {
  if ((f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) || f->width != V.w ||
      f->height != V.h)
    return;
  const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
  const double pts = ts == AV_NOPTS_VALUE ? 0 : (double)ts * V.vtb;
  if (V.seek_pts >= 0 && pts < V.seek_pts - 0.001)
    return; /* decoded on the way from the keyframe to where the seek went */
  int slot = -1;
  while (!V.stop && !V.seek_req) {
    mutexLock(&V.lock);
    slot = free_slot();
    mutexUnlock(&V.lock);
    if (slot >= 0)
      break;
    svcSleepThread(3000000ll);
  }
  if (slot < 0)
    return;
  Pic *p = &V.pic[slot];
  for (int i = 0; i < 3; i++) {
    const int pw = i ? (V.w + 1) / 2 : V.w, ph = i ? (V.h + 1) / 2 : V.h;
    for (int y = 0; y < ph; y++)
      memcpy(p->plane[i] + (size_t)y * p->stride[i], f->data[i] + (size_t)y * f->linesize[i], (size_t)pw);
  }
  mutexLock(&V.lock);
  p->pts = pts;
  p->ready = 1;
  mutexUnlock(&V.lock);
}

/* ------------------------------------------------------------ sound */
static size_t ring_used(void) { return V.ring_cap ? (V.ring_w + V.ring_cap - V.ring_r) % V.ring_cap : 0; }

static void put_sound(const AVFrame *f) {
  if (!V.ring || f->nb_samples <= 0)
    return;
  const int ch = f->ch_layout.nb_channels;
  int i = 0;
  if (V.seek_pts >= 0) {
    /* after a seek: from where it went */
    const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    if (ts != AV_NOPTS_VALUE) {
      const double skip = (V.seek_pts - (double)ts * V.atb) * f->sample_rate;
      if (skip >= f->nb_samples)
        return;
      if (skip > 0)
        i = (int)skip;
    }
  }
  while (i < f->nb_samples && !V.stop && !V.seek_req) {
    mutexLock(&V.lock);
    size_t space = V.ring_cap - 1 - ring_used();
    mutexUnlock(&V.lock);
    if (!space) {
      svcSleepThread(5000000ll);
      continue;
    }
    for (; i < f->nb_samples && space; i++, space--) {
      int16_t *d = V.ring + V.ring_w * 2;
      for (int c = 0; c < 2; c++) {
        const int k = c < ch ? c : 0;
        float s;
        switch (f->format) {
        case AV_SAMPLE_FMT_FLTP: s = ((const float *)f->extended_data[k])[i]; break;
        case AV_SAMPLE_FMT_FLT: s = ((const float *)f->extended_data[0])[i * ch + k]; break;
        case AV_SAMPLE_FMT_S16P: s = ((const int16_t *)f->extended_data[k])[i] / 32768.0f; break;
        case AV_SAMPLE_FMT_S16: s = ((const int16_t *)f->extended_data[0])[i * ch + k] / 32768.0f; break;
        case AV_SAMPLE_FMT_S32P: s = ((const int32_t *)f->extended_data[k])[i] / 2147483648.0f; break;
        default: s = 0; break;
        }
        int v = (int)(s * 32767.0f);
        d[c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
      }
      mutexLock(&V.lock);
      V.ring_w = (V.ring_w + 1) % V.ring_cap;
      mutexUnlock(&V.lock);
    }
  }
}

int abs_video_mix(int16_t *out, int frames, int rate) {
  int s = V.state;
  if (s < ABS_VS_CONNECTING || s > ABS_VS_PAUSED)
    return 0;
  memset(out, 0, (size_t)frames * 4);
  mutexLock(&V.lock);
  if (!V.audio_go || !V.ring || V.arate <= 0 || V.freeze_at) {
    mutexUnlock(&V.lock);
    return 1;
  }
  const double step = (double)V.arate / (double)rate;
  for (int i = 0; i < frames; i++) {
    if (ring_used() < 2)
      break;
    const size_t a = V.ring_r, b = (V.ring_r + 1) % V.ring_cap;
    const double t = V.apos;
    for (int c = 0; c < 2; c++)
      out[i * 2 + c] = (int16_t)(V.ring[a * 2 + c] * (1 - t) + V.ring[b * 2 + c] * t);
    V.apos += step;
    while (V.apos >= 1.0 && ring_used() >= 2) {
      V.apos -= 1.0;
      V.ring_r = (V.ring_r + 1) % V.ring_cap;
    }
  }
  mutexUnlock(&V.lock);
  return 1;
}

/* ------------------------------------------------------------ decoding */
static AVCodecContext *open_decoder(AVStream *st, int video) {
  const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
  if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  if (video) {
    /* one core: the loop filter is skipped on pictures nothing refers to */
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx->skip_loop_filter = AVDISCARD_NONREF;
  }
  if (avcodec_open2(ctx, c, NULL) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  return ctx;
}

static int open_fmt(int i) {
  unsigned char *buf = av_malloc(65536);
  V.io[i] = buf ? avio_alloc_context(buf, 65536, 0, &V.src[i], io_read, NULL, io_seek) : NULL;
  V.fmt[i] = avformat_alloc_context();
  if (!V.io[i] || !V.fmt[i])
    return 0;
  V.fmt[i]->pb = V.io[i];
  if (avformat_open_input(&V.fmt[i], NULL, NULL, NULL) < 0) {
    V.fmt[i] = NULL; /* freed by avformat_open_input */
    return 0;
  }
  return avformat_find_stream_info(V.fmt[i], NULL) >= 0;
}

/* one packet from fmt i, decoded; 0 at its end */
static int step(int i, AVPacket *pkt, AVFrame *fr) {
  if (av_read_frame(V.fmt[i], pkt) < 0) {
    AVCodecContext *decs[2] = {V.vfmt == i ? V.vdec : NULL, V.afmt == i ? V.adec : NULL};
    for (int k = 0; k < 2; k++) {
      if (!decs[k])
        continue;
      avcodec_send_packet(decs[k], NULL);
      while (!V.stop && avcodec_receive_frame(decs[k], fr) == 0)
        k ? put_sound(fr) : put_picture(fr);
    }
    return 0;
  }
  AVCodecContext *dec = (i == V.vfmt && pkt->stream_index == V.vs) ? V.vdec
                        : (i == V.afmt && pkt->stream_index == V.as) ? V.adec : NULL;
  if (dec && avcodec_send_packet(dec, pkt) >= 0)
    while (!V.stop && avcodec_receive_frame(dec, fr) == 0)
      dec == V.vdec ? put_picture(fr) : put_sound(fr);
  av_packet_unref(pkt);
  return 1;
}

/* (loader) a seek asked for: to the keyframe before it, decoding on from
 * there without showing what comes before; the picture on screen stays
 * until the first one from there, and the clock starts again with it */
static void do_seek(void) {
  mutexLock(&V.lock);
  const double to = V.seek_to;
  mutexUnlock(&V.lock);
  int ok = av_seek_frame(V.fmt[V.vfmt], V.vs, (int64_t)(to / V.vtb), AVSEEK_FLAG_BACKWARD) >= 0;
  if (ok && V.nsrc == 2 && V.adec && V.atb > 0)
    ok = av_seek_frame(V.fmt[V.afmt], V.as, (int64_t)(to / V.atb), AVSEEK_FLAG_BACKWARD) >= 0;
  avcodec_flush_buffers(V.vdec);
  if (V.adec)
    avcodec_flush_buffers(V.adec);
  mutexLock(&V.lock);
  for (int i = 0; i < NSLOT; i++)
    if (i != V.shown)
      V.pic[i].ready = 0;
  V.ring_r = V.ring_w = 0;
  V.apos = 0;
  V.audio_go = 0;
  V.started = 0;
  V.stalled = 0;
  V.eof = 0;
  V.seek_pts = to;
  V.seeks++;
  V.seek_req = V.seek_to != to; /* another push came meanwhile: that one next */
  mutexUnlock(&V.lock);
  debugPrintf("[video] seek to %.1f s%s\n", to, ok ? "" : " (failed: from where it was)");
}

static void set_error(int code, const char *why) {
  debugPrintf("[video] %s\n", why);
  V.state = code;
}

static void loader(void *arg) {
  (void)arg;
  AbsYtStreams yt;
  memset(&yt, 0, sizeof yt);
  char err[256] = "";
  V.state = ABS_VS_CONNECTING;
  if (V.from_yt) {
    if (!abs_net_online()) {
      set_error(ABS_VS_ERR_OFFLINE, "the console is not on the internet");
      return;
    }
    if (abs_yt_resolve(V.want, &yt, err, sizeof err)) {
      char m[600];
      snprintf(m, sizeof m, "%s: %s", V.want, err);
      set_error(ABS_VS_ERR_UNAVAILABLE, m);
      return;
    }
    if (V.stop)
      goto out;
    int ok;
    if (yt.video_url) {
      V.nsrc = 2;
      ok = src_start(&V.src[0], yt.video_url, yt.video_len, 1) && src_start(&V.src[1], yt.audio_url, yt.audio_len, 1);
      debugPrintf("[video] %s: %dp, %lld + %lld bytes\n", V.want, yt.height, (long long)yt.video_len,
                  (long long)yt.audio_len);
    } else {
      V.nsrc = 1;
      ok = src_start(&V.src[0], yt.prog_url, -1, 0);
      debugPrintf("[video] %s: 360p\n", V.want);
    }
    if (!ok) {
      set_error(ABS_VS_ERR_NETWORK, "the download could not start");
      goto out;
    }
  } else {
    V.nsrc = 1;
    memset(&V.src[0], 0, sizeof V.src[0]);
    V.src[0].file = fopen(V.want, "rb");
    if (!V.src[0].file) {
      set_error(ABS_VS_ERR_UNAVAILABLE, "the video file could not be opened");
      goto out;
    }
  }
  V.state = ABS_VS_BUFFERING;
  av_log_set_level(AV_LOG_ERROR);
  for (int i = 0; i < V.nsrc; i++)
    if (!open_fmt(i)) {
      set_error(V.src[i].failed ? ABS_VS_ERR_NETWORK : ABS_VS_ERR_DECODE, "the video could not be read");
      goto out;
    }
  V.vfmt = 0, V.afmt = V.nsrc - 1;
  V.vs = av_find_best_stream(V.fmt[V.vfmt], AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  V.as = av_find_best_stream(V.fmt[V.afmt], AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (V.vs >= 0)
    V.vdec = open_decoder(V.fmt[V.vfmt]->streams[V.vs], 1);
  if (V.as >= 0)
    V.adec = open_decoder(V.fmt[V.afmt]->streams[V.as], 0);
  if (!V.vdec) {
    set_error(ABS_VS_ERR_DECODE, "no decoder for the video's pictures (H.264 or MPEG-4 needed)");
    goto out;
  }
  V.w = V.vdec->width, V.h = V.vdec->height;
  if (V.w <= 0 || V.h <= 0 || V.w > 1920 || V.h > 1088) {
    set_error(ABS_VS_ERR_DECODE, "the video is too big (1920x1080 at most)");
    goto out;
  }
  V.vtb = av_q2d(V.fmt[V.vfmt]->streams[V.vs]->time_base);
  V.atb = V.as >= 0 ? av_q2d(V.fmt[V.afmt]->streams[V.as]->time_base) : 0;
  {
    AVStream *st = V.fmt[V.vfmt]->streams[V.vs];
    V.duration = V.fmt[V.vfmt]->duration > 0 ? (double)V.fmt[V.vfmt]->duration / AV_TIME_BASE
                 : st->duration > 0            ? (double)st->duration * V.vtb
                                               : 0;
  }
  for (int i = 0; i < NSLOT; i++)
    for (int k = 0; k < 3; k++) {
      const int pw = k ? (V.w + 1) / 2 : V.w, ph = k ? (V.h + 1) / 2 : V.h;
      V.pic[i].stride[k] = pw;
      if (!(V.pic[i].plane[k] = memalign(64, (size_t)pw * ph))) {
        set_error(ABS_VS_ERR_DECODE, "no memory for the pictures");
        goto out;
      }
    }
  if (V.adec) {
    V.arate = V.adec->sample_rate > 0 ? V.adec->sample_rate : 44100;
    mutexLock(&V.lock);
    V.ring_cap = (size_t)(RING_SECONDS * V.arate);
    V.ring = malloc(V.ring_cap * 4);
    mutexUnlock(&V.lock);
  }
  debugPrintf("[video] %dx%d %s, %s\n", V.w, V.h, avcodec_get_name(V.vdec->codec_id),
              V.adec ? avcodec_get_name(V.adec->codec_id) : "no sound");

  /* decode: sound while it runs low, pictures while there is room; at the
   * end, wait: a seek can still go back */
  {
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    int v_end = 0, a_end = V.nsrc == 1 || !V.adec;
    V.aeof = a_end;
    while (pkt && fr && !V.stop) {
      if (V.seek_req) {
        do_seek();
        v_end = 0, a_end = V.nsrc == 1 || !V.adec;
        V.aeof = a_end;
        continue;
      }
      if (v_end && a_end) {
        V.eof = 1;
        svcSleepThread(10000000ll);
        continue;
      }
      if (V.nsrc == 1) {
        if (!step(0, pkt, fr))
          v_end = 1;
        continue;
      }
      mutexLock(&V.lock);
      size_t used = ring_used();
      int room = free_slot() >= 0;
      mutexUnlock(&V.lock);
      int sound_low = !a_end && used < (size_t)V.arate * 2;
      if (sound_low || (!room && !a_end && used < V.ring_cap - (size_t)V.arate)) {
        if (!step(1, pkt, fr))
          a_end = 1, V.aeof = 1;
      } else if (room && !v_end) {
        if (!step(0, pkt, fr))
          v_end = 1;
      } else {
        svcSleepThread(3000000ll);
      }
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
  }
  debugPrintf("[video] decoded%s\n", V.stop ? " (stopped)" : "");
  V.eof = 1;
out:
  abs_yt_free(&yt);
}

static void free_all(void) {
  V.stop = 1;
  abs_net_abort_all(); /* a download or the player API waiting on the network lets go */
  for (int i = 0; i < 2; i++)
    if (V.src[i].th_on)
      src_signal(&V.src[i]);
  if (V.loader_on) {
    threadWaitForExit(&V.loader);
    threadClose(&V.loader);
    V.loader_on = 0;
  }
  for (int i = 0; i < 2; i++)
    src_free(&V.src[i]);
  mutexLock(&V.lock);
  V.audio_go = 0;
  mutexUnlock(&V.lock);
  avcodec_free_context(&V.vdec);
  avcodec_free_context(&V.adec);
  for (int i = 0; i < 2; i++) {
    if (V.fmt[i])
      avformat_close_input(&V.fmt[i]);
    if (V.io[i]) {
      av_freep(&V.io[i]->buffer);
      avio_context_free(&V.io[i]);
    }
  }
  for (int i = 0; i < NSLOT; i++) {
    for (int k = 0; k < 3; k++) {
      free(V.pic[i].plane[k]);
      V.pic[i].plane[k] = NULL;
    }
    V.pic[i].ready = 0;
  }
  mutexLock(&V.lock);
  free(V.ring);
  V.ring = NULL;
  V.ring_cap = 0;
  mutexUnlock(&V.lock);
  V.shown = V.uploaded = -1;
  V.vs = V.as = -1;
  V.nsrc = 0;
  if (V.boosted) {
    dcr_boost_hold(0);
    V.boosted = 0;
  }
}

static int start(const char *what, int yt) {
  static int inited;
  if (!inited) {
    mutexInit(&V.lock);
    mutexInit(&g_refresh_lock);
    inited = 1;
  }
  free_all();
  V.stop = V.eof = V.aeof = 0;
  V.started = 0;
  V.freeze_at = 0;
  V.paused = V.stalled = 0;
  V.seek_req = V.seek_pending = 0;
  V.seek_pts = -1;
  V.bar_until = 0;
  V.duration = 0;
  V.apos = 0;
  V.ring_r = V.ring_w = 0;
  V.shown_count = V.dropped = V.stalls = V.seeks = 0;
  V.uploaded = -1;
  V.from_yt = yt;
  snprintf(V.want, sizeof V.want, "%s", what);
  V.state = ABS_VS_CONNECTING;
  /* the loader and decoder: core 1 (the game draws on core 0, its sound mixes
   * on 2), at the game's own priority (59, the lowest the NPDM allows: 0x3C
   * is refused as an invalid priority), so it shares a core by time slices
   * and a seek's burst of decoding never takes a frame from the game */
  if (R_FAILED(threadCreate(&V.loader, loader, NULL, NULL, 0x40000, 0x3B, 1)) || R_FAILED(threadStart(&V.loader))) {
    set_error(ABS_VS_ERR_DECODE, "no thread for the video");
    return 0;
  }
  V.loader_on = 1;
  dcr_boost_hold(1);
  V.boosted = 1;
  debugPrintf("[video] %s %s\n", yt ? "streaming" : "playing", what);
  return 1;
}

int abs_video_play(const char *path) { return start(path, 0); }
int abs_video_play_youtube(const char *id) { return start(id, 1); }

/* (any thread) the end is carried out where the pictures are drawn */
static volatile int g_stop_req;
void abs_video_stop(void) { g_stop_req = 1; }

/* The clock: seconds of the video, standing still while it is paused or
 * waits for the download (V.lock held for the changes). */
static void clock_freeze(void) {
  if (!V.freeze_at)
    V.freeze_at = armGetSystemTick();
}

static void clock_thaw(void) {
  if (V.freeze_at && !V.paused && !V.stalled) {
    V.t0 += armGetSystemTick() - V.freeze_at;
    V.freeze_at = 0;
  }
}

static double now_s(void) {
  u64 t = V.freeze_at ? V.freeze_at : armGetSystemTick();
  return (double)armTicksToNs(t - V.t0) / 1e9;
}

static void show_bar(void) { V.bar_until = armGetSystemTick() + armNsToTicks(2500000000ull); }

static void set_pause(int on) {
  mutexLock(&V.lock);
  if (on && !V.paused) {
    V.paused = 1;
    clock_freeze();
    V.state = ABS_VS_PAUSED;
  } else if (!on && V.paused) {
    V.paused = 0;
    clock_thaw();
    V.state = ABS_VS_PLAYING;
  }
  mutexUnlock(&V.lock);
  show_bar();
}

/* where the video is: the seek on its way, or the clock */
static double position(void) {
  if (V.seek_pending)
    return V.seek_to;
  return V.started ? now_s() : V.shown_pts;
}

static void seek_by(double delta) {
  if (V.duration <= 0 || !V.vdec)
    return;
  mutexLock(&V.lock);
  double to = position() + delta;
  if (to > V.duration - 1.0)
    to = V.duration - 1.0;
  if (to < 0)
    to = 0;
  V.seek_to = to;
  V.seek_pending = 1;
  V.seek_req = 1;
  mutexUnlock(&V.lock);
  show_bar();
}

void abs_video_input(uint64_t down, uint64_t held, float lsx) {
  const uint64_t k_a = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  const uint64_t k_b = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;
  if (down & (k_b | HidNpadButton_Plus)) {
    g_stop_req = 1;
    return;
  }
  const int s = V.state;
  if (s != ABS_VS_PLAYING && s != ABS_VS_PAUSED)
    return;
  if (down & k_a)
    set_pause(s == ABS_VS_PLAYING);
  /* 10 s back / on: a push of the stick or the D-pad, again while held */
  static int last;
  static u64 next;
  int dir = (held & HidNpadButton_Right) || lsx > 0.6f ? 1 : (held & HidNpadButton_Left) || lsx < -0.6f ? -1 : 0;
  u64 now = armGetSystemTick();
  if (dir && (dir != last || now >= next)) {
    seek_by(10.0 * dir);
    next = now + armNsToTicks(dir != last ? 450000000ull : 220000000ull);
  }
  last = dir;
}

/* The box the video plays in: the popup's, or the whole screen. */
static void video_box(float *x, float *y, float *w, float *h) {
  const int boxed = V.rw > 0 && V.rh > 0;
  *x = boxed ? V.rx : 0, *y = boxed ? V.ry : 0;
  *w = boxed ? V.rw : (float)abs_surface_w(), *h = boxed ? V.rh : (float)abs_surface_h();
}

/* A tap on the picture: pause / play (as A does); 1. Outside the video's
 * box: 0, the game's (abs_input.c passes it on: the popup's X stops the
 * video, as B does, and the rest of the popup takes no notice). */
int abs_video_tap(float x, float y) {
  float bx, by, bw, bh;
  video_box(&bx, &by, &bw, &bh);
  if (x < bx || x >= bx + bw || y < by || y >= by + bh)
    return 0;
  const int s = V.state;
  if (s == ABS_VS_PLAYING || s == ABS_VS_PAUSED) {
    set_pause(s == ABS_VS_PLAYING);
    show_bar();
  }
  return 1;
}

static void finish(int state, const char *why) {
  debugPrintf("[video] ended (%s): %d pictures shown, %d skipped late, %d waits for the download, %d seeks\n", why,
              V.shown_count, V.dropped, V.stalls, V.seeks);
  free_all();
  V.state = state;
}

static void fmt_time(char *out, size_t n, double t) {
  if (t < 0)
    t = 0;
  int s = (int)t;
  snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

/* the time bar along the bottom of the picture, and a spinner while it waits */
static void draw_controls(float bx, float by, float bw, float bh, int waiting, int bar) {
  const float k = (float)abs_surface_h() / 720.0f;
  if (waiting) {
    /* the dots of a spinner, going round */
    const float cx = bx + bw * 0.5f, cy = by + bh * 0.5f, r = 26 * k, d = 7 * k;
    const double t = (double)armTicksToNs(armGetSystemTick()) / 1e9;
    const int head = (int)(t * 10.0) % 8;
    abs_ov_rrect(cx - r - 2 * d, cy - r - 2 * d, 2 * (r + 2 * d), 2 * (r + 2 * d), r + 2 * d, 0x00000080u);
    for (int i = 0; i < 8; i++) {
      const float a = (float)i * 0.785398f;
      const int age = (head - i + 8) % 8;
      const uint32_t alpha = (uint32_t)(255 - age * 28);
      abs_ov_rrect(cx + cosf(a) * r - d * 0.5f, cy + sinf(a) * r - d * 0.5f, d, d, d * 0.5f, 0xffffff00u | alpha);
    }
  }
  if (!bar || V.duration <= 0)
    return;
  const double pos = position();
  const float h = 34 * k, pad = 12 * k, px = 15 * k;
  const float y = by + bh - h;
  abs_ov_rect(bx, y, bw, h, 0x000000a0u);
  char a[16], b[16], line[40];
  fmt_time(a, sizeof a, pos);
  fmt_time(b, sizeof b, V.duration);
  snprintf(line, sizeof line, "%s / %s", a, b);
  const float tw = abs_ov_text_width(px, line);
  abs_ov_text(bx + bw - pad - tw, y + (h - px) * 0.5f - 2 * k, px, 0xffffffffu, line);
  const float lx = bx + pad, lw = bw - 3 * pad - tw, ly = y + h * 0.5f - 2 * k;
  float f = (float)(pos / V.duration);
  if (f < 0)
    f = 0;
  if (f > 1)
    f = 1;
  abs_ov_rrect(lx, ly, lw, 4 * k, 2 * k, 0xffffff50u);
  abs_ov_rrect(lx, ly, lw * f, 4 * k, 2 * k, 0x8fd62effu); /* the game's green */
  abs_ov_rrect(lx + lw * f - 6 * k, ly - 4 * k, 12 * k, 12 * k, 6 * k, 0xffffffffu);
}

/* Inside abs_ov_begin/end, before the present. */
void abs_video_draw(void) {
  if (g_stop_req) {
    g_stop_req = 0;
    if (V.state != ABS_VS_IDLE)
      finish(ABS_VS_IDLE, "stopped");
    return;
  }
  const int s = V.state;
  if (s < ABS_VS_CONNECTING || s > ABS_VS_PAUSED)
    return;
  const float W = (float)abs_surface_w(), H = (float)abs_surface_h(), k = H / 720.0f;
  const int boxed = V.rw > 0 && V.rh > 0;
  if (!boxed)
    abs_ov_rect(0, 0, W, H, 0x000000ffu);
  if (s < ABS_VS_PLAYING && !V.started && V.shown < 0) {
    /* loading: the popup shows the game's spinner; full screen, a line */
    if (!boxed)
      abs_ov_text(W * 0.5f - abs_ov_text_width(24 * k, "Loading the video...") * 0.5f, H * 0.5f - 12 * k, 24 * k,
                  0xffffffc0u, "Loading the video...");
  }
  mutexLock(&V.lock);
  int due = -1;
  if (!V.started && V.vdec) {
    /* the start, or the start again after a seek: with a little sound in hand */
    double first = 1e9;
    int cand = -1;
    for (int i = 0; i < NSLOT; i++)
      if (V.pic[i].ready && i != V.shown && V.pic[i].pts < first)
        first = V.pic[i].pts, cand = i;
    if (cand >= 0 && (!V.ring || ring_used() > (size_t)(V.arate / 4) || V.aeof || V.eof)) {
      V.started = 1;
      V.t0 = armGetSystemTick() - armNsToTicks((u64)(first * 1e9));
      V.freeze_at = 0;
      if (V.paused)
        clock_freeze();
      V.audio_go = 1;
      if (!V.seek_req)
        V.seek_pending = 0;
      if (V.state != ABS_VS_PAUSED)
        V.state = ABS_VS_PLAYING;
      due = cand;
    }
  } else if (V.started) {
    const double t = now_s();
    for (int i = 0; i < NSLOT; i++)
      if (V.pic[i].ready && V.pic[i].pts <= t && (due < 0 || V.pic[i].pts > V.pic[due].pts))
        due = i;
    /* the download fell behind: the clock waits for it rather than run on
     * without pictures (or sound), and goes on when some are in hand */
    int newer = 0;
    for (int i = 0; i < NSLOT; i++)
      newer += V.pic[i].ready && i != V.shown && V.pic[i].pts > V.shown_pts;
    const size_t low = V.ring ? (size_t)(V.arate / 20) : 0, enough = V.ring ? (size_t)(V.arate / 2) : 0;
    const int sound_out = V.ring && !V.aeof && ring_used() < low;
    if (!V.stalled && !V.eof && ((!newer && t > V.shown_pts + 0.25) || sound_out)) {
      V.stalled = 1;
      V.stalls++;
      clock_freeze();
    } else if (V.stalled && (newer >= 2 || V.eof) && (!V.ring || V.aeof || ring_used() >= enough)) {
      V.stalled = 0;
      clock_thaw();
    }
  }
  if (due >= 0 && due != V.shown) {
    for (int i = 0; i < NSLOT; i++)
      if (V.pic[i].ready && i != due && i != V.shown && V.pic[i].pts < V.pic[due].pts) {
        V.pic[i].ready = 0;
        V.dropped++;
      }
    if (V.shown >= 0)
      V.pic[V.shown].ready = 0;
    V.shown_count++;
    V.shown = due;
    V.shown_pts = V.pic[due].pts;
  }
  int any_left = 0;
  for (int i = 0; i < NSLOT; i++)
    any_left |= V.pic[i].ready && i != V.shown;
  const int eof = V.eof, started = V.started;
  const int waiting = V.stalled || (V.seek_pending && !V.started) || (!V.started && V.shown >= 0);
  mutexUnlock(&V.lock);

  if (V.shown >= 0) {
    float bx = boxed ? V.rx : 0, by = boxed ? V.ry : 0, bw = boxed ? V.rw : W, bh = boxed ? V.rh : H;
    float dw = bw, dh = bw * (float)V.h / (float)V.w;
    if (dh > bh)
      dh = bh, dw = bh * (float)V.w / (float)V.h;
    if (boxed)
      abs_ov_rect(bx, by, bw, bh, 0x000000ffu);
    Pic *p = &V.pic[V.shown];
    const uint8_t *planes[3] = {p->plane[0], p->plane[1], p->plane[2]};
    /* a picture goes up to the textures once, however many frames show it */
    abs_ov_yuv(V.uploaded == V.shown ? NULL : planes, p->stride, V.w, V.h, bx + (bw - dw) * 0.5f,
               by + (bh - dh) * 0.5f, dw, dh);
    V.uploaded = V.shown;
    const int paused = V.state == ABS_VS_PAUSED;
    if (paused && !waiting) {
      /* paused: two bars in the middle, on a dimmed picture */
      float cx = bx + bw * 0.5f, cy = by + bh * 0.5f, u = 14 * k;
      abs_ov_rect(bx, by, bw, bh, 0x00000060u);
      abs_ov_rrect(cx - 2.6f * u, cy - 2.6f * u, 5.2f * u, 5.2f * u, 2.6f * u, 0x000000a0u);
      abs_ov_rrect(cx - 1.3f * u, cy - 1.5f * u, 0.9f * u, 3.0f * u, 0.3f * u, 0xffffffffu);
      abs_ov_rrect(cx + 0.4f * u, cy - 1.5f * u, 0.9f * u, 3.0f * u, 0.3f * u, 0xffffffffu);
    }
    draw_controls(bx, by, bw, bh, waiting, paused || waiting || armGetSystemTick() < V.bar_until);
  }
  if (V.src[0].failed || V.src[1].failed) {
    if (!started || (!any_left && V.shown >= 0)) {
      finish(ABS_VS_ERR_NETWORK, "the download broke off");
      return;
    }
  }
  if (V.state >= 60 && V.state < 70) /* the loader failed */
    return;
  if (eof && !any_left && started && now_s() > V.shown_pts + 0.2 && (!V.ring || ring_used() < 2))
    finish(ABS_VS_ENDED, "the end");
  else if (eof && !started && !any_left)
    finish(V.shown >= 0 ? ABS_VS_ENDED : ABS_VS_ERR_DECODE, V.shown >= 0 ? "the end" : "no pictures in it");
}
#endif /* ABS_VIDEO */
