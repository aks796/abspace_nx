/* abs_assets.c -- libandroid's AAssetManager over the user's APK.
 *
 * Fusion's io::BundleFileSystem reads every game file (images, sounds, the
 * encrypted Lua, levels, fonts, assets.list) through the NDK asset manager:
 * AAssetManager_fromJava(Context.getAssets()), then AAssetManager_open,
 * AAsset_getLength64, AAsset_getBuffer (the whole file at once) and
 * AAsset_close. Nothing is ever extracted from the APK: its central
 * directory is indexed once, and each open reads the entry's bytes through
 * the APK block cache (dcr_apkcache.c: RAM, filled from the SD card on first
 * use) and inflates them if they are deflated.
 *
 * Mods: when <root>/mods/ exists, a file there (mods/data/images/..., the
 * same path as under the APK's assets/) is served instead of the APK's. The
 * folder is checked once at start-up, so without it opens never touch the
 * SD card. MIT.
 */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "abs.h"
#include "util.h"

const char *dcr_game_root(void);                            /* main.c */
ssize_t dcr_apkcache_read(uint64_t off, void *buf, size_t n); /* dcr_apkcache.c */

typedef struct {
  uint32_t hash;
  uint32_t name_off;   /* into g_names: the path under assets/ */
  uint32_t local_off;  /* local file header */
  uint32_t csize, usize;
  uint16_t method;
} Entry;

static Entry *g_ent;
static int g_nent;
static char *g_names;
static uint32_t *g_tab; /* open addressing: entry index + 1 */
static uint32_t g_mask;
static FILE *g_apk;     /* fallback reader when the cache is unavailable */
static Mutex g_apk_lock;
static int g_mods;      /* <root>/mods/ exists */
static uint64_t g_opens, g_bytes, g_misses;

static uint32_t fnv(const char *s) {
  uint32_t h = 2166136261u;
  while (*s)
    h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int apk_read(uint64_t off, void *buf, size_t n) {
  ssize_t r = dcr_apkcache_read(off, buf, n);
  if (r == (ssize_t)n)
    return 0;
  mutexLock(&g_apk_lock);
  int ok = g_apk && fseek(g_apk, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, g_apk) == n;
  mutexUnlock(&g_apk_lock);
  return ok ? 0 : -1;
}

int abs_assets_init(const char *apk_path) {
  g_apk = fopen(apk_path, "rb");
  if (!g_apk) {
    debugPrintf("[assets] cannot open %s\n", apk_path);
    return -1;
  }
  fseek(g_apk, 0, SEEK_END);
  long size = ftell(g_apk);
  /* the end of central directory record: in the last 64 KB + 22 bytes */
  long tail = size < 0x10000 + 22 ? size : 0x10000 + 22;
  uint8_t *t = malloc((size_t)tail);
  if (!t || apk_read((uint64_t)(size - tail), t, (size_t)tail)) {
    free(t);
    return -1;
  }
  long eocd = -1;
  for (long i = tail - 22; i >= 0; i--)
    if (rd32(t + i) == 0x06054b50u) {
      eocd = i;
      break;
    }
  if (eocd < 0) {
    free(t);
    debugPrintf("[assets] %s: not a zip (no end of central directory)\n", apk_path);
    return -1;
  }
  uint32_t n_total = rd16(t + eocd + 10), cd_size = rd32(t + eocd + 12), cd_off = rd32(t + eocd + 16);
  free(t);
  uint8_t *cd = malloc(cd_size);
  if (!cd || apk_read(cd_off, cd, cd_size)) {
    free(cd);
    return -1;
  }
  g_ent = calloc(n_total, sizeof *g_ent);
  g_names = malloc(cd_size); /* the names fit in the directory they come from */
  uint32_t names_len = 0;
  for (uint32_t i = 0, p = 0; i < n_total && p + 46 <= cd_size; i++) {
    if (rd32(cd + p) != 0x02014b50u)
      break;
    uint16_t method = rd16(cd + p + 10), nlen = rd16(cd + p + 28), xlen = rd16(cd + p + 30),
             clen = rd16(cd + p + 32);
    uint32_t csize = rd32(cd + p + 20), usize = rd32(cd + p + 24), loff = rd32(cd + p + 42);
    const char *name = (const char *)cd + p + 46;
    p += 46 + nlen + xlen + clen;
    if (nlen <= 7 || strncmp(name, "assets/", 7) || name[nlen - 1] == '/')
      continue;
    Entry *e = &g_ent[g_nent++];
    e->name_off = names_len;
    memcpy(g_names + names_len, name + 7, nlen - 7u);
    names_len += nlen - 7u;
    g_names[names_len++] = 0;
    e->local_off = loff;
    e->csize = csize;
    e->usize = usize;
    e->method = method;
    e->hash = fnv(g_names + e->name_off);
  }
  free(cd);
  uint32_t cap = 16;
  while (cap < (uint32_t)g_nent * 2)
    cap <<= 1;
  g_tab = calloc(cap, sizeof *g_tab);
  g_mask = cap - 1;
  for (int i = 0; i < g_nent; i++) {
    uint32_t h = g_ent[i].hash & g_mask;
    while (g_tab[h])
      h = (h + 1) & g_mask;
    g_tab[h] = (uint32_t)i + 1;
  }
  char mods[300];
  snprintf(mods, sizeof mods, "%s/mods", dcr_game_root());
  struct stat st;
  g_mods = stat(mods, &st) == 0 && S_ISDIR(st.st_mode);
  debugPrintf("[assets] %d files under assets/ in the APK%s\n", g_nent,
              g_mods ? "; mods/ folder present: its files replace the APK's" : "");
  return g_nent > 0 ? 0 : -1;
}

static const Entry *lookup(const char *name) {
  if (!g_tab)
    return NULL;
  uint32_t hv = fnv(name);
  for (uint32_t h = hv & g_mask, k; (k = g_tab[h]) != 0; h = (h + 1) & g_mask) {
    const Entry *e = &g_ent[k - 1];
    if (e->hash == hv && !strcmp(g_names + e->name_off, name))
      return e;
  }
  return NULL;
}

/* "./data/x", "/data/x", "assets/data/x" -> "data/x" */
static const char *norm(const char *name) {
  for (;;) {
    if (name[0] == '/')
      name++;
    else if (name[0] == '.' && name[1] == '/')
      name += 2;
    else
      break;
  }
  return name;
}

static void *read_mod(const char *name, size_t *len) {
  char path[512];
  snprintf(path, sizeof path, "%s/mods/%s", dcr_game_root(), name);
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) {
    *len = (size_t)n;
    static unsigned logged;
    if (logged++ < 32)
      debugPrintf("[assets] %s: from mods/\n", name);
  }
  return buf;
}

void abs_assets_each(const char *prefix, void (*cb)(const char *name, void *ud), void *ud) {
  size_t pl = strlen(prefix);
  for (int i = 0; i < g_nent; i++) {
    const char *n = g_names + g_ent[i].name_off;
    if (!strncmp(n, prefix, pl))
      cb(n, ud);
  }
}

void *abs_asset_read(const char *name_in, size_t *len) {
  if (!name_in)
    return NULL;
  const char *name = norm(name_in);
  /* Orbital Escapade's worlds, from the player's copy (abs_orbital.c) */
  if (!strncmp(name, "data/levels/oe_", 15) || !strncmp(name, "data/images/oe/", 15) ||
      !strncmp(name, "data/audio/oe/", 14)) {
    void *o = abs_orbital_read(name, len);
    if (o) {
      g_opens++;
      g_bytes += *len;
    } else {
      g_misses++;
      debugPrintf("[orbital] %s: not in orbital/data\n", name);
    }
    return o;
  }
  if (g_mods) {
    void *m = read_mod(name, len);
    if (m)
      return m;
  }
  const Entry *e = lookup(name);
  if (!e && !strncmp(name, "assets/", 7))
    e = lookup(name + 7);
  if (!e) {
    g_misses++;
    return NULL;
  }
  uint8_t lh[30];
  if (apk_read(e->local_off, lh, sizeof lh) || rd32(lh) != 0x04034b50u)
    return NULL;
  uint64_t data = (uint64_t)e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
  uint8_t *out = malloc(e->usize ? e->usize : 1);
  if (!out)
    return NULL;
  if (e->method == 0) {
    if (apk_read(data, out, e->usize)) {
      free(out);
      return NULL;
    }
  } else if (e->method == 8) {
    uint8_t *in = malloc(e->csize ? e->csize : 1);
    size_t got = TINFL_DECOMPRESS_MEM_TO_MEM_FAILED;
    if (in && !apk_read(data, in, e->csize))
      got = tinfl_decompress_mem_to_mem(out, e->usize, in, e->csize, 0);
    free(in);
    if (got != e->usize) {
      debugPrintf("[assets] %s: inflate failed (%u of %u bytes)\n", name, (unsigned)got,
                  (unsigned)e->usize);
      free(out);
      return NULL;
    }
  } else {
    debugPrintf("[assets] %s: compression method %u not supported\n", name, e->method);
    free(out);
    return NULL;
  }
  *len = e->usize;
  g_opens++;
  g_bytes += e->usize;
  return out;
}

void abs_assets_report(void) {
  debugPrintf("[assets] %llu opens, %llu MB read, %llu not found\n", (unsigned long long)g_opens,
              (unsigned long long)(g_bytes >> 20), (unsigned long long)g_misses);
}

/* ================================ the NDK API ============================== */
#define ASSET_MAGIC 0x41535354u /* 'ASST' */
#define AASSET_MODE_UNKNOWN 0

typedef struct {
  uint32_t magic;
  uint8_t *buf;
  size_t len, pos;
} AAsset;

static uint32_t g_manager[4] = {0x4d475231u}; /* the one AAssetManager */

void *b_AAssetManager_fromJava(void *env, void *asset_manager) { return g_manager; }

void *b_AAssetManager_open(void *mgr, const char *filename, int mode) {
  size_t len = 0;
  void *buf = abs_asset_read(filename, &len);
  if (!buf) {
    static unsigned logged;
    if (logged++ < 64)
      debugPrintf("[assets] open(%s): not in the APK\n", filename ? filename : "(null)");
    return NULL;
  }
  AAsset *a = calloc(1, sizeof *a);
  if (!a) {
    free(buf);
    return NULL;
  }
  a->magic = ASSET_MAGIC;
  a->buf = buf;
  a->len = len;
  return a;
}

static AAsset *as_asset(void *p) {
  AAsset *a = p;
  return a && a->magic == ASSET_MAGIC ? a : NULL;
}

void b_AAsset_close(void *p) {
  AAsset *a = as_asset(p);
  if (!a)
    return;
  a->magic = 0;
  free(a->buf);
  free(a);
}

const void *b_AAsset_getBuffer(void *p) {
  AAsset *a = as_asset(p);
  return a ? a->buf : NULL;
}

int64_t b_AAsset_getLength64(void *p) {
  AAsset *a = as_asset(p);
  return a ? (int64_t)a->len : 0;
}

/* The rest of the API, for completeness (dlsym): the engine imports the
 * five above. */
int32_t b_AAsset_getLength(void *p) { return (int32_t)b_AAsset_getLength64(p); }

int b_AAsset_read(void *p, void *buf, size_t n) {
  AAsset *a = as_asset(p);
  if (!a)
    return -1;
  size_t left = a->len - a->pos;
  if (n > left)
    n = left;
  memcpy(buf, a->buf + a->pos, n);
  a->pos += n;
  return (int)n;
}

int64_t b_AAsset_seek64(void *p, int64_t off, int whence) {
  AAsset *a = as_asset(p);
  if (!a)
    return -1;
  int64_t base = whence == 1 ? (int64_t)a->pos : whence == 2 ? (int64_t)a->len : 0;
  int64_t np = base + off;
  if (np < 0 || np > (int64_t)a->len)
    return -1;
  a->pos = (size_t)np;
  return np;
}

int32_t b_AAsset_seek(void *p, int32_t off, int whence) { return (int32_t)b_AAsset_seek64(p, off, whence); }

int64_t b_AAsset_getRemainingLength64(void *p) {
  AAsset *a = as_asset(p);
  return a ? (int64_t)(a->len - a->pos) : 0;
}
