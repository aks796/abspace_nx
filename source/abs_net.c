/* abs_net.c -- HTTP(S) for the links' videos (abs_yt.c, abs_video.c).
 *
 * The console's own BSD sockets (libnx bsd:u, started the first time: the
 * same setup PvZ Touch's port runs on hardware), DNS through them, TLS 1.2
 * with mbedTLS (tools/mbedtls; certificates are not checked, as
 * switch-newpipe does not either: nothing is sent but a public video's id),
 * and HTTP/1.1: one connection per request ("Connection: close"),
 * Content-Length or chunked bodies, redirects followed. Blocking, with send
 * and receive timeouts; used from the video's own threads, never the game's.
 * nifm says whether the console is on the internet at all.
 *
 * Built on a PC too (POSIX sockets), where tools/test_net.c runs the same
 * requests against YouTube. MIT.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <psa/crypto.h>

#include "abs_net.h"

#ifdef __SWITCH__
#include <switch.h>
#include "util.h"
#define NLOG(...) debugPrintf("[net] " __VA_ARGS__)
#else
#include <pthread.h>
#define NLOG(...) fprintf(stderr, "[net] " __VA_ARGS__)
#endif

/* ------------------------------------------------------------ the sockets */
#ifdef __SWITCH__
static Mutex g_lock;
#define LOCK() mutexLock(&g_lock)
#define UNLOCK() mutexUnlock(&g_lock)
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&g_lock)
#define UNLOCK() pthread_mutex_unlock(&g_lock)
#endif

static int g_sockets;   /* 0 not tried, 1 up, -1 failed */
#ifdef __SWITCH__
static int g_nifm;
#endif

/* the requests open now, so that a stopped video can break them off */
#define MAXOPEN 8
static int g_open_fd[MAXOPEN];
static int g_open_n;
static volatile unsigned g_abort_seq;

static void track(int fd, int on) {
  LOCK();
  if (on && g_open_n < MAXOPEN)
    g_open_fd[g_open_n++] = fd;
  else if (!on)
    for (int i = 0; i < g_open_n; i++)
      if (g_open_fd[i] == fd) {
        g_open_fd[i] = g_open_fd[--g_open_n];
        break;
      }
  UNLOCK();
}

void abs_net_abort_all(void) {
  LOCK();
  g_abort_seq++;
  for (int i = 0; i < g_open_n; i++)
    shutdown(g_open_fd[i], SHUT_RDWR);
  UNLOCK();
}

static int sockets_up(void) {
#ifdef __SWITCH__
  if (!g_sockets) {
    SocketInitConfig cfg = *socketGetDefaultInitConfig();
    cfg.tcp_tx_buf_size = 0x8000;
    cfg.tcp_rx_buf_size = 0x40000;
    cfg.tcp_tx_buf_max_size = 0x40000;
    cfg.tcp_rx_buf_max_size = 0x100000;
    cfg.sb_efficiency = 4;
    cfg.num_bsd_sessions = 4;
    cfg.bsd_service_type = BsdServiceType_User;
    Result rc = socketInitialize(&cfg);
    g_sockets = R_SUCCEEDED(rc) ? 1 : -1;
    g_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    NLOG("sockets %s (0x%x), nifm %s\n", g_sockets > 0 ? "up" : "failed", (unsigned)rc,
         g_nifm ? "up" : "unavailable");
  }
#else
  g_sockets = 1;
#endif
  return g_sockets > 0;
}

int abs_net_online(void) {
  LOCK();
  int ok = sockets_up();
  UNLOCK();
  if (!ok)
    return 0;
#ifdef __SWITCH__
  if (!g_nifm)
    return 1; /* no way to ask: try */
  NifmInternetConnectionType type;
  u32 strength = 0;
  NifmInternetConnectionStatus status;
  if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
    return 0;
  return status == NifmInternetConnectionStatus_Connected;
#else
  return 1;
#endif
}

/* ------------------------------------------------------------------ TLS */
static int g_tls;       /* 0 not set up, 1 ready, -1 failed */
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_ssl_config g_conf;

/* MBEDTLS_ENTROPY_HARDWARE_ALT: the console's random numbers */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen);
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen) {
  (void)data;
#ifdef __SWITCH__
  randomGet(out, len);
#else
  arc4random_buf(out, len);
#endif
  *olen = len;
  return 0;
}

static int tls_up(void) {
  if (g_tls)
    return g_tls > 0;
  g_tls = -1;
  psa_status_t ps = psa_crypto_init();
  if (ps != PSA_SUCCESS)
    NLOG("psa_crypto_init: %d (TLS 1.2 goes on without it)\n", (int)ps);
  mbedtls_entropy_init(&g_entropy);
  mbedtls_ctr_drbg_init(&g_drbg);
  mbedtls_ssl_config_init(&g_conf);
  static const unsigned char pers[] = "abspace_nx video";
  int r = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy, pers, sizeof pers - 1);
  if (!r)
    r = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (r) {
    NLOG("TLS setup failed: -0x%x\n", (unsigned)-r);
    return 0;
  }
  mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_drbg);
  g_tls = 1;
  return 1;
}

/* ------------------------------------------------------------ a request */
struct AbsHttp {
  int fd, tls;
  mbedtls_ssl_context ssl;
  unsigned char buf[16384];
  int buf_len, buf_pos;
  int64_t length, left;   /* Content-Length (-1: none), bytes of it left */
  int chunked;
  int64_t chunk_left;     /* of the current chunk; -1 before the first */
  int eof;
  unsigned seq;           /* g_abort_seq when it opened: another means broken off */
};

static int bio_send(void *ctx, const unsigned char *b, size_t n) {
  int fd = *(int *)ctx;
  ssize_t r = send(fd, b, n, 0);
  if (r < 0)
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
  return (int)r;
}

static int bio_recv(void *ctx, unsigned char *b, size_t n) {
  int fd = *(int *)ctx;
  ssize_t r = recv(fd, b, n, 0);
  if (r < 0)
    return MBEDTLS_ERR_NET_RECV_FAILED; /* includes the receive timeout */
  return (int)r;
}

static int raw_write(AbsHttp *h, const void *p, size_t n) {
  const unsigned char *b = p;
  while (n) {
    int r = h->tls ? mbedtls_ssl_write(&h->ssl, b, n) : (int)send(h->fd, b, n, 0);
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ)
      continue;
    if (r <= 0)
      return -1;
    b += r, n -= (size_t)r;
  }
  return 0;
}

static int raw_read(AbsHttp *h, void *p, size_t n) {
  for (;;) {
    int r = h->tls ? mbedtls_ssl_read(&h->ssl, p, n) : (int)recv(h->fd, p, n, 0);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
      continue;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0)
      return 0;
    return r < 0 ? -1 : r;
  }
}

/* buffered: refill when empty */
static int fill(AbsHttp *h) {
  if (h->buf_pos < h->buf_len)
    return h->buf_len - h->buf_pos;
  int r = raw_read(h, h->buf, sizeof h->buf);
  if (r <= 0)
    return r;
  h->buf_pos = 0, h->buf_len = r;
  return r;
}

static int get_byte(AbsHttp *h) {
  if (fill(h) <= 0)
    return -1;
  return h->buf[h->buf_pos++];
}

/* a line without its CR LF */
static int get_line(AbsHttp *h, char *out, int cap) {
  int n = 0;
  for (;;) {
    int c = get_byte(h);
    if (c < 0)
      return n ? n : -1;
    if (c == '\n')
      break;
    if (c != '\r' && n + 1 < cap)
      out[n++] = (char)c;
  }
  out[n] = 0;
  return n;
}

typedef struct {
  char scheme[8], host[256], path[4096];
  int port;
} Url;

static int parse_url(const char *u, Url *o) {
  const char *s = strstr(u, "://");
  if (!s || s - u >= (int)sizeof o->scheme)
    return 0;
  memcpy(o->scheme, u, (size_t)(s - u));
  o->scheme[s - u] = 0;
  s += 3;
  const char *e = s + strcspn(s, "/?#");
  const char *colon = memchr(s, ':', (size_t)(e - s));
  const char *he = colon ? colon : e;
  if (he - s <= 0 || he - s >= (int)sizeof o->host)
    return 0;
  memcpy(o->host, s, (size_t)(he - s));
  o->host[he - s] = 0;
  o->port = colon ? atoi(colon + 1) : (!strcasecmp(o->scheme, "https") ? 443 : 80);
  const char *path = *e == '/' ? e : NULL;
  if (path) {
    size_t n = strcspn(path, "#");
    if (n >= sizeof o->path)
      return 0;
    memcpy(o->path, path, n);
    o->path[n] = 0;
  } else if (*e == '?') {
    snprintf(o->path, sizeof o->path, "/%s", e);
  } else {
    strcpy(o->path, "/");
  }
  return 1;
}

/* connect, giving up after 8 s or when the requests are broken off */
static int connect_to(const Url *u) {
  const unsigned seq = g_abort_seq;
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port[8];
  snprintf(port, sizeof port, "%d", u->port);
  int e = getaddrinfo(u->host, port, &hints, &res);
  if (e || !res) {
    NLOG("DNS %s: failed (%d)\n", u->host, e);
    return -1;
  }
  int fd = -1;
  for (struct addrinfo *a = res; a && seq == g_abort_seq; a = a->ai_next) {
    fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0)
      continue;
    struct timeval tv = {12, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    int one = 1; /* a PC: a write to a broken-off connection fails, it does not kill */
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int ok = connect(fd, a->ai_addr, a->ai_addrlen) == 0;
    if (!ok && errno == EINPROGRESS) {
      for (int waited = 0; waited < 8000 && seq == g_abort_seq; waited += 200) {
        struct pollfd pf = {fd, POLLOUT, 0};
        int r = poll(&pf, 1, 200);
        if (r > 0) {
          int err = 0;
          socklen_t el = sizeof err;
          ok = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0;
          break;
        }
        if (r < 0)
          break;
      }
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    if (ok && seq == g_abort_seq)
      break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0)
    NLOG("connect %s:%d failed (%d)\n", u->host, u->port, errno);
  return fd;
}

static void destroy(AbsHttp *h) {
  if (!h)
    return;
  if (h->tls) {
    if (h->seq == g_abort_seq) /* a broken-off connection gets no goodbye */
      mbedtls_ssl_close_notify(&h->ssl);
    mbedtls_ssl_free(&h->ssl);
  }
  if (h->fd >= 0) {
    track(h->fd, 0);
    close(h->fd);
  }
  free(h);
}

/* one request, no redirects: the response's headers read */
static AbsHttp *request(const char *method, const Url *u, const char *extra, const void *body, size_t body_len,
                        int *status, char *location, size_t loc_cap) {
  AbsHttp *h = calloc(1, sizeof *h);
  if (!h)
    return NULL;
  h->fd = -1;
  h->length = -1;
  h->chunk_left = -1;
  *status = 0;
  if (location)
    location[0] = 0;
  h->seq = g_abort_seq;
  h->fd = connect_to(u);
  if (h->fd < 0)
    goto fail;
  track(h->fd, 1);
  if (h->seq != g_abort_seq) /* broken off before it was tracked */
    goto fail;
  if (!strcasecmp(u->scheme, "https")) {
    LOCK();
    int ok = tls_up();
    UNLOCK();
    if (!ok)
      goto fail;
    mbedtls_ssl_init(&h->ssl);
    h->tls = 1;
    int r = mbedtls_ssl_setup(&h->ssl, &g_conf);
    if (!r)
      r = mbedtls_ssl_set_hostname(&h->ssl, u->host);
    if (r)
      goto fail;
    mbedtls_ssl_set_bio(&h->ssl, &h->fd, bio_send, bio_recv, NULL);
    while ((r = mbedtls_ssl_handshake(&h->ssl)) != 0) {
      if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
        NLOG("TLS with %s failed: -0x%x\n", u->host, (unsigned)-r);
        goto fail;
      }
    }
  }
  char head[5120];
  int n = snprintf(head, sizeof head,
                   "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept-Encoding: identity\r\n%s", method,
                   u->path, u->host, extra ? extra : "");
  if (body)
    n += snprintf(head + n, sizeof head - (size_t)n, "Content-Length: %u\r\n", (unsigned)body_len);
  n += snprintf(head + n, sizeof head - (size_t)n, "\r\n");
  if (n >= (int)sizeof head - 1 || raw_write(h, head, (size_t)n) || (body && raw_write(h, body, body_len)))
    goto fail;
  /* the status line and the headers */
  char line[4096];
  if (get_line(h, line, sizeof line) < 12 || strncmp(line, "HTTP/1.", 7))
    goto fail;
  *status = atoi(line + 9);
  while (get_line(h, line, sizeof line) > 0) {
    char *colon = strchr(line, ':');
    if (!colon)
      continue;
    *colon = 0;
    const char *v = colon + 1;
    while (*v == ' ')
      v++;
    if (!strcasecmp(line, "content-length"))
      h->length = strtoll(v, NULL, 10);
    else if (!strcasecmp(line, "transfer-encoding") && strstr(v, "chunked"))
      h->chunked = 1;
    else if (!strcasecmp(line, "location") && location)
      snprintf(location, loc_cap, "%s", v);
  }
  h->left = h->length;
  return h;
fail:
  destroy(h);
  return NULL;
}

AbsHttp *abs_http_open(const char *method, const char *url, const char *extra, const void *body, size_t body_len,
                       int *status) {
  int dummy;
  if (!status)
    status = &dummy;
  *status = 0;
  LOCK();
  int ok = sockets_up();
  UNLOCK();
  if (!ok)
    return NULL;
  char cur[8192], loc[8192];
  snprintf(cur, sizeof cur, "%s", url);
  for (int hop = 0; hop < 6; hop++) {
    Url u;
    if (!parse_url(cur, &u)) {
      NLOG("not a URL: %.80s\n", cur);
      return NULL;
    }
    AbsHttp *h = request(method, &u, extra, body, body_len, status, loc, sizeof loc);
    if (!h)
      return NULL;
    if (*status >= 300 && *status < 400 && loc[0]) {
      destroy(h);
      if (strstr(loc, "://")) {
        snprintf(cur, sizeof cur, "%s", loc);
      } else {
        char base[300];
        snprintf(base, sizeof base, "%s://%s:%d", u.scheme, u.host, u.port);
        snprintf(cur, sizeof cur, "%s%s%s", base, loc[0] == '/' ? "" : "/", loc);
      }
      if (*status != 307 && *status != 308)
        method = "GET", body = NULL, body_len = 0;
      continue;
    }
    return h;
  }
  return NULL;
}

int64_t abs_http_length(const AbsHttp *h) { return h ? h->length : -1; }

int abs_http_read(AbsHttp *h, void *out, int n) {
  if (!h || h->eof || n <= 0)
    return 0;
  if (h->chunked) {
    if (h->chunk_left <= 0) {
      char line[64];
      if (h->chunk_left == 0 && get_line(h, line, sizeof line) < 0) /* the CR LF after a chunk */
        return -1;
      if (get_line(h, line, sizeof line) < 0)
        return -1;
      h->chunk_left = strtoll(line, NULL, 16);
      if (h->chunk_left <= 0) {
        h->eof = 1;
        return 0;
      }
    }
    if (n > h->chunk_left)
      n = (int)h->chunk_left;
  } else if (h->length >= 0) {
    if (h->left <= 0) {
      h->eof = 1;
      return 0;
    }
    if (n > h->left)
      n = (int)h->left;
  }
  int got;
  if (h->buf_pos < h->buf_len) {
    got = h->buf_len - h->buf_pos;
    if (got > n)
      got = n;
    memcpy(out, h->buf + h->buf_pos, (size_t)got);
    h->buf_pos += got;
  } else {
    got = raw_read(h, out, (size_t)n);
    if (got <= 0) {
      h->eof = 1;
      return (h->length >= 0 && h->left > 0) || h->chunked ? -1 : 0;
    }
  }
  if (h->chunked)
    h->chunk_left -= got;
  else if (h->length >= 0)
    h->left -= got;
  return got;
}

void abs_http_close(AbsHttp *h) { destroy(h); }

char *abs_http_fetch(const char *method, const char *url, const char *extra, const char *body, size_t *len,
                     int *status) {
  AbsHttp *h = abs_http_open(method, url, extra, body, body ? strlen(body) : 0, status);
  if (!h)
    return NULL;
  size_t cap = h->length > 0 ? (size_t)h->length + 1 : 65536, n = 0;
  char *out = malloc(cap);
  while (out) {
    if (n + 16384 + 1 > cap) {
      char *p = realloc(out, cap * 2);
      if (!p) {
        free(out);
        out = NULL;
        break;
      }
      out = p, cap *= 2;
    }
    int r = abs_http_read(h, out + n, 16384);
    if (r < 0) {
      free(out);
      out = NULL;
      break;
    }
    if (r == 0)
      break;
    n += (size_t)r;
  }
  abs_http_close(h);
  if (out) {
    out[n] = 0;
    if (len)
      *len = n;
  }
  return out;
}
