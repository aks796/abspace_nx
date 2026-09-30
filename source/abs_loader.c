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
  int rc = so_load(&g_mod_game, path, NULL, SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than SO_REGION_BYTES"
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

/* ------------------------------------------------ the shared runtime's hooks
 * codespace.h: code written at run time by the game's own modules (PvZ's mod
 * did that); this engine never does, so every request is the plain shim's. */
volatile int g_cs_armed;
void *cs_mmap(size_t len, int prot, const void *caller) { return NULL; }
int cs_munmap(void *addr, size_t len) { return 0; }
int cs_mprotect(void *addr, size_t len, int prot, const void *caller) {
  /* The engine's own pages: never a real change (text stays RX, data RW). */
  return so_find_module_by_addr(addr) != NULL;
}
int cs_write(void *dst, const void *src, size_t n, int c, int kind) { return 0; }

/* exc_handler.c: no trampoline pool here. */
int dcr_in_code_pool(const void *p) { return 0; }

/* dcr_net.h: offline; no real sockets. */
int dcr_net_owns(int fd) { return 0; }
int dcr_net_close(int fd) { return -1; }
int dcr_net_fcntl(int fd, int cmd, long arg) { return -1; }
int dcr_net_ioctl(int fd, unsigned long req, void *arg) { return -1; }
short dcr_net_ready(int fd, short events) { return 0; }
