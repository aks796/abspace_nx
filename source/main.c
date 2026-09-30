/* main.c -- boot sequence for the Angry Birds Space wrapper (32-bit).
 *
 * The order here matters; each step says why it is where it is. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include "abs.h"
#include "config.h"
#include "dcr_apkfind.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_sched.h"
#include "dcr_time.h"
#include "error.h"
#include "nx_init.h"
#include "selfproc.h"
#include "so_util.h"
#include "util.h"

void dcr_setup_move_old_folder(void); /* dcr_setup.c */
void dcr_setup_update_from_nro(void);
void dcr_setup_from_apk(const char *apk);

static char g_root[256] = "sdmc:" DCR_ROOT_PATH;
const char *dcr_game_root(void) { return g_root; }
static char g_apk[512];
const char *dcr_apk_path(void) { return g_apk; } /* dcr_path.c: the app's base.apk */

/* The player's APK: any file in the game folder ending in .apk that holds
 * the game's library (dcr_apkfind.h); of several, the one whose manifest is
 * this game at the version the port is made for (then this game at any
 * version, then the newest file). 0 when there is none. */
static int find_apk(void) {
  DcrApks a;
  dcr_find_apks(g_root, &a);
  int best = -1, best_score = -1;
  time_t best_time = 0;
  for (int i = 0; i < a.n; i++) {
    char p[600];
    snprintf(p, sizeof p, "%s/%s", g_root, a.name[i]);
    int score = 0;
    if (dcr_manifest_load(p) == 0)
      score = (strcmp(dcr_manifest_package(), DCR_PACKAGE) ? 0 : 2) +
              (dcr_manifest_version_code() == ABS_VERSION_CODE ? 1 : 0);
    struct stat st;
    time_t t = stat(p, &st) == 0 ? st.st_mtime : 0;
    if (score > best_score || (score == best_score && t > best_time))
      best = i, best_score = score, best_time = t;
  }
  if (best < 0) {
    if (a.others)
      debugPrintf("[boot] %d APK%s in %s, none with %s (e.g. %s)\n", a.others, a.others > 1 ? "s" : "", g_root,
                  DCR_APK_LIB, a.other);
    return 0;
  }
  snprintf(g_apk, sizeof g_apk, "%s/%s", g_root, a.name[best]);
  if (a.n > 1)
    debugPrintf("[boot] %d APKs with the game in %s: using %s\n", a.n, g_root, a.name[best]);
  return 1;
}

extern volatile uint32_t __dcr_reloc_path __attribute__((visibility("hidden")));

static void report_boot(void) {
  const u64 MB = 1024 * 1024;
  debugPrintf("[boot] === abspace_nx: Angry Birds Space (Rovio Fusion engine, armeabi-v7a) ===\n");
  static const char *const paths[] = {"none needed", "patched through a writable alias (hardware)",
                                      "direct writes (emulator: pseudo-handle refused)"};
  debugPrintf("[boot] text relocations: %s\n",
              __dcr_reloc_path < 3 ? paths[__dcr_reloc_path] : "?");
  debugPrintf("[heap] total %u MB, used %u MB at start, heap region %u MB, heap %u MB @ %p\n",
              (unsigned)(g_nxinit.total / MB), (unsigned)(g_nxinit.used / MB),
              (unsigned)(g_nxinit.heap_region / MB), (unsigned)(g_nxinit.heap / MB),
              (void *)g_nxinit.heap_base);
  debugPrintf("[svc] sm=%x applet=%x hid=%x time=%x fs=%x sdmc=%x\n", g_nxinit.rc_sm,
              g_nxinit.rc_applet, g_nxinit.rc_hid, g_nxinit.rc_time, g_nxinit.rc_fs,
              g_nxinit.rc_sdmc);
  if (R_FAILED(g_nxinit.rc_time))
    debugPrintf("[svc] time service unavailable: clocks fall back to the system tick\n");
}

int main(int argc, char *argv[]) {
  mkdir(g_root, 0777);
  log_init(g_root);
  log_console_open(); /* blank: text only when asked or for setup work */
  report_boot();
  /* an install from before the folder's rename comes over first: its
   * config.ini, APK and saves are what the rest reads */
  dcr_setup_move_old_folder();

  if (chdir(g_root) != 0)
    debugPrintf("[boot] WARNING: chdir(%s) failed\n", g_root);
  dcr_config_load(); /* config.ini: controls, resolution, language, boost */
  void dcr_boost_launch_begin(void);
  dcr_boost_launch_begin(); /* CPU at 1785 MHz until the first picture (dcr_boost.c) */
  if (dcr_config()->boot_log)
    log_console_show_text();
  dcr_time_init();
  dcr_path_prepare_dirs();

  /* A newer build of this program in the launcher NRO: install it and
   * restart into it before anything else happens (dcr_setup.c). */
  dcr_setup_update_from_nro();

  if (!find_apk())
    fatal_error("No APK of Angry Birds Space HD in %s.\n\n"
                "Copy the APK of your own Angry Birds Space HD (com.rovio.angrybirdsspaceHD,\n"
                "2.2.14, armeabi-v7a) into that folder, under any name ending in .apk:\n"
                "the game reads its data from it, and its library is unpacked from it\n"
                "on the first launch.",
                g_root);
  const char *apk = g_apk;
  void dcr_apkcache_set_path(const char *real);
  dcr_apkcache_set_path(apk); /* the asset reads of it are cached (dcr_apkcache.c) */
  if (dcr_manifest_load(apk) != 0)
    fatal_error("%s is unreadable.\n\nCopy your APK of Angry Birds Space HD there again.", apk);
  if (strcmp(dcr_manifest_package(), DCR_PACKAGE))
    debugPrintf("[boot] WARNING: the APK is %s, not %s\n", dcr_manifest_package(), DCR_PACKAGE);
  debugPrintf("[boot] APK %s: %s %s (%d)\n", strrchr(apk, '/') + 1, dcr_manifest_package(),
              dcr_manifest_version_name(), dcr_manifest_version_code());

  if (dcr_self_process() == INVALID_HANDLE)
    fatal_error("Could not obtain a handle to this process.\n"
                "The loader needs it to map the game's code.");

  /* libAngryBirdsSpace.so and classes.txt, from the APK when they are
   * missing or it has changed */
  dcr_setup_from_apk(apk);
  if (abs_assets_init(apk) != 0)
    fatal_error("Could not read the game's files from %s.\n\n"
                "Is it the APK of Angry Birds Space HD (it must hold assets/data/...)?",
                apk);
  if (abs_load_module() != 0)
    fatal_error("Could not load the game library from %s.\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n"
                ABS_LIB_GAME " and .setup there to unpack it again.",
                g_root);

  /* The main thread becomes a guest thread like the game's own: priority
   * 59 on cores 0-2, where the kernel time-slices (dcr_sched.c). */
  dcr_sched_init();
  {
    abs_audio_selftest();
    void dcr_pthread_selftest(void);
    dcr_pthread_selftest();
    void dcr_io_selftest(void);
    dcr_io_selftest();
  }
#if DCR_GL_MESA
  if (dcr_is_emulator() || dcr_config()->gl_selftest) {
    int dcr_gl_selftest(void);
    dcr_gl_selftest();
  }
#endif

  /* System.loadLibrary: the engine's constructors, then (abs_boot.c)
   * JNI_OnLoad and the activity. */
  abs_run_constructors();
  abs_boot_run();
  debugPrintf("[boot] exiting\n");
  log_flush_ring();
  return 0;
}
