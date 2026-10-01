/* abs_loader.c -- loading Angry Birds Space's engine.
 *
 * The APK carries three armeabi-v7a libraries; only one is the game:
 *   libAngryBirdsSpace.so  Rovio's Fusion engine + the game (Lua 5.1, Box2D,
 *                          libpng/jpeg/webp, mpg123, libcurl, BoringSSL...),
 *                          all statically linked; imports only bionic libc/m,
 *                          pthread, GLES2, five AAssetManager calls and liblog
 *   libjs.so, libadcolony.so   the AdColony ad SDK: not loaded
 * Java loads it with System.loadLibrary (Globals.loadLibraries): its
 * constructors run then, JNI_OnLoad right after (abs_boot.c).
 *
 * Unlike PvZ's mod, nothing here writes code at run time, so the module is
 * mapped and sealed in one go. The only change made to it is data: before its
 * Lua states exist, abs_lua_install() points a few entries of Lua's math
 * library table at the controller bridge (abs_lua.c). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "codespace.h"
#include "config.h"
#include "dcr_net.h"
#include "error.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

so_module g_mod_game;

void *abs_native(const char *symbol) { return so_resolve_external(symbol); }

int abs_load_module(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), ABS_LIB_GAME);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB staged %p -> %p (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base,
              g_mod_game.load_virtbase, missing);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  /* Data only (the RW segment): Lua's math library table, before any
   * lua_State registers it. */
  abs_lua_install();
  return 0;
}

/* System.loadLibrary runs the library's constructors. */
void abs_run_constructors(void) {
  extern int g_so_trace_ctors;
  g_so_trace_ctors = dcr_is_emulator();
  u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", g_mod_game.base_name,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}
