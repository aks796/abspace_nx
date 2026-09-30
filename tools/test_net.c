/* test_net.c -- the port's HTTP/TLS client and YouTube resolver on a PC:
 * resolves a video and fetches its streams as abs_video.c does (1 MiB ranges
 * for the split 720p files, one request for the 360p file).
 *   cc -I source -I <mbedtls>/include -I tools/mbedtls \
 *      '-DMBEDTLS_USER_CONFIG_FILE=<abs_mbedtls_user_config.h>' \
 *      tools/test_net.c source/abs_net.c source/abs_json.c source/abs_yt.c <libmbedtls...>
 *   ./a.out CL15siZ2pv4 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "abs_net.h"

static double now(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

static long long fetch_ranges(const char *url, long long total, const char *path) {
  FILE *f = fopen(path, "wb");
  char *buf = malloc(1 << 20);
  long long got = 0;
  double t0 = now();
  while (got < total) {
    long long end = got + (1 << 20) - 1;
    if (end >= total)
      end = total - 1;
    size_t ul = strlen(url) + 64;
    char *u = malloc(ul);
    snprintf(u, ul, "%s&range=%lld-%lld", url, got, end);
    int status = 0;
    AbsHttp *h = abs_http_open("GET", u, "User-Agent: com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip\r\n",
                               NULL, 0, &status);
    free(u);
    if (!h || (status != 200 && status != 206)) {
      printf("  range at %lld: HTTP %d\n", got, status);
      abs_http_close(h);
      break;
    }
    int r;
    while ((r = abs_http_read(h, buf, 1 << 20)) > 0) {
      fwrite(buf, 1, (size_t)r, f);
      got += r;
    }
    abs_http_close(h);
    if (r < 0) {
      printf("  read error at %lld\n", got);
      break;
    }
  }
  fclose(f);
  free(buf);
  printf("  %s: %lld of %lld bytes in %.1f s\n", path, got, total, now() - t0);
  return got;
}

int main(int argc, char **argv) {
  const char *id = argc > 1 ? argv[1] : "CL15siZ2pv4";
  printf("online: %d\n", abs_net_online());
  AbsYtStreams s;
  char err[256];
  double t0 = now();
  if (abs_yt_resolve(id, &s, err, sizeof err)) {
    printf("resolve failed: %s\n", err);
    return 1;
  }
  printf("resolved in %.1f s: %dp video %lld B, audio %lld B, 360p %s\n", now() - t0, s.height,
         (long long)s.video_len, (long long)s.audio_len, s.prog_url ? "yes" : "no");
  int ok = 1;
  if (s.video_url) {
    ok &= fetch_ranges(s.video_url, s.video_len, "/tmp/abs_v.mp4") == s.video_len;
    ok &= fetch_ranges(s.audio_url, s.audio_len, "/tmp/abs_a.m4a") == s.audio_len;
  }
  if (s.prog_url) {
    int status = 0;
    double t1 = now();
    AbsHttp *h = abs_http_open("GET", s.prog_url, NULL, NULL, 0, &status);
    long long n = 0;
    char buf[65536];
    int r;
    while (h && (r = abs_http_read(h, buf, sizeof buf)) > 0)
      n += r;
    printf("  360p: HTTP %d, %lld bytes (Content-Length %lld) in %.1f s\n", status, n,
           (long long)abs_http_length(h), now() - t1);
    abs_http_close(h);
  }
  abs_yt_free(&s);
  printf(ok ? "OK\n" : "FAILED\n");
  return ok ? 0 : 1;
}
