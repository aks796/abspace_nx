/* abs_orbital.c -- Angry Birds Orbital Escapade as two more planets.
 *
 * Orbital Escapade (ShadowBird81, 2025) is a mod of the PC edition of Angry
 * Birds Space 1.4. Its new worlds are two episodes that took the places of
 * Pig Bang and Cold Cuts in its levels/theme1 and levels/theme2: here they
 * are two more planets, Vege-toids and Omelettification, beside the game's
 * own, which stay as they are. Nothing of the mod is in the port: the player
 * copies the mod's data folder to <root>/orbital/data, and this file serves
 * what the game asks of it:
 *
 *   - the levels, as the engine reads every Lua file of its own: AES-256-CBC
 *     (the Android key, zero IV, PKCS#7) over "\x89LZMA\r\n\x1a\n" and an
 *     LZMA stream. The mod's files are Lua source or bytecode, some under the
 *     PC edition's key (as the mod's executable builds it) or the Android
 *     one; they come out plain, then go into that container (the LZMA stream
 *     holds literals only: the size of the file, not its smallness, matters).
 *       data/levels/oe_vegetoids/<name>.lua        levels/theme1/...
 *       data/levels/oe_omelettification/<name>.lua levels/theme2/...
 *     with the levels' own names behind a prefix (OEV_Level1 -> Level1,
 *     LevelComicOEV_1 -> LevelComic1: the scores are kept by name, and the
 *     mod's levels and comics reuse the game's names);
 *   - its sprite sheets (the same KA3D format as the game's, PNG pictures),
 *     each .dat cut down to the sprites the game does not have itself, so an
 *     existing bird or block always looks as it does, and served as OE_<name>
 *     (the game has sheets of the same names): data/images/oe/OE_...
 *   - its sounds:                                    data/audio/oe/...
 *   - its script files, plain, to the controller script (abs_ctl.lua reads
 *     the mod's definitions: birds, blocks, themes, star limits...).
 * MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include <switch.h>

#include "mbedtls/aes.h"

#include "abs.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

/* AES-256 keys: 32 bytes (and the literal's NUL) */
static const unsigned char KEY_ANDROID[33] = "RmgdZ0JenLFgWwkYvCL2lSahFbEhFec4";
/* the PC edition's key, as its executable builds it byte by byte */
static const unsigned char KEY_PC[33] = "mRWgk0JenLFgWwkYvCL2lSahFbEhFec4";

static int g_checked, g_present;
static char g_root[300];
static void idx_start(void);

static const struct {
  const char *folder, *modfolder, *prefix, *boss;
} k_eps[] = {
    {"oe_vegetoids", "theme1", "OEV_", "LevelBossTheme1"},
    {"oe_omelettification", "theme2", "OEO_", "LevelBossTheme2"},
};

/* The mod's data folder (the one beside AngryBirdsOrbitalEscapade.exe), by
 * what it holds: its scripts and the two worlds' level folders. */
static int is_mod_data(const char *dir) {
  char p[500];
  struct stat st;
  snprintf(p, sizeof p, "%s/scripts/birds.lua", dir);
  if (stat(p, &st) != 0)
    return 0;
  for (unsigned e = 0; e < sizeof k_eps / sizeof k_eps[0]; e++) {
    snprintf(p, sizeof p, "%s/levels/%s", dir, k_eps[e].modfolder);
    if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode))
      return 0;
  }
  return 1;
}

/* It may be anywhere in the game folder, under any name, up to three
 * folders down: orbital/data as the README suggests, or the download's own
 * folder copied over whole ("Angry Birds Orbital Escapade/data"). */
static int find_mod_data(const char *dir, int depth, char *out, size_t cap) {
  if (is_mod_data(dir)) {
    snprintf(out, cap, "%s", dir);
    return 1;
  }
  if (depth >= 3)
    return 0;
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  int found = 0;
  struct dirent *e;
  while (!found && (e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue; /* also a Mac's "._" files */
    char p[400];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
      found = find_mod_data(p, depth + 1, out, cap);
  }
  closedir(d);
  return found;
}

int abs_orbital_present(void) {
  if (!g_checked) {
    g_checked = 1;
    char first[sizeof g_root];
    snprintf(first, sizeof first, "%s/orbital/data", dcr_game_root());
    g_present = is_mod_data(first) ? (snprintf(g_root, sizeof g_root, "%s", first), 1)
                                   : find_mod_data(dcr_game_root(), 0, g_root, sizeof g_root);
    if (g_present)
      debugPrintf("[orbital] Orbital Escapade found in %s: its two worlds are planets\n", g_root);
    else
      debugPrintf("[orbital] no Orbital Escapade in %s\n", dcr_game_root());
    if (g_present)
      idx_start();
  }
  return g_present;
}

static uint8_t *read_file(const char *rel, size_t *len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  FILE *f = fopen(p, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  if (b) {
    b[n] = 0;
    *len = (size_t)n;
  }
  return b;
}

/* ------------------------------------------------------------ Lua files */
static int looks_like_lua(const uint8_t *d, size_t n) {
  if (n >= 4 && !memcmp(d, "\x1bLua", 4))
    return 1;
  size_t k = n < 256 ? n : 256, ok = 0;
  if (!k)
    return 0;
  for (size_t i = 0; i < k; i++)
    ok += (d[i] >= 32 && d[i] < 127) || d[i] == 9 || d[i] == 10 || d[i] == 13;
  return ok * 100 >= k * 97;
}

static int aes_cbc(const unsigned char key[32], int enc, const uint8_t *in, uint8_t *out, size_t n) {
  mbedtls_aes_context c;
  unsigned char iv[16] = {0};
  mbedtls_aes_init(&c);
  int r = enc ? mbedtls_aes_setkey_enc(&c, key, 256) : mbedtls_aes_setkey_dec(&c, key, 256);
  if (!r)
    r = mbedtls_aes_crypt_cbc(&c, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, n, iv, in, out);
  mbedtls_aes_free(&c);
  return r;
}

/* A mod Lua file, plain (source or bytecode): as it is, or decrypted. */
uint8_t *abs_orbital_lua_plain(const char *rel, size_t *len) {
  size_t n;
  uint8_t *raw = read_file(rel, &n);
  if (!raw)
    return NULL;
  if (looks_like_lua(raw, n)) {
    *len = n;
    return raw;
  }
  if (n == 0 || n % 16) {
    free(raw);
    return NULL;
  }
  uint8_t *d = malloc(n + 1);
  const unsigned char *keys[2] = {KEY_PC, KEY_ANDROID};
  for (int k = 0; d && k < 2; k++) {
    if (aes_cbc(keys[k], 0, raw, d, n))
      continue;
    size_t m = n;
    uint8_t pad = d[m - 1];
    if (pad >= 1 && pad <= 16 && pad <= m) {
      int good = 1;
      for (size_t i = m - pad; i < m; i++)
        good &= d[i] == pad;
      if (good)
        m -= pad;
    }
    if (looks_like_lua(d, m)) {
      if (d[0] != 0x1b) /* source: no trailing NULs */
        while (m && d[m - 1] == 0)
          m--;
      d[m] = 0;
      free(raw);
      *len = m;
      return d;
    }
  }
  free(d);
  free(raw);
  debugPrintf("[orbital] %s: not a Lua file this can read\n", rel);
  return NULL;
}

/* An LZMA stream of literals only (lc 3, lp 0, pb 2), with the range coder
 * of the LZMA SDK: what LzmaDec reads back byte for byte. */
typedef struct {
  uint8_t *out;
  size_t n, cap;
  uint64_t low;
  uint32_t range;
  uint8_t cache;
  uint64_t cache_size;
  int oom;
} RC;

static void rc_byte(RC *rc, uint8_t b) {
  if (rc->n == rc->cap) {
    size_t cap = rc->cap ? rc->cap * 2 : 4096;
    uint8_t *o = realloc(rc->out, cap);
    if (!o) {
      rc->oom = 1;
      return;
    }
    rc->out = o, rc->cap = cap;
  }
  rc->out[rc->n++] = b;
}

static void rc_shift_low(RC *rc) {
  if ((uint32_t)rc->low < 0xFF000000u || (rc->low >> 32) != 0) {
    uint8_t temp = rc->cache;
    do {
      rc_byte(rc, (uint8_t)(temp + (uint8_t)(rc->low >> 32)));
      temp = 0xFF;
    } while (--rc->cache_size != 0);
    rc->cache = (uint8_t)((uint32_t)rc->low >> 24);
  }
  rc->cache_size++;
  rc->low = (uint32_t)rc->low << 8;
}

static void rc_bit(RC *rc, uint16_t *p, int bit) {
  uint32_t bound = (rc->range >> 11) * *p;
  if (!bit) {
    rc->range = bound;
    *p = (uint16_t)(*p + ((2048 - *p) >> 5));
  } else {
    rc->low += bound;
    rc->range -= bound;
    *p = (uint16_t)(*p - (*p >> 5));
  }
  while (rc->range < (1u << 24)) {
    rc->range <<= 8;
    rc_shift_low(rc);
  }
}

static uint8_t *lzma_literals(const uint8_t *in, size_t n, size_t *out_len) {
  static uint16_t is_match[4], lit[8][0x300]; /* state 0 only; 8 contexts of lc 3 */
  for (int i = 0; i < 4; i++)
    is_match[i] = 1024;
  for (int c = 0; c < 8; c++)
    for (int i = 0; i < 0x300; i++)
      lit[c][i] = 1024;
  RC rc = {.range = 0xFFFFFFFFu, .cache_size = 1};
  static const uint8_t magic[9] = {0x89, 'L', 'Z', 'M', 'A', '\r', '\n', 0x1a, '\n'};
  for (int i = 0; i < 9; i++)
    rc_byte(&rc, magic[i]);
  const uint32_t dict = 1u << 16;
  rc_byte(&rc, 0x5d); /* (pb 2 * 5 + lp 0) * 9 + lc 3 */
  for (int i = 0; i < 4; i++)
    rc_byte(&rc, (uint8_t)(dict >> (8 * i)));
  for (int i = 0; i < 8; i++)
    rc_byte(&rc, (uint8_t)((uint64_t)n >> (8 * i)));
  /* the range coder's first byte (always 0) is its own: it starts here */
  size_t start = rc.n;
  uint8_t prev = 0;
  for (size_t i = 0; i < n && !rc.oom; i++) {
    rc_bit(&rc, &is_match[i & 3], 0);
    uint16_t *probs = lit[prev >> 5];
    unsigned sym = 1;
    for (int b = 7; b >= 0; b--) {
      int bit = (in[i] >> b) & 1;
      rc_bit(&rc, &probs[sym], bit);
      sym = (sym << 1) | (unsigned)bit;
    }
    prev = in[i];
  }
  for (int i = 0; i < 5; i++)
    rc_shift_low(&rc);
  (void)start;
  if (rc.oom) {
    free(rc.out);
    return NULL;
  }
  *out_len = rc.n;
  return rc.out;
}

/* A plain Lua file in the container the engine opens. */
uint8_t *abs_orbital_wrap_lua(const uint8_t *plain, size_t n, size_t *out_len) {
  size_t zn;
  uint8_t *z = lzma_literals(plain, n, &zn);
  if (!z)
    return NULL;
  size_t padded = (zn / 16 + 1) * 16;
  uint8_t *buf = malloc(padded);
  uint8_t *enc = malloc(padded);
  if (!buf || !enc) {
    free(z), free(buf), free(enc);
    return NULL;
  }
  memcpy(buf, z, zn);
  memset(buf + zn, (int)(padded - zn), padded - zn);
  free(z);
  int r = aes_cbc(KEY_ANDROID, 1, buf, enc, padded);
  free(buf);
  if (r) {
    free(enc);
    return NULL;
  }
  *out_len = padded;
  return enc;
}

/* levels/<folder>/<name>.lua -> the mod's file */
static int level_path(const char *path, char *rel, size_t cap) {
  for (unsigned e = 0; e < sizeof k_eps / sizeof k_eps[0]; e++) {
    char pre[64];
    int pl = snprintf(pre, sizeof pre, "data/levels/%s/", k_eps[e].folder);
    if (strncmp(path, pre, (size_t)pl))
      continue;
    const char *name = path + pl;
    size_t nl = strlen(name);
    if (nl < 5 || strcmp(name + nl - 4, ".lua"))
      return 0;
    char base[128], comic[140];
    snprintf(base, sizeof base, "%.*s", (int)(nl - 4), name);
    const char *orig = base;
    size_t xl = strlen(k_eps[e].prefix);
    if (!strncmp(base, k_eps[e].prefix, xl)) {
      orig = base + xl;
      if (!strcmp(orig, "Boss"))
        orig = k_eps[e].boss;
    } else if (!strncmp(base, "LevelComic", 10) && !strncmp(base + 10, k_eps[e].prefix, xl)) {
      /* the worlds' comics keep the game's LevelComic start: LevelComicOEV_1 */
      snprintf(comic, sizeof comic, "LevelComic%s", base + 10 + xl);
      orig = comic;
    }
    snprintf(rel, cap, "levels/%s/%s.lua", k_eps[e].modfolder, orig);
    return 1;
  }
  return 0;
}

/* A level declares its file's name (filename = "Level1.lua") and the engine
 * takes it only under that name: served as OEV_Level1, it declares that.
 * Source gets the assignment appended (the last one wins); bytecode has the
 * string constant rewritten (a Lua 5.1 chunk is sequential: a longer string
 * moves what follows, and nothing points into it). */
static uint8_t *rename_level(uint8_t *plain, size_t *n, const char *from, const char *to) {
  size_t tl = strlen(to), fl = strlen(from);
  if (*n && plain[0] == 0x1b) {
    uint8_t pat[160];
    if (fl + 5 > sizeof pat)
      return plain;
    uint32_t sz = (uint32_t)fl + 1;
    memcpy(pat, &sz, 4); /* size_t, little-endian, with the NUL */
    memcpy(pat + 4, from, fl + 1);
    for (size_t i = 0; i + fl + 5 <= *n; i++) {
      if (memcmp(plain + i, pat, fl + 5))
        continue;
      uint8_t *o = malloc(*n + tl + 8);
      if (!o)
        return plain;
      uint32_t nz = (uint32_t)tl + 1;
      memcpy(o, plain, i);
      memcpy(o + i, &nz, 4);
      memcpy(o + i + 4, to, tl + 1);
      memcpy(o + i + 4 + tl + 1, plain + i + fl + 5, *n - i - fl - 5);
      *n = *n - fl + tl;
      free(plain);
      return o;
    }
    debugPrintf("[orbital] %s: its name is not in the bytecode\n", from);
    return plain;
  }
  uint8_t *o = realloc(plain, *n + tl + 32);
  if (!o)
    return plain;
  *n += (size_t)sprintf((char *)o + *n, "\nfilename = \"%s\"\n", to);
  return o;
}

/* ------------------------------------------------------------ sprite sheets */
/* the sprite names the game has itself: from every .dat of its own */
typedef struct {
  char **v;
  uint32_t n, cap;
} NameSet;
static NameSet g_game_sprites, g_game_comps; /* the game's own names (the index, below) */

static uint32_t fnv1(const char *s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++)
    h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h;
}

static int set_has(const NameSet *s, const char *name, size_t n) {
  if (!s->cap)
    return 0;
  for (uint32_t h = fnv1(name, n) & (s->cap - 1);; h = (h + 1) & (s->cap - 1)) {
    const char *e = s->v[h];
    if (!e)
      return 0;
    if (strlen(e) == n && !memcmp(e, name, n))
      return 1;
  }
}

static void set_add(NameSet *s, const char *name, size_t n) {
  if ((s->n + 1) * 2 > s->cap) {
    uint32_t cap = s->cap ? s->cap * 2 : 4096;
    char **v = calloc(cap, sizeof *v);
    if (!v)
      return;
    for (uint32_t i = 0; i < s->cap; i++)
      if (s->v[i]) {
        uint32_t h = fnv1(s->v[i], strlen(s->v[i])) & (cap - 1);
        while (v[h])
          h = (h + 1) & (cap - 1);
        v[h] = s->v[i];
      }
    free(s->v);
    s->v = v, s->cap = cap;
  }
  if (set_has(s, name, n))
    return;
  uint32_t h = fnv1(name, n) & (s->cap - 1);
  while (s->v[h])
    h = (h + 1) & (s->cap - 1);
  char *c = malloc(n + 1);
  if (!c)
    return;
  memcpy(c, name, n);
  c[n] = 0;
  s->v[h] = c;
  s->n++;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* Walks a KA3D sprite file's SPRT chunk: each sheet (cb_sheet) and sprite
 * (cb_sprite, with its 12 bytes of rectangle and pivot). 0 if malformed. */
typedef void (*SheetCb)(void *ud, const char *name, size_t n, int nspr);
typedef void (*SpriteCb)(void *ud, const char *name, size_t n, const uint8_t rect[12]);
static int ka3d_walk(const uint8_t *b, size_t len, SheetCb cs, SpriteCb cp, void *ud) {
  if (len < 8 || memcmp(b, "KA3D", 4))
    return 0;
  size_t p = 8;
  while (p + 8 <= len) {
    uint32_t sz = be32(b + p + 4);
    const uint8_t *body = b + p + 8;
    if (p + 8 + sz > len)
      return 0;
    if (!memcmp(b + p, "SPRT", 4)) {
      size_t q = 0;
      if (sz < 2)
        return 0;
      int nsheets = be16(body);
      q = 2;
      for (int s = 0; s < nsheets; s++) {
        if (q + 2 > sz)
          return 0;
        size_t ln = be16(body + q);
        q += 2;
        if (q + ln + 2 > sz)
          return 0;
        const char *sname = (const char *)body + q;
        q += ln;
        int nspr = be16(body + q);
        q += 2;
        if (cs)
          cs(ud, sname, ln, nspr);
        for (int i = 0; i < nspr; i++) {
          if (q + 2 > sz)
            return 0;
          size_t l2 = be16(body + q);
          q += 2;
          if (q + l2 + 12 > sz)
            return 0;
          if (cp)
            cp(ud, (const char *)body + q, l2, body + q + l2);
          q += l2 + 12;
        }
      }
    }
    p += 8 + sz;
  }
  return 1;
}

/* a sheet file cut down to the new sprites: sheets with none are dropped */
typedef struct {
  uint8_t *out;
  size_t n, cap;
  size_t sheet_count_at, sheet_nspr_at;
  int nsheets, nspr, kept;
  int full; /* keep every sprite, the game's names as OE_<name> */
} Cut;

static void cut_put(Cut *c, const void *p, size_t n) {
  if (c->n + n > c->cap) {
    size_t cap = c->cap ? c->cap * 2 : 8192;
    while (cap < c->n + n)
      cap *= 2;
    uint8_t *o = realloc(c->out, cap);
    if (!o)
      return;
    c->out = o, c->cap = cap;
  }
  memcpy(c->out + c->n, p, n);
  c->n += n;
}

static void cut_put16(Cut *c, unsigned v) {
  uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
  cut_put(c, b, 2);
}

static void cut_close_sheet(Cut *c) {
  if (c->sheet_nspr_at && c->out) {
    c->out[c->sheet_nspr_at] = (uint8_t)(c->nspr >> 8);
    c->out[c->sheet_nspr_at + 1] = (uint8_t)c->nspr;
  }
}

static void cut_sheet(void *ud, const char *name, size_t n, int nspr) {
  Cut *c = ud;
  (void)nspr;
  cut_close_sheet(c);
  c->nsheets++;
  /* its picture under the served name: OE_<name> (the game has sheets of
   * the same names, which stay its own) */
  cut_put16(c, (unsigned)n + 3);
  cut_put(c, "OE_", 3);
  cut_put(c, name, n);
  c->sheet_nspr_at = c->n;
  cut_put16(c, 0);
  c->nspr = 0;
}

static void cut_sprite(void *ud, const char *name, size_t n, const uint8_t rect[12]) {
  Cut *c = ud;
  if (set_has(&g_game_sprites, name, n)) {
    if (!c->full)
      return;
    /* a full sheet: the game's name taken, the mod's picture as OE_<name> */
    cut_put16(c, (unsigned)n + 3);
    cut_put(c, "OE_", 3);
    cut_put(c, name, n);
    cut_put(c, rect, 12);
    c->nspr++;
    c->kept++;
    return;
  }
  cut_put16(c, (unsigned)n);
  cut_put(c, name, n);
  cut_put(c, rect, 12);
  c->nspr++;
  c->kept++;
}

static uint8_t *cut_dat(const uint8_t *b, size_t len, size_t *out_len, int *kept, int full) {
  Cut c = {0};
  c.full = full;
  static const uint8_t head[16] = {'K', 'A', '3', 'D', 0, 0, 0, 0, 'S', 'P', 'R', 'T', 0, 0, 0, 0};
  cut_put(&c, head, sizeof head);
  c.sheet_count_at = c.n;
  cut_put16(&c, 0);
  if (!ka3d_walk(b, len, cut_sheet, cut_sprite, &c) || !c.out) {
    free(c.out);
    return NULL;
  }
  cut_close_sheet(&c);
  c.out[c.sheet_count_at] = (uint8_t)(c.nsheets >> 8);
  c.out[c.sheet_count_at + 1] = (uint8_t)c.nsheets;
  uint32_t sprt = (uint32_t)(c.n - 16), all = (uint32_t)(c.n - 8);
  for (int i = 0; i < 4; i++) {
    c.out[4 + i] = (uint8_t)(all >> (24 - 8 * i));
    c.out[12 + i] = (uint8_t)(sprt >> (24 - 8 * i));
  }
  *out_len = c.n;
  *kept = c.kept;
  return c.out;
}

/* Composite sprites (a COMP chunk: version, count; each: name, parts of
 * name and x, y, and one 16-bit field): the same, cut to the new ones. */

typedef void (*CompCb)(void *ud, const uint8_t *entry, size_t n, const char *name, size_t nl);
static int comp_walk(const uint8_t *b, size_t len, CompCb cb, void *ud, unsigned *version) {
  if (len < 8 || memcmp(b, "KA3D", 4))
    return 0;
  size_t p = 8;
  while (p + 8 <= len) {
    uint32_t sz = be32(b + p + 4);
    const uint8_t *body = b + p + 8;
    if (p + 8 + sz > len)
      return 0;
    if (!memcmp(b + p, "COMP", 4)) {
      if (sz < 4)
        return 0;
      if (version)
        *version = be16(body);
      unsigned count = be16(body + 2);
      size_t q = 4;
      for (unsigned i = 0; i < count; i++) {
        size_t start = q;
        if (q + 2 > sz)
          return 0;
        size_t nl = be16(body + q);
        q += 2;
        const char *name = (const char *)body + q;
        q += nl;
        if (q + 2 > sz)
          return 0;
        unsigned parts = be16(body + q);
        q += 2;
        for (unsigned k = 0; k < parts; k++) {
          if (q + 2 > sz)
            return 0;
          q += 2 + be16(body + q) + 4;
        }
        q += 2;
        if (q > sz)
          return 0;
        cb(ud, body + start, q - start, name, nl);
      }
    }
    p += 8 + sz;
  }
  return 1;
}

/* ------------------------------------------------------------ the index */
/* Which sprites and composites each sheet file holds, the game's (from the
 * APK) and the mod's new ones, with every composite's parts: read once, on a
 * thread of its own started when the mod is found (a few seconds of reading,
 * off the game's thread), so the script's questions and the served files'
 * cuts are answered from memory. */
typedef struct {
  char *file;    /* the name the group lists: the game's file, OE_<mod file> */
  int comp;      /* a composite file */
  NameSet names; /* its sprites or composites (the mod's: those the game lacks) */
} Sheet;
typedef struct {
  Sheet *v;
  int n, cap;
} Sheets;
static Sheets g_game, g_mod, g_mod_all; /* g_mod_all: every sprite sheet of the mod's, all its names */

/* each composite's parts, "A,B", by its name */
typedef struct {
  char **k, **v;
  uint32_t n, cap;
} StrMap;
static StrMap g_parts;

static const char *map_get(const StrMap *m, const char *key, size_t n) {
  if (!m->cap)
    return NULL;
  for (uint32_t h = fnv1(key, n) & (m->cap - 1);; h = (h + 1) & (m->cap - 1)) {
    if (!m->k[h])
      return NULL;
    if (strlen(m->k[h]) == n && !memcmp(m->k[h], key, n))
      return m->v[h];
  }
}

static void map_put(StrMap *m, const char *key, size_t n, char *val) {
  if (map_get(m, key, n)) {
    free(val);
    return;
  }
  if ((m->n + 1) * 2 > m->cap) {
    uint32_t cap = m->cap ? m->cap * 2 : 1024;
    char **k = calloc(cap, sizeof *k), **v = calloc(cap, sizeof *v);
    if (!k || !v) {
      free(k), free(v), free(val);
      return;
    }
    for (uint32_t i = 0; i < m->cap; i++)
      if (m->k[i]) {
        uint32_t h = fnv1(m->k[i], strlen(m->k[i])) & (cap - 1);
        while (k[h])
          h = (h + 1) & (cap - 1);
        k[h] = m->k[i], v[h] = m->v[i];
      }
    free(m->k), free(m->v);
    m->k = k, m->v = v, m->cap = cap;
  }
  char *kc = malloc(n + 1);
  if (!kc) {
    free(val);
    return;
  }
  memcpy(kc, key, n);
  kc[n] = 0;
  uint32_t h = fnv1(key, n) & (m->cap - 1);
  while (m->k[h])
    h = (h + 1) & (m->cap - 1);
  m->k[h] = kc, m->v[h] = val;
  m->n++;
}

static Sheet *sheets_add(Sheets *s, const char *prefix, const char *file, int comp) {
  if (s->n == s->cap) {
    int cap = s->cap ? s->cap * 2 : 256;
    Sheet *v = realloc(s->v, sizeof *v * (size_t)cap);
    if (!v)
      return NULL;
    s->v = v, s->cap = cap;
  }
  Sheet *sh = &s->v[s->n];
  memset(sh, 0, sizeof *sh);
  size_t n = strlen(prefix) + strlen(file) + 1;
  sh->file = malloc(n);
  if (!sh->file)
    return NULL;
  snprintf(sh->file, n, "%s%s", prefix, file);
  sh->comp = comp;
  s->n++;
  return sh;
}

static void sheets_drop_last(Sheets *s) {
  Sheet *sh = &s->v[--s->n];
  free(sh->file);
  for (uint32_t i = 0; i < sh->names.cap; i++)
    free(sh->names.v[i]);
  free(sh->names.v);
}

/* a composite entry's parts, "A,B" */
static char *entry_parts(const uint8_t *entry, size_t n, size_t nl) {
  size_t q = 2 + nl, cap = 64, len = 0;
  char *out = malloc(cap);
  if (!out || q + 2 > n) {
    free(out);
    return NULL;
  }
  out[0] = 0;
  unsigned count = be16(entry + q);
  q += 2;
  for (unsigned k = 0; k < count && q + 2 <= n; k++) {
    size_t pl = be16(entry + q);
    if (q + 2 + pl > n)
      break;
    if (len + pl + 2 > cap) {
      char *o = realloc(out, cap = (len + pl + 2) * 2);
      if (!o)
        break;
      out = o;
    }
    if (len)
      out[len++] = ',';
    memcpy(out + len, entry + q + 2, pl);
    len += pl;
    out[len] = 0;
    q += 2 + pl + 4;
  }
  return out;
}

typedef struct {
  Sheet *sh;
  int mod; /* the mod's: only what the game lacks */
} IdxCtx;

/* each sprite's size as it shows: the game's, or the mod's new one's */
static StrMap g_sizes;

static void size_put(const char *name, size_t n, const uint8_t rect[12]) {
  if (map_get(&g_sizes, name, n))
    return;
  char *v = malloc(16);
  if (v) {
    snprintf(v, 16, "%u,%u", (unsigned)be16(rect + 4), (unsigned)be16(rect + 6));
    map_put(&g_sizes, name, n, v);
  }
}

static void idx_all(void *ud, const char *name, size_t n, const uint8_t rect[12]) {
  (void)rect;
  set_add(&((Sheet *)ud)->names, name, n);
}

static void idx_sprite(void *ud, const char *name, size_t n, const uint8_t rect[12]) {
  IdxCtx *c = ud;
  if (c->mod) {
    if (!set_has(&g_game_sprites, name, n)) {
      set_add(&c->sh->names, name, n);
      size_put(name, n, rect);
    }
  } else {
    set_add(&c->sh->names, name, n);
    set_add(&g_game_sprites, name, n);
    size_put(name, n, rect);
  }
}

static void idx_comp(void *ud, const uint8_t *entry, size_t n, const char *name, size_t nl) {
  IdxCtx *c = ud;
  if (c->mod && set_has(&g_game_comps, name, nl))
    return;
  set_add(&c->sh->names, name, nl);
  if (!c->mod)
    set_add(&g_game_comps, name, nl);
  map_put(&g_parts, name, nl, entry_parts(entry, n, nl));
}

static void idx_game_file(const char *name, void *ud) {
  (void)ud;
  size_t n = strlen(name);
  if (n < 5 || strcmp(name + n - 4, ".dat"))
    return;
  size_t len;
  void *b = abs_asset_read(name, &len);
  if (!b)
    return;
  const char *base = strrchr(name, '/');
  int comp = strstr(name, "COMPOSPRITES") != NULL;
  IdxCtx c = {sheets_add(&g_game, "", base ? base + 1 : name, comp), 0};
  if (c.sh) {
    if (comp)
      comp_walk(b, len, idx_comp, &c, NULL);
    else
      ka3d_walk(b, len, NULL, idx_sprite, &c);
  }
  free(b);
}

/* a sheet's picture is in the mod's folder (one sheet names a picture the
 * mod does not have: served, the engine would stop the game) */
static void idx_picture(void *ud, const char *name, size_t n, int nspr) {
  (void)nspr;
  int *ok = ud;
  char p[600];
  struct stat st;
  snprintf(p, sizeof p, "%s/images/PC/%.*s", g_root, (int)n, name);
  if (stat(p, &st) != 0)
    *ok = 0;
}

static void idx_mod_files(int comp) {
  char dir[400];
  snprintf(dir, sizeof dir, "%s/images/PC", g_root);
  DIR *d = opendir(dir);
  struct dirent *de;
  while (d && (de = readdir(d)) != NULL) {
    size_t fl = strlen(de->d_name);
    if (de->d_name[0] == '.' || fl < 5 || strcmp(de->d_name + fl - 4, ".dat") ||
        (strstr(de->d_name, "COMPOSPRITES") != NULL) != comp)
      continue; /* a sheet table: not a Mac's "._" file beside one */
    char rel[400];
    snprintf(rel, sizeof rel, "images/PC/%s", de->d_name);
    size_t n;
    uint8_t *b = read_file(rel, &n);
    if (!b)
      continue;
    IdxCtx c = {sheets_add(&g_mod, "OE_", de->d_name, comp), 1};
    if (c.sh) {
      int pictures = 1;
      if (comp)
        comp_walk(b, n, idx_comp, &c, NULL);
      else if (ka3d_walk(b, n, idx_picture, NULL, &pictures) && pictures) {
        ka3d_walk(b, n, NULL, idx_sprite, &c);
        Sheet *all = sheets_add(&g_mod_all, "OEF_", de->d_name, 0);
        if (all)
          ka3d_walk(b, n, NULL, idx_all, all);
      }
      if (!c.sh->names.n || !pictures)
        sheets_drop_last(&g_mod); /* nothing new in it, or a picture it names is missing */
    }
    free(b);
  }
  if (d)
    closedir(d);
}

static Mutex g_idx_lock, g_gc_lock;
static int g_idx_ready;
static Thread g_idx_thread;

static void idx_build_locked(void) {
  if (__atomic_load_n(&g_idx_ready, __ATOMIC_ACQUIRE))
    return;
  abs_assets_each("data/images/1024x768_android/", idx_game_file, NULL);
  idx_mod_files(0);
  idx_mod_files(1);
  debugPrintf("[orbital] index: the game's %u sprites and %u composites in %d files; the mod's new ones in %d "
              "files\n",
              (unsigned)g_game_sprites.n, (unsigned)g_game_comps.n, g_game.n, g_mod.n);
  __atomic_store_n(&g_idx_ready, 1, __ATOMIC_RELEASE);
}

static void idx_thread(void *arg) {
  (void)arg;
  mutexLock(&g_idx_lock);
  idx_build_locked();
  mutexUnlock(&g_idx_lock);
}

/* the index, built on this thread if its own has not finished yet */
static void idx_wait(void) {
  if (__atomic_load_n(&g_idx_ready, __ATOMIC_ACQUIRE))
    return;
  mutexLock(&g_idx_lock);
  idx_build_locked();
  mutexUnlock(&g_idx_lock);
}

static void idx_start(void) {
  mutexInit(&g_idx_lock);
  mutexInit(&g_gc_lock);
  static const int cores[2] = {1, 2};
  Result rc = 0;
  for (int i = 0; i < 2; i++) {
    rc = threadCreate(&g_idx_thread, idx_thread, NULL, NULL, 0x10000, 0x3B, cores[i]); /* 59: the NPDM's lowest */
    if (R_SUCCEEDED(rc)) {
      rc = threadStart(&g_idx_thread);
      if (R_SUCCEEDED(rc))
        return;
      threadClose(&g_idx_thread);
    }
  }
  debugPrintf("[orbital] no thread for the index (0x%x): it is read when first needed\n", (unsigned)rc);
}

static void names_of(const char *names, NameSet *out) {
  for (const char *p = names; *p;) {
    const char *e = strchr(p, ',');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n)
      set_add(out, p, n);
    p += n + (e ? 1 : 0);
  }
}

static void set_free(NameSet *s) {
  for (uint32_t i = 0; i < s->cap; i++)
    free(s->v[i]);
  free(s->v);
  memset(s, 0, sizeof *s);
}

/* ------------------------------------------------------------ the overlay */
/* KA3D with one empty chunk: a sheet of no sprites, a composite file of none */
static uint8_t *empty_ka3d(int comp, size_t *len) {
  static const uint8_t sprt[18] = {'K', 'A', '3', 'D', 0, 0, 0, 10, 'S', 'P', 'R', 'T', 0, 0, 0, 2, 0, 0};
  static const uint8_t cmp[20] = {'K', 'A', '3', 'D', 0, 0, 0, 12, 'C', 'O', 'M', 'P', 0, 0, 0, 4, 0, 2, 0, 0};
  size_t n = comp ? sizeof cmp : sizeof sprt;
  uint8_t *b = malloc(n);
  if (b) {
    memcpy(b, comp ? cmp : sprt, n);
    *len = n;
  }
  return b;
}

/* A group's composites: the engine reads <group>_COMPOSPRITES.dat for a
 * group that lists one. The mod's composites the game lacks that the script
 * says the group uses (abs_orbital_group_composites), each once; all of
 * them for a group it did not name. */
typedef struct {
  char name[64];
  NameSet comps;
} GroupComps;
static GroupComps g_gc[24];
static int g_ngc;

int abs_orbital_group_composites(const char *arg) {
  const char *bar = strchr(arg, '|');
  if (!bar || bar - arg >= (int)sizeof g_gc[0].name)
    return 0;
  idx_wait();
  mutexLock(&g_gc_lock);
  GroupComps *g = NULL;
  for (int i = 0; i < g_ngc; i++)
    if (strlen(g_gc[i].name) == (size_t)(bar - arg) && !memcmp(g_gc[i].name, arg, (size_t)(bar - arg)))
      g = &g_gc[i];
  if (!g && g_ngc < (int)(sizeof g_gc / sizeof g_gc[0])) {
    g = &g_gc[g_ngc++];
    memcpy(g->name, arg, (size_t)(bar - arg));
    g->name[bar - arg] = 0;
  }
  int n = 0;
  if (g) {
    NameSet want = {0};
    names_of(bar + 1, &want);
    for (uint32_t k = 0; k < want.cap; k++) {
      const char *w = want.v[k];
      if (!w || set_has(&g_game_comps, w, strlen(w)) || !map_get(&g_parts, w, strlen(w)))
        continue;
      set_add(&g->comps, w, strlen(w));
      n++;
    }
    set_free(&want);
  }
  mutexUnlock(&g_gc_lock);
  return n;
}

typedef struct {
  Cut out;
  NameSet seen;
  const NameSet *only;
  unsigned count;
} CompAll;

static void comp_all_entry(void *ud, const uint8_t *entry, size_t n, const char *name, size_t nl) {
  CompAll *c = ud;
  if (set_has(&g_game_comps, name, nl) || set_has(&c->seen, name, nl) || (c->only && !set_has(c->only, name, nl)))
    return;
  set_add(&c->seen, name, nl);
  cut_put(&c->out, entry, n);
  c->count++;
}

static uint8_t *mod_composites(const char *group, size_t gl, size_t *out_len) {
  idx_wait();
  CompAll c = {{0}, {0}, NULL, 0};
  mutexLock(&g_gc_lock);
  for (int i = 0; i < g_ngc; i++)
    if (strlen(g_gc[i].name) == gl && !memcmp(g_gc[i].name, group, gl))
      c.only = &g_gc[i].comps;
  mutexUnlock(&g_gc_lock);
  static const uint8_t head[20] = {'K', 'A', '3', 'D', 0, 0, 0, 0, 'C', 'O', 'M', 'P', 0, 0, 0, 0, 0, 2, 0, 0};
  cut_put(&c.out, head, sizeof head);
  for (int i = 0; i < g_mod.n; i++) {
    if (!g_mod.v[i].comp)
      continue;
    char rel[400];
    snprintf(rel, sizeof rel, "images/PC/%s", g_mod.v[i].file + 3);
    size_t n;
    uint8_t *b = read_file(rel, &n);
    if (b)
      comp_walk(b, n, comp_all_entry, &c, NULL);
    free(b);
  }
  for (uint32_t i = 0; i < c.seen.cap; i++)
    free(c.seen.v[i]);
  free(c.seen.v);
  uint8_t *o = c.out.out;
  if (!o)
    return NULL;
  uint32_t comp = (uint32_t)(c.out.n - 16), all = (uint32_t)(c.out.n - 8);
  for (int i = 0; i < 4; i++) {
    o[4 + i] = (uint8_t)(all >> (24 - 8 * i));
    o[12 + i] = (uint8_t)(comp >> (24 - 8 * i));
  }
  o[18] = (uint8_t)(c.count >> 8), o[19] = (uint8_t)c.count;
  *out_len = c.out.n;
  return o;
}

void *abs_orbital_read(const char *name, size_t *len) {
  if (strncmp(name, "data/", 5) || !abs_orbital_present())
    return NULL;
  char rel[400];
  if (level_path(name, rel, sizeof rel)) {
    size_t n;
    uint8_t *plain = abs_orbital_lua_plain(rel, &n);
    if (!plain)
      return NULL;
    const char *asked = strrchr(name, '/') + 1, *file = strrchr(rel, '/') + 1;
    if (strcmp(asked, file))
      plain = rename_level(plain, &n, file, asked);
    uint8_t *w = abs_orbital_wrap_lua(plain, n, len);
    free(plain);
    return w;
  }
  if (!strncmp(name, "data/images/oe/", 15)) {
    size_t nl = strlen(name);
    int dat = nl > 4 && !strcmp(name + nl - 4, ".dat");
    const char *cs = strstr(name, "_COMPOSPRITES.dat");
    if (cs)
      return mod_composites(name + 15, (size_t)(cs - (name + 15)), len);
    int full = !strncmp(name, "data/images/oe/OEF_", 19);
    if (full || !strncmp(name, "data/images/oe/OE_", 18)) {
      snprintf(rel, sizeof rel, "images/PC/%s", name + (full ? 19 : 18));
      size_t n;
      uint8_t *b = read_file(rel, &n);
      if (b && dat) {
        idx_wait();
        int kept = 0;
        uint8_t *c = cut_dat(b, n, len, &kept, full);
        free(b);
        return c;
      }
      if (b) {
        *len = n;
        return b;
      }
    }
    /* never a missing file: the engine stops the game on one */
    if (dat) {
      debugPrintf("[orbital] %s: not in the mod, served empty\n", name);
      return empty_ka3d(0, len);
    }
    if (nl > 4 && !strcmp(name + nl - 4, ".png")) {
      static const uint8_t k_png[70] = {
          0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
          0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
          0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01,
          0x7a, 0xa8, 0x57, 0x50, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
      uint8_t *b = malloc(sizeof k_png);
      if (b) {
        memcpy(b, k_png, sizeof k_png);
        *len = sizeof k_png;
      }
      debugPrintf("[orbital] %s: not in the mod, served blank\n", name);
      return b;
    }
    debugPrintf("[orbital] %s: not in the mod\n", name);
    return NULL;
  }
  if (!strncmp(name, "data/audio/oe/", 14)) {
    snprintf(rel, sizeof rel, "audio/%s", name + 14);
    return read_file(rel, len);
  }
  return NULL;
}

/* ------------------------------------------------------------ the card */
/* The mod is the whole PC game; the worlds use a part of it. abs_orbital.lua
 * lists what (prune_card): "images/PC/<file>.dat" for every sheet given to
 * the game and "audio/<path>" for every sound made for the worlds, a line
 * each. Kept besides: the pictures those sheets name (their SPRT sheet
 * names), every sheet table (.dat, read by the index), the scripts, the
 * level folders of the two worlds and the files at the top of levels/.
 * Removed: the other pictures in images/PC, the other sounds under audio/,
 * the other folders in levels/, and the "._" files a Mac writes beside every
 * file it copies to the card. All of it on a thread of its own (reading the
 * sheets and a few thousand files on the card), and only while the folder
 * is not trimmed yet (abs_orbital_trimmed). Nothing is removed unless the
 * list and the folder agree (every listed sheet is there, the worlds'
 * levels are). */
static Thread g_prune_thread;
static int g_prune_started;
static NameSet g_prune_keep;
static char *g_prune_list;

static void keep_picture(void *ud, const char *name, size_t n, int nspr) {
  (void)nspr;
  char rel[300];
  int l = snprintf(rel, sizeof rel, "images/PC/%.*s", (int)n, name);
  if (l > 0 && l < (int)sizeof rel)
    set_add(ud, rel, (size_t)l);
}

/* a folder's entries, read whole before any is removed */
typedef struct {
  char **name;
  unsigned char *dir;
  int n;
} Listing;

static int list_dir(const char *path, Listing *out) {
  memset(out, 0, sizeof *out);
  DIR *d = opendir(path);
  if (!d)
    return -1;
  int cap = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    if (out->n == cap) {
      cap = cap ? cap * 2 : 64;
      char **nv = realloc(out->name, (size_t)cap * sizeof *nv);
      unsigned char *dv = realloc(out->dir, (size_t)cap);
      if (!nv || !dv) {
        free(nv ? nv : out->name);
        free(dv ? dv : out->dir);
        out->name = NULL, out->dir = NULL, out->n = 0;
        closedir(d);
        return -1;
      }
      out->name = nv, out->dir = dv;
    }
    char p[600];
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    struct stat st;
    out->dir[out->n] = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
    out->name[out->n++] = strdup(e->d_name);
  }
  closedir(d);
  return 0;
}

static void list_free(Listing *l) {
  for (int i = 0; i < l->n; i++)
    free(l->name[i]);
  free(l->name);
  free(l->dir);
  memset(l, 0, sizeof *l);
}

typedef struct {
  unsigned files;
  uint64_t bytes;
} Freed;

static void remove_file(const char *path, Freed *fr) {
  struct stat st;
  if (stat(path, &st) != 0)
    return;
  if (unlink(path) == 0)
    fr->files++, fr->bytes += (uint64_t)st.st_size;
}

static void remove_tree(const char *path, Freed *fr) {
  Listing l;
  if (list_dir(path, &l) != 0)
    return;
  for (int i = 0; i < l.n; i++) {
    char p[600];
    snprintf(p, sizeof p, "%s/%s", path, l.name[i]);
    if (l.dir[i])
      remove_tree(p, fr);
    else
      remove_file(p, fr);
  }
  list_free(&l);
  rmdir(path);
}

/* the files under g_root/<rel> not in the keep set (pictures only, in
 * images/PC) */
static void remove_unlisted(const char *rel, int pictures, Freed *fr) {
  char path[600];
  snprintf(path, sizeof path, "%s/%s", g_root, rel);
  Listing l;
  if (list_dir(path, &l) != 0)
    return;
  for (int i = 0; i < l.n; i++) {
    char r[600], p[900];
    int rl = snprintf(r, sizeof r, "%s/%s", rel, l.name[i]);
    if (rl <= 0 || rl >= (int)sizeof r)
      continue;
    snprintf(p, sizeof p, "%s/%s", g_root, r);
    if (l.dir[i]) {
      if (!pictures)
        remove_unlisted(r, 0, fr);
      continue;
    }
    if (!strncmp(l.name[i], "._", 2)) {
      remove_file(p, fr); /* a Mac's */
      continue;
    }
    if (pictures) {
      const char *dot = strrchr(l.name[i], '.');
      if (!dot || (strcasecmp(dot, ".png") && strcasecmp(dot, ".pvr") && strcasecmp(dot, ".webp") &&
                   strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg")))
        continue; /* sheet tables and the rest stay */
    }
    if (!set_has(&g_prune_keep, r, (size_t)rl))
      remove_file(p, fr);
  }
  list_free(&l);
}

/* every "._" file under the folder (a Mac's), as deep as it goes */
static void remove_mac_files(const char *path, int depth, Freed *fr) {
  Listing l;
  if (depth > 6 || list_dir(path, &l) != 0)
    return;
  for (int i = 0; i < l.n; i++) {
    char p[900];
    snprintf(p, sizeof p, "%s/%s", path, l.name[i]);
    if (l.dir[i])
      remove_mac_files(p, depth + 1, fr);
    else if (!strncmp(l.name[i], "._", 2))
      remove_file(p, fr);
  }
  list_free(&l);
}

static int has_files(const char *rel) {
  char path[600];
  snprintf(path, sizeof path, "%s/%s", g_root, rel);
  Listing l;
  if (list_dir(path, &l) != 0)
    return 0;
  int n = 0;
  for (int i = 0; i < l.n; i++)
    n += strncmp(l.name[i], "._", 2) != 0;
  list_free(&l);
  return n > 0;
}

/* the list (abs_orbital.lua) -> the keep set: what it names and the pictures
 * its sheets name; 0 when it and the folder do not agree */
static const char *prune_keep_set(const char *keep) {
  unsigned sheets = 0, sounds = 0;
  for (const char *p = keep; *p;) {
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n > 10 && n < 280 && !strstr(p, "..")) {
      if (!strncmp(p, "images/PC/", 10) && n > 14 && !memcmp(p + n - 4, ".dat", 4)) {
        char rel[300];
        memcpy(rel, p, n), rel[n] = 0;
        size_t dn;
        uint8_t *dat = read_file(rel, &dn);
        if (!dat)
          return "a sheet the worlds use is missing";
        ka3d_walk(dat, dn, keep_picture, NULL, &g_prune_keep);
        free(dat);
        set_add(&g_prune_keep, p, n);
        sheets++;
      } else if (!strncmp(p, "audio/", 6)) {
        set_add(&g_prune_keep, p, n);
        sounds++;
      }
    }
    p += n + (e ? 1 : 0);
  }
  char l1[64], l2[64];
  snprintf(l1, sizeof l1, "levels/%s", k_eps[0].modfolder);
  snprintf(l2, sizeof l2, "levels/%s", k_eps[1].modfolder);
  if (sheets < 10 || sounds < 10 || !has_files(l1) || !has_files(l2))
    return "the list and the folder do not agree";
  return NULL;
}

static void prune_thread(void *arg) {
  (void)arg;
  const char *why = prune_keep_set(g_prune_list);
  free(g_prune_list);
  g_prune_list = NULL;
  if (why) {
    debugPrintf("[orbital] orbital/data not trimmed: %s\n", why);
    set_free(&g_prune_keep);
    return;
  }
  Freed fr = {0, 0};
  remove_unlisted("images/PC", 1, &fr);
  remove_unlisted("audio", 0, &fr);
  char path[600];
  snprintf(path, sizeof path, "%s/levels", g_root);
  Listing l;
  if (list_dir(path, &l) == 0) {
    for (int i = 0; i < l.n; i++)
      if (l.dir[i] && strcmp(l.name[i], k_eps[0].modfolder) && strcmp(l.name[i], k_eps[1].modfolder)) {
        char p[700];
        snprintf(p, sizeof p, "%s/%s", path, l.name[i]);
        remove_tree(p, &fr);
      }
    list_free(&l);
  }
  remove_mac_files(g_root, 0, &fr);
  set_free(&g_prune_keep);
  if (fr.files)
    debugPrintf("[orbital] orbital/data trimmed to what the worlds use: %u files removed, %llu MB freed\n", fr.files,
                (unsigned long long)((fr.bytes + (1 << 19)) >> 20));
  else
    debugPrintf("[orbital] orbital/data holds only what the worlds use\n");
}

/* Trimmed already: levels/ holds no folder but the two worlds' and no Mac
 * file (a fresh copy of the mod over it brings the others back). One
 * listing: asked at every launch, before anything is worked out for it. */
int abs_orbital_trimmed(void) {
  if (!abs_orbital_present())
    return 1;
  char path[600];
  snprintf(path, sizeof path, "%s/levels", g_root);
  Listing l;
  if (list_dir(path, &l) != 0)
    return 1;
  int clean = 1;
  for (int i = 0; i < l.n && clean; i++)
    if (!strncmp(l.name[i], "._", 2) ||
        (l.dir[i] && strcmp(l.name[i], k_eps[0].modfolder) && strcmp(l.name[i], k_eps[1].modfolder)))
      clean = 0;
  list_free(&l);
  return clean;
}

char *abs_orbital_prune(const char *keep, int wait) {
  if (!abs_orbital_present() || g_prune_started)
    return strdup("");
  g_prune_list = strdup(keep);
  if (!g_prune_list)
    return strdup("");
  g_prune_started = 1;
  if (wait) {
    prune_thread(NULL);
    return strdup("");
  }
  static const int cores[2] = {1, 2};
  for (int i = 0; i < 2; i++)
    if (R_SUCCEEDED(threadCreate(&g_prune_thread, prune_thread, NULL, NULL, 0x10000, 0x3B, cores[i]))) {
      if (R_SUCCEEDED(threadStart(&g_prune_thread)))
        return strdup("");
      threadClose(&g_prune_thread);
    }
  prune_thread(NULL); /* no thread: here, then */
  return strdup("");
}

/* ------------------------------------------------------------ to the script */
typedef struct {
  char *s;
  size_t n, cap;
} Str;

static void str_add(Str *o, const char *a, size_t n) {
  if (o->n + n + 1 > o->cap) {
    size_t cap = (o->n + n + 1) * 2;
    char *s = realloc(o->s, cap);
    if (!s)
      return;
    o->s = s, o->cap = cap;
  }
  memcpy(o->s + o->n, a, n);
  o->n += n;
  o->s[o->n] = 0;
}

static char *str_done(Str *o) {
  if (!o->s)
    str_add(o, "", 0);
  return o->s;
}

/* Which of the mod's files hold any of these sprites or composites (names
 * separated by commas) that the game does not have itself:
 * "OE_FILE.dat,OE_FILE_COMPOSPRITES.dat". */
char *abs_orbital_sheets_for(const char *names) {
  if (!abs_orbital_present())
    return NULL;
  idx_wait();
  NameSet want = {0};
  names_of(names, &want);
  Str out = {0};
  for (int i = 0; i < g_mod.n; i++) {
    const Sheet *sh = &g_mod.v[i];
    for (uint32_t k = 0; k < want.cap; k++)
      if (want.v[k] && set_has(&sh->names, want.v[k], strlen(want.v[k]))) {
        if (out.n)
          str_add(&out, ",", 1);
        str_add(&out, sh->file, strlen(sh->file));
        break;
      }
  }
  set_free(&want);
  return str_done(&out);
}

/* Which of the game's own files (sheets and composite files of
 * data/images/1024x768_android) hold which of these names:
 * "FILE.dat=NAME|NAME,FILE.dat=NAME", for the script to pick the fewest of
 * the game's groups (from its loadlist) that cover them. */
char *abs_orbital_game_sheets_for(const char *names) {
  idx_wait();
  NameSet want = {0};
  names_of(names, &want);
  Str out = {0};
  for (int i = 0; i < g_game.n; i++) {
    const Sheet *sh = &g_game.v[i];
    int any = 0;
    for (uint32_t k = 0; k < want.cap; k++) {
      const char *w = want.v[k];
      if (!w || !set_has(&sh->names, w, strlen(w)))
        continue;
      if (!any) {
        if (out.n)
          str_add(&out, ",", 1);
        str_add(&out, sh->file, strlen(sh->file));
      }
      str_add(&out, any ? "|" : "=", 1);
      str_add(&out, w, strlen(w));
      any = 1;
    }
  }
  set_free(&want);
  return str_done(&out);
}

/* The sprites the named composites (the mod's and the game's) are made of:
 * "NAME,NAME". */
char *abs_orbital_parts(const char *names) {
  idx_wait();
  NameSet want = {0}, got = {0};
  names_of(names, &want);
  for (uint32_t k = 0; k < want.cap; k++) {
    const char *w = want.v[k];
    const char *parts = w ? map_get(&g_parts, w, strlen(w)) : NULL;
    if (parts && *parts)
      names_of(parts, &got);
  }
  Str out = {0};
  for (uint32_t k = 0; k < got.cap; k++)
    if (got.v[k]) {
      if (out.n)
        str_add(&out, ",", 1);
      str_add(&out, got.v[k], strlen(got.v[k]));
    }
  set_free(&want);
  set_free(&got);
  return str_done(&out);
}

/* The strings in double quotes in these of the mod's script files (paths
 * under data/, separated by commas), each once, one per line: the script
 * looks among them for the sounds the mod's code plays. */
char *abs_orbital_quoted(const char *files) {
  NameSet got = {0};
  for (const char *p = files; *p;) {
    const char *e = strchr(p, ',');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    char rel[300];
    if (n && n < sizeof rel) {
      memcpy(rel, p, n);
      rel[n] = 0;
      size_t len;
      uint8_t *src = abs_orbital_lua_plain(rel, &len);
      if (src && len && src[0] != 0x1b) {
        for (size_t i = 0; i < len; i++) {
          if (src[i] != '"')
            continue;
          size_t j = i + 1;
          while (j < len && src[j] != '"' && src[j] != '\n' && src[j] != '\\' && j - i < 80)
            j++;
          if (j < len && src[j] == '"' && j > i + 1)
            set_add(&got, (const char *)src + i + 1, j - i - 1);
          i = j;
        }
      }
      free(src);
    }
    p += n + (e ? 1 : 0);
  }
  Str out = {0};
  for (uint32_t k = 0; k < got.cap; k++)
    if (got.v[k]) {
      if (out.n)
        str_add(&out, "\n", 1);
      str_add(&out, got.v[k], strlen(got.v[k]));
    }
  set_free(&got);
  return str_done(&out);
}

/* A sprite's size as it shows, "w,h" (the game's, or the mod's new one's),
 * or "" */
char *abs_orbital_sprite_size(const char *name) {
  idx_wait();
  const char *v = map_get(&g_sizes, name, strlen(name));
  Str out = {0};
  if (v)
    str_add(&out, v, strlen(v));
  return str_done(&out);
}

/* Which of the mod's sheets hold any of these names, whether the game has
 * them or not, served whole with the game's names as OE_<name>:
 * "OEF_FILE.dat,..." (the comics: the mod drew its own under the game's
 * names). */
char *abs_orbital_full_sheets_for(const char *names) {
  idx_wait();
  NameSet want = {0};
  names_of(names, &want);
  Str out = {0};
  for (int i = 0; i < g_mod_all.n; i++) {
    const Sheet *sh = &g_mod_all.v[i];
    for (uint32_t k = 0; k < want.cap; k++)
      if (want.v[k] && set_has(&sh->names, want.v[k], strlen(want.v[k]))) {
        if (out.n)
          str_add(&out, ",", 1);
        str_add(&out, sh->file, strlen(sh->file));
        break;
      }
  }
  set_free(&want);
  return str_done(&out);
}
