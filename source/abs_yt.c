/* abs_yt.c -- a YouTube video's streams, for the links' popups.
 *
 * YouTube's player API (youtubei/v1/player) asked as the Android app does
 * (the same request as switch-newpipe's YouTubeResolver makes first). Its
 * answer lists the formats with direct URLs, and for the videos these links
 * point to (the Rocket Science Show, NASA's, Rovio's, all "made for kids" or
 * plain uploads) that works without a token or deciphering:
 *   - 720p H.264 video (itag 136, or the best AVC under it) and AAC audio
 *     (itag 140), separate files; abs_video.c fetches them in 1 MiB ranges
 *     ("&range=a-b": whole requests are throttled to about twice real time,
 *     ranges are not);
 *   - 360p H.264 + AAC in one file (itag 18, "ratebypass=yes"): the fallback.
 * Checked against YouTube in September 2026 (tools/test_net.c). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_net.h"

#ifdef __SWITCH__
#include "util.h"
#define YLOG(...) debugPrintf("[yt] " __VA_ARGS__)
#else
#define YLOG(...) fprintf(stderr, "[yt] " __VA_ARGS__)
#endif

#define API "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"
#define ANDROID_UA "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip"

static char *dup_str(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if (d)
    memcpy(d, s, n);
  return d;
}

void abs_yt_free(AbsYtStreams *s) {
  free(s->video_url);
  free(s->audio_url);
  free(s->prog_url);
  memset(s, 0, sizeof *s);
}

static int valid_id(const char *id) {
  size_t n = strlen(id);
  if (n != 11)
    return 0;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
      return 0;
  }
  return 1;
}

int abs_yt_resolve(const char *video_id, AbsYtStreams *out, char *err, size_t errsz) {
  memset(out, 0, sizeof *out);
  if (!valid_id(video_id)) {
    snprintf(err, errsz, "not a video id");
    return -1;
  }
  char body[512];
  snprintf(body, sizeof body,
           "{\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"context\":{\"client\":"
           "{\"clientName\":\"ANDROID\",\"clientVersion\":\"20.10.38\",\"androidSdkVersion\":30,"
           "\"hl\":\"en\",\"gl\":\"US\"}}}",
           video_id);
  const char *headers = "Content-Type: application/json\r\n"
                        "User-Agent: " ANDROID_UA "\r\n"
                        "X-Youtube-Client-Name: 3\r\n"
                        "X-Youtube-Client-Version: 20.10.38\r\n"
                        "Origin: https://www.youtube.com\r\n";
  int status = 0;
  size_t len = 0;
  char *text = abs_http_fetch("POST", API, headers, body, &len, &status);
  if (!text || status != 200) {
    snprintf(err, errsz, "the player API did not answer (HTTP %d)", status);
    free(text);
    return -1;
  }
  AbsJson *root = abs_json_parse(text, len);
  free(text);
  if (!root) {
    snprintf(err, errsz, "the player API's answer could not be read");
    return -1;
  }
  AbsJson *ps = abs_json_get(root, "playabilityStatus");
  const char *st = abs_json_str(abs_json_get(ps, "status"));
  if (st && strcmp(st, "OK")) {
    const char *why = abs_json_str(abs_json_get(ps, "reason"));
    snprintf(err, errsz, "%s%s%s", st, why ? ": " : "", why ? why : "");
    abs_json_free(root);
    return -1;
  }
  AbsJson *sd = abs_json_get(root, "streamingData");
  /* 360p in one file */
  AbsJson *formats = abs_json_get(sd, "formats");
  for (int i = 0; formats && i < formats->n; i++) {
    AbsJson *f = abs_json_at(formats, i);
    if ((int)abs_json_num(abs_json_get(f, "itag"), 0) == 18 && abs_json_str(abs_json_get(f, "url")))
      out->prog_url = dup_str(abs_json_str(abs_json_get(f, "url")));
  }
  /* the best H.264 video up to 720p, and AAC-LC audio */
  AbsJson *ad = abs_json_get(sd, "adaptiveFormats");
  int best_h = 0;
  for (int i = 0; ad && i < ad->n; i++) {
    AbsJson *f = abs_json_at(ad, i);
    const char *mime = abs_json_str(abs_json_get(f, "mimeType"));
    const char *u = abs_json_str(abs_json_get(f, "url"));
    if (!mime || !u)
      continue;
    int h = (int)abs_json_num(abs_json_get(f, "height"), 0);
    int64_t clen = (int64_t)abs_json_num(abs_json_get(f, "contentLength"), -1);
    if (strstr(mime, "video/mp4") && strstr(mime, "avc1") && h <= 720 && h > best_h && clen > 0) {
      free(out->video_url);
      out->video_url = dup_str(u);
      out->video_len = clen;
      out->height = best_h = h;
    } else if (strstr(mime, "audio/mp4") && strstr(mime, "mp4a.40.2") && clen > 0 && !out->audio_url) {
      out->audio_url = dup_str(u);
      out->audio_len = clen;
    }
  }
  abs_json_free(root);
  if (!(out->video_url && out->audio_url)) {
    free(out->video_url), free(out->audio_url);
    out->video_url = out->audio_url = NULL;
  }
  if (!out->video_url && !out->prog_url) {
    snprintf(err, errsz, "no playable format in the answer");
    return -1;
  }
  YLOG("%s: %s%dp split%s\n", video_id, out->video_url ? "" : "no ", out->height,
       out->prog_url ? ", 360p single file" : "");
  return 0;
}
