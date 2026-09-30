/* test_video.c -- source/abs_video.c on a PC: the real player (streaming,
 * seeking, the clock that waits for the download) driven like the game
 * drives it -- a draw and a sound pull every 1/60 s, the controller's
 * input -- with the drawing counted instead of shown. A script of seeks and
 * pauses runs and each is checked against where the pictures went.
 *   H=build-ffmpeg-host/prefix M=<host mbedtls>
 *   cc -O2 -DABS_VIDEO=1 -I tools/host_shim -I source -I $H/include -I $M/include -I tools/mbedtls \
 *      '-DMBEDTLS_USER_CONFIG_FILE=<abs_mbedtls_user_config.h>' tools/test_video.c source/abs_net.c \
 *      source/abs_json.c source/abs_yt.c $H/lib/libavformat.a $H/lib/libavcodec.a $H/lib/libavutil.a <mbedtls libs>
 *   ./a.out CL15siZ2pv4          (a YouTube id)   ./a.out file.mp4   (a file) */
#include <stdarg.h>

#include "dcr_config.h"

static DcrConfig g_cfg;
const DcrConfig *dcr_config(void) { return &g_cfg; }
void dcr_boost_hold(int on) { (void)on; }
void debugPrintf(const char *fmt, ...);

#include "../source/abs_video.c"

void debugPrintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("    ");
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}

int abs_surface_w(void) { return 1280; }
int abs_surface_h(void) { return 720; }
static int g_draws, g_uploads, g_bars, g_spinner;
void abs_ov_rect(float x, float y, float w, float h, uint32_t rgba) { (void)x, (void)y, (void)w, (void)h, (void)rgba; }
void abs_ov_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {
  (void)x, (void)y, (void)w, (void)r, (void)rgba;
  if (h > 11 && h < 13)
    g_bars++; /* the time bar's knob */
  if (w > 6 && w < 8)
    g_spinner++; /* a spinner dot */
}
void abs_ov_text(float x, float y, float px, uint32_t rgba, const char *utf8) {
  (void)x, (void)y, (void)px, (void)rgba, (void)utf8;
}
float abs_ov_text_width(float px, const char *utf8) { return px * 0.5f * (float)strlen(utf8); }
void abs_ov_yuv(const uint8_t *const planes[3], const int strides[3], int w, int h, float x, float y, float dw,
                float dh) {
  (void)strides, (void)w, (void)h, (void)x, (void)y, (void)dw, (void)dh;
  g_draws++;
  if (planes)
    g_uploads++;
}

static double wall(void) { return (double)armTicksToNs(armGetSystemTick()) / 1e9; }

static int failures;
static void check(int ok, const char *what) {
  printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    failures++;
}

/* run the game's loop for sec seconds: a draw and a sound pull a frame */
static void run(double sec, uint64_t down, float lsx) {
  const double end = wall() + sec;
  int first = 1;
  while (wall() < end) {
    int16_t buf[800 * 2];
    abs_video_mix(buf, 800, 48000);
    abs_video_input(first ? down : 0, 0, first ? lsx : 0.0f);
    first = 0;
    abs_video_draw();
    svcSleepThread(16666667);
  }
}

static double shown(void) { return V.shown >= 0 ? V.shown_pts : -1; }

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *what = argc > 1 ? argv[1] : "CL15siZ2pv4";
  const int yt = !strchr(what, '.');
  abs_video_set_rect(160, 120, 960, 540);
  double t0 = wall();
  yt ? abs_video_play_youtube(what) : abs_video_play(what);
  while (V.state != ABS_VS_PLAYING && V.state < 60 && wall() - t0 < 30)
    run(0.1, 0, 0);
  check(V.state == ABS_VS_PLAYING, "it plays");
  if (V.state != ABS_VS_PLAYING)
    return 1;
  printf("  playing after %.1f s, %dx%d, %.0f s long\n", wall() - t0, V.w, V.h, V.duration);
  run(4, 0, 0);
  double a = shown();
  run(3, 0, 0);
  double b = shown();
  check(b - a > 2.6 && b - a < 3.4, "the pictures follow the clock (3 s)");
  printf("  3 s of play: %.2f s of video\n", b - a);

  /* +10 s: one push of the stick */
  double before = shown();
  run(0.05, 0, 1.0f);
  double t_seek = wall();
  while (V.seek_pending && wall() - t_seek < 15)
    run(0.05, 0, 0);
  printf("  +10 s from %.1f: at %.1f after %.2f s\n", before, shown(), wall() - t_seek);
  check(shown() > before + 9 && shown() < before + 11.5, "a push right goes 10 s on");
  run(2, 0, 0);

  /* three pushes in a row: +30 s */
  before = shown();
  for (int i = 0; i < 3; i++) {
    run(0.05, 0, 1.0f);
    run(0.05, 0, 0.0f);
  }
  t_seek = wall();
  while (V.seek_pending && wall() - t_seek < 20)
    run(0.05, 0, 0);
  printf("  +30 s from %.1f: at %.1f after %.2f s\n", before, shown(), wall() - t_seek);
  check(shown() > before + 28 && shown() < before + 32, "three pushes go 30 s on");
  run(2, 0, 0);

  /* back 20 s: two pushes left (the part is already in) */
  before = shown();
  run(0.05, 0, -1.0f);
  run(0.05, 0, 0.0f);
  run(0.05, 0, -1.0f);
  t_seek = wall();
  while (V.seek_pending && wall() - t_seek < 10)
    run(0.05, 0, 0);
  printf("  -20 s from %.1f: at %.1f after %.2f s\n", before, shown(), wall() - t_seek);
  check(shown() < before - 18 && shown() > before - 22, "two pushes left go 20 s back");

  /* a long video: two minutes on, past what is downloaded -- that part is
   * fetched first */
  if (V.duration > 170) {
    before = shown();
    int64_t had = V.src[0].have;
    for (int i = 0; i < 12; i++) {
      run(0.03, 0, 1.0f);
      run(0.03, 0, 0.0f);
    }
    t_seek = wall();
    while (V.seek_pending && wall() - t_seek < 30)
      run(0.05, 0, 0);
    printf("  +120 s from %.1f: at %.1f after %.2f s (%lld of %lld bytes were in)\n", before, shown(),
           wall() - t_seek, (long long)had, (long long)V.src[0].size);
    check(shown() > before + 115 && shown() < before + 125, "a jump of two minutes, ahead of the download");
    a = shown();
    run(3, 0, 0);
    check(shown() - a > 2.4, "and it plays on from there");
  }

  /* paused: the picture stands, a seek still moves it, and it stays paused */
  run(1, 0, 0);
  run(0.1, HidNpadButton_A, 0);
  check(V.state == ABS_VS_PAUSED, "A pauses");
  a = shown();
  run(1.5, 0, 0);
  check(shown() == a, "paused: the picture stands");
  run(0.05, 0, 1.0f);
  t_seek = wall();
  while (V.seek_pending && wall() - t_seek < 15)
    run(0.05, 0, 0);
  run(0.5, 0, 0);
  printf("  paused +10 s from %.1f: at %.1f\n", a, shown());
  check(shown() > a + 9 && V.state == ABS_VS_PAUSED, "a seek while paused shows the new place, still paused");
  b = shown();
  run(0.1, HidNpadButton_A, 0);
  run(2, 0, 0);
  check(V.state == ABS_VS_PLAYING && shown() > b + 1.5, "A again: on from there");

  printf("  %d frames drawn, %d pictures uploaded, %d shown, %d skipped late, %d waits\n", g_draws, g_uploads,
         V.shown_count, V.dropped, V.stalls);
  check(g_uploads <= V.shown_count + 2, "a picture is uploaded once");
  check(g_bars > 0 && g_spinner >= 0, "the time bar showed");
  run(0.1, HidNpadButton_B, 0);
  run(0.1, 0, 0);
  check(V.state == ABS_VS_IDLE, "B stops");
  printf(failures ? "%d FAILED\n" : "ALL OK\n", failures);
  return failures ? 1 : 0;
}
