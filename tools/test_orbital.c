/* test_orbital.c -- source/abs_orbital.c on a PC, against the player's copy of
 * the mod and the game's assets unpacked from the APK:
 *   cc -I tools/host_shim -I source -I runtime/source -I <mbedtls>/include tools/test_orbital.c \
 *      source/abs_orbital.c source/abs_png.c <mbedtls libs> -lz -o test_orbital
 *   ./test_orbital <root with orbital/data> <unpacked APK assets dir> <out dir>
 * Writes out/level.bin (a wrapped level: checked by the caller with openssl +
 * Python's lzma), out/cut.dat (a cut sheet), and prints what it found. */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "abs.h"

static const char *g_root, *g_apk;
const char *dcr_game_root(void) { return g_root; }
void debugPrintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("    ");
  vprintf(fmt, ap);
  va_end(ap);
}
void *abs_asset_read(const char *name, size_t *len) {
  char p[800];
  snprintf(p, sizeof p, "%s/%s", g_apk, name);
  FILE *f = fopen(p, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *b = malloc((size_t)n + 1);
  fread(b, 1, (size_t)n, f);
  fclose(f);
  *len = (size_t)n;
  return b;
}
void abs_assets_each(const char *prefix, void (*cb)(const char *name, void *ud), void *ud) {
  char dir[800];
  snprintf(dir, sizeof dir, "%s/%s", g_apk, prefix);
  DIR *d = opendir(dir);
  struct dirent *de;
  while (d && (de = readdir(d))) {
    char n[800];
    snprintf(n, sizeof n, "%s%s", prefix, de->d_name);
    cb(n, ud);
  }
  if (d)
    closedir(d);
}

static int fails;
static void check(int ok, const char *what) {
  printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  fails += !ok;
}

static void save(const char *dir, const char *name, const void *b, size_t n) {
  char p[800];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "wb");
  fwrite(b, 1, n, f);
  fclose(f);
}

int main(int argc, char **argv) {
  g_root = argv[1], g_apk = argv[2];
  const char *out = argv[3];
  check(abs_orbital_present(), "the mod is found");
  size_t n;
  uint8_t *src = abs_orbital_lua_plain("levels/theme1/Level1.lua", &n);
  check(src && n > 100 && !memcmp(src, "filename", 8), "a source level (the mod's own, plain)");
  free(src);
  uint8_t *pc = abs_orbital_lua_plain("scripts/birds.lua", &n);
  check(pc && !memcmp(pc, "birds = {", 9), "a file under the PC key (birds.lua)");
  free(pc);
  uint8_t *an = abs_orbital_lua_plain("levels/bonus/Bonus444.lua", &n);
  check(an && !memcmp(an, "\x1bLua", 4), "a file under the Android key (bytecode)");
  free(an);
  /* the overlay: a level of each world, wrapped */
  size_t wn;
  void *w = abs_orbital_read("data/levels/oe_vegetoids/OEV_Level3.lua", &wn);
  check(w && wn % 16 == 0, "Vege-toids level 3, in the game's container");
  if (w)
    save(out, "level.bin", w, wn);
  free(w);
  w = abs_orbital_read("data/levels/oe_omelettification/OEO_Boss.lua", &wn);
  check(w != NULL, "Omelettification's boss (OEO_Boss -> LevelBossTheme2)");
  free(w);
  w = abs_orbital_read("data/levels/oe_vegetoids/LevelSelection.lua", &wn);
  check(w != NULL, "Vege-toids' level selection");
  free(w);
  /* a sheet cut down to new sprites */
  void *c = abs_orbital_read("data/images/oe/OE_INGAME_BIRDS_1.dat", &wn);
  check(c && wn > 16, "INGAME_BIRDS_1.dat cut down");
  if (c)
    save(out, "cut.dat", c, wn);
  free(c);
  void *comp = abs_orbital_read("data/images/oe/OE_INGAME_COMPOSPRITES.dat", &wn);
  check(comp && wn > 20, "INGAME_COMPOSPRITES.dat cut down");
  if (comp)
    save(out, "comp.dat", comp, wn);
  free(comp);
  void *png = abs_orbital_read("data/images/oe/OE_THEME_GA_1.png", &wn);
  check(png && !memcmp(png, "\x89PNG", 4), "a picture, as it is");
  free(png);
  char *sheets = abs_orbital_sheets_for("BIRD_SPACE_BOOMERANG_NORMAL,THEME_GA_1_BG,RED_BIRD_NOT_A_SPRITE,BIRD_RED_NORMAL");
  printf("  sheets: %s\n", sheets ? sheets : "(none)");
  check(sheets && strstr(sheets, "OE_THEME_GA_1.dat") && strstr(sheets, ".dat"), "the sheets holding new sprites");
  free(sheets);
  printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
  return fails != 0;
}
