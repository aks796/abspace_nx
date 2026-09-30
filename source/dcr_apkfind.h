/* dcr_apkfind.h -- the player's APK in the game folder, whatever it is
 * called: every file there ending in .apk is opened as a zip, and the ones
 * holding the game's library (DCR_APK_LIB) are the game's. Header-only and
 * free of libnx: the game program (main.c, which then picks among them by
 * their manifests) and the launcher both use it. MIT.
 */
#ifndef DCR_APKFIND_H
#define DCR_APKFIND_H

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DCR_APK_LIB "lib/armeabi-v7a/libAngryBirdsSpace.so"
#define DCR_APKS_MAX 8

static inline uint32_t apkf_rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint32_t apkf_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* The zip at `path` has an entry named `want`: read from its central
 * directory (found through the end record in its last 64 KB). */
static inline int apk_has_entry(const char *path, const char *want) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  int found = 0;
  uint8_t *tail = NULL, *cd = NULL;
  long size, tl, e = -1;
  uint32_t cd_size, cd_off;
  size_t wl = strlen(want);
  if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 22)
    goto out;
  tl = size < 65557 ? size : 65557;
  tail = malloc((size_t)tl);
  if (!tail || fseek(f, size - tl, SEEK_SET) != 0 || fread(tail, 1, (size_t)tl, f) != (size_t)tl)
    goto out;
  for (long i = tl - 22; i >= 0; i--)
    if (apkf_rd32(tail + i) == 0x06054b50) {
      e = i;
      break;
    }
  if (e < 0)
    goto out;
  cd_size = apkf_rd32(tail + e + 12), cd_off = apkf_rd32(tail + e + 16);
  if (!cd_size || cd_size > (64u << 20) || (long)cd_off + (long)cd_size > size)
    goto out;
  cd = malloc(cd_size);
  if (!cd || fseek(f, (long)cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size)
    goto out;
  for (uint32_t p = 0; p + 46 <= cd_size;) {
    if (apkf_rd32(cd + p) != 0x02014b50)
      break;
    uint32_t nl = apkf_rd16(cd + p + 28), xl = apkf_rd16(cd + p + 30), cl = apkf_rd16(cd + p + 32);
    if (p + 46 + nl > cd_size)
      break;
    if (nl == wl && !memcmp(cd + p + 46, want, wl)) {
      found = 1;
      break;
    }
    p += 46 + nl + xl + cl;
  }
out:
  free(tail);
  free(cd);
  fclose(f);
  return found;
}

/* The APKs in `dir`: the game's (name[], up to DCR_APKS_MAX) and how many
 * others (other: the first of them), for the messages. */
typedef struct {
  char name[DCR_APKS_MAX][256];
  int n;
  int others;
  char other[256];
} DcrApks;

static inline void dcr_find_apks(const char *dir, DcrApks *out) {
  memset(out, 0, sizeof *out);
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (e->d_name[0] == '.' || n < 5 || n >= 256 || strcasecmp(e->d_name + n - 4, ".apk"))
      continue; /* not a Mac's "._" file beside one either */
    char p[600];
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    if (apk_has_entry(p, DCR_APK_LIB)) {
      if (out->n < DCR_APKS_MAX)
        snprintf(out->name[out->n++], sizeof out->name[0], "%s", e->d_name);
    } else if (!out->others++) {
      snprintf(out->other, sizeof out->other, "%s", e->d_name);
    }
  }
  closedir(d);
}

#endif
