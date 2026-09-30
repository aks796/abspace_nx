/* abs_net.h -- the network, for streaming the links' videos: the console's
 * sockets, TLS (mbedTLS), a small HTTP/1.1 client (abs_net.c); a JSON reader
 * (abs_json.c); YouTube's player API (abs_yt.c). Builds on a PC too (POSIX
 * sockets), for testing the requests. MIT. */
#ifndef ABS_NET_H
#define ABS_NET_H
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------- abs_net.c */
/* Starts the sockets the first time; 1 when the console is on the internet. */
int abs_net_online(void);

typedef struct AbsHttp AbsHttp;
/* A request (GET or POST), redirects followed. extra_headers: "Name: value\r\n"
 * lines, or NULL. Returns NULL on a connection failure; *status is the HTTP
 * status (or 0). */
AbsHttp *abs_http_open(const char *method, const char *url, const char *extra_headers, const void *body,
                       size_t body_len, int *status);
int64_t abs_http_length(const AbsHttp *h); /* Content-Length, -1 if not known */
/* The body: bytes read (0 at its end, < 0 on an error). */
int abs_http_read(AbsHttp *h, void *buf, int n);
void abs_http_close(AbsHttp *h);
/* Breaks off every request in progress (their reads fail at once). */
void abs_net_abort_all(void);
/* The whole body of a request into a malloc()ed, 0-terminated buffer. */
char *abs_http_fetch(const char *method, const char *url, const char *extra_headers, const char *body,
                     size_t *len, int *status);

/* ------------------------------------------------------------- abs_json.c */
enum { ABS_J_NULL, ABS_J_BOOL, ABS_J_NUM, ABS_J_STR, ABS_J_ARR, ABS_J_OBJ };
typedef struct AbsJson {
  int t;
  double num;
  char *s;
  int n, cap;
  struct AbsJson **items;
  char **keys; /* objects */
} AbsJson;
AbsJson *abs_json_parse(const char *text, size_t len);
void abs_json_free(AbsJson *v);
AbsJson *abs_json_get(const AbsJson *v, const char *key);
AbsJson *abs_json_at(const AbsJson *v, int i);
const char *abs_json_str(const AbsJson *v);
double abs_json_num(const AbsJson *v, double def); /* numbers, and numbers in strings */

/* --------------------------------------------------------------- abs_yt.c */
typedef struct {
  char *video_url, *audio_url; /* 720p (or the best under it) H.264 + AAC, fetched in ranges */
  int64_t video_len, audio_len;
  int height;
  char *prog_url;              /* 360p H.264 + AAC in one file (itag 18): the fallback */
} AbsYtStreams;
/* The streams of a YouTube video (its 11-character id). 0 on success; else err. */
int abs_yt_resolve(const char *video_id, AbsYtStreams *out, char *err, size_t errsz);
void abs_yt_free(AbsYtStreams *s);

#endif
