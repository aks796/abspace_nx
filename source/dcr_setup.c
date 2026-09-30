/* dcr_setup.c -- a first launch from nothing but the player's APK.
 *
 * The game folder (/switch/abspace_nx) needs only the user's own APK, under
 * any name (main.c find_apk), and the launcher NRO (launcher/). Everything else is made from the
 * APK here, before anything is loaded:
 *   libAngryBirdsSpace.so   lib/armeabi-v7a/ out of the APK
 *   classes.txt             the Java class names its classes*.dex define
 *                           (jni_core.c answers FindClass / ClassLoader.findClass
 *                           with exactly those)
 * and made again whenever the APK changes: .setup records the CRC-32 of each
 * source entry. Files that are already right are checked once and kept. The
 * game reads its data straight out of the APK (abs_assets.c), so the APK
 * itself is left as it is.
 *
 * UPDATES FROM THE NRO. This program runs as a forwarder title through an
 * ExeFS override, /atmosphere/contents/<title id>/exefs.nsp, which the
 * launcher wrote on its first run (it carries abspace_nx.nsp in its romfs).
 * When the NRO in the game folder carries a NEWER build than the one running
 * (romfs:/abspace_nx.build against DCR_BUILD), the override is rewritten from
 * it (dcr_exefs.h) and the program restarts into the new build: updating is
 * copying the new NRO over the old one. Never a downgrade, never a file this
 * program did not come from (the override must exist and name this title),
 * and never twice for the same build (.update records the attempt). MIT.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "config.h"
#include "dcr_build.h"
#include "dcr_config.h"
#include "dcr_exefs.h"
#include "dcr_formats.h"
#include "dcr_migrate.h"
#include "error.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

static const char *const k_libs[] = {ABS_LIB_GAME};
#define ABI_DIR "lib/armeabi-v7a/"

static void root_path(char *out, size_t cap, const char *name) {
  snprintf(out, cap, "%s/%s", dcr_game_root(), name);
}

/* Setup work shows on screen as a progress bar with what is being done (util.c
 * log_console_progress); the log it writes goes to debug.log only -- or
 * scrolls on screen instead with [debug] boot_log_on_screen. */
static int g_setup_shown;
static void setup_progress(const char *what, int permille) {
  if (!g_setup_shown) {
    g_setup_shown = 1;
    debugPrintf("[setup] setting up Angry Birds Space -- this happens once\n");
  }
  log_console_progress(what, permille);
  log_console_update(); /* the log on screen, if it is */
}

static long file_size(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (long)st.st_size : -1;
}

/* ---------------------------------------------------------------- .setup */
/* One line per made file: "<name> <crc of its source> <size>". */
#define MAX_STAMP 8
typedef struct {
  char name[32];
  unsigned long crc, size;
} Stamp;
static Stamp g_stamp[MAX_STAMP];
static int g_nstamp, g_stamp_dirty;

static void stamp_load(void) {
  char path[300];
  root_path(path, sizeof path, ".setup");
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  while (g_nstamp < MAX_STAMP && fscanf(f, "%31s %lx %lu", g_stamp[g_nstamp].name,
                                        &g_stamp[g_nstamp].crc, &g_stamp[g_nstamp].size) == 3)
    g_nstamp++;
  fclose(f);
}

static Stamp *stamp_get(const char *name) {
  for (int i = 0; i < g_nstamp; i++)
    if (!strcmp(g_stamp[i].name, name))
      return &g_stamp[i];
  return NULL;
}

static void stamp_set(const char *name, unsigned long crc, unsigned long size) {
  Stamp *s = stamp_get(name);
  if (!s && g_nstamp < MAX_STAMP) {
    s = &g_stamp[g_nstamp++];
    snprintf(s->name, sizeof s->name, "%s", name);
  }
  if (s && (s->crc != crc || s->size != size)) {
    s->crc = crc;
    s->size = size;
    g_stamp_dirty = 1;
  }
}

static void stamp_save(void) {
  if (!g_stamp_dirty)
    return;
  char path[300];
  root_path(path, sizeof path, ".setup");
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  for (int i = 0; i < g_nstamp; i++)
    fprintf(f, "%s %08lx %lu\n", g_stamp[i].name, g_stamp[i].crc, g_stamp[i].size);
  fclose(f);
}

/* ------------------------------------------------------------- libraries */
static unsigned long crc_of_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  static unsigned char buf[1 << 16];
  mz_ulong crc = mz_crc32(0, NULL, 0);
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    crc = mz_crc32(crc, buf, n);
  fclose(f);
  return (unsigned long)crc;
}

/* Write to <dst>.part, then put it in place: a half-written library is never
 * mistaken for a whole one. */
static int extract_entry(mz_zip_archive *zip, int idx, const char *dst) {
  char tmp[320];
  snprintf(tmp, sizeof tmp, "%s.part", dst);
  unlink(tmp);
  if (!mz_zip_reader_extract_to_file(zip, (mz_uint)idx, tmp, 0)) {
    unlink(tmp);
    return -1;
  }
  unlink(dst);
  return rename(tmp, dst);
}

static void ensure_libs(mz_zip_archive *zip, const char *apk) {
  for (unsigned i = 0; i < sizeof k_libs / sizeof k_libs[0]; i++) {
    char arc[64], dst[300];
    snprintf(arc, sizeof arc, ABI_DIR "%s", k_libs[i]);
    root_path(dst, sizeof dst, k_libs[i]);
    int idx = mz_zip_reader_locate_file(zip, arc, NULL, 0);
    mz_zip_archive_file_stat st;
    if (idx < 0 || !mz_zip_reader_file_stat(zip, (mz_uint)idx, &st))
      fatal_error("%s has no %s.\n\n"
                  "This port needs Angry Birds Space HD 2.2.14 (com.rovio.angrybirdsspaceHD,\n"
                  "armeabi-v7a): use the APK of that version.",
                  apk, arc);
    unsigned long crc = (unsigned long)st.m_crc32, size = (unsigned long)st.m_uncomp_size;
    long have = file_size(dst);
    Stamp *s = stamp_get(k_libs[i]);
    if (have == (long)size && s && s->crc == crc)
      continue; /* made from this APK before */
    if (have == (long)size && crc_of_file(dst) == crc) {
      stamp_set(k_libs[i], crc, size); /* already the right file */
      continue;
    }
    setup_progress("Unpacking the game", 100);
    debugPrintf("[setup] unpacking %s from the APK (%lu KB)...\n", k_libs[i], size >> 10);
    if (extract_entry(zip, idx, dst) != 0)
      fatal_error("Could not write %s (from %s).\n\nIs the SD card full or read-only?", dst, apk);
    stamp_set(k_libs[i], crc, size);
    setup_progress("Unpacking the game", 500);
  }
}

/* ------------------------------------------------------------ classes.txt */
static int cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void ensure_classes(mz_zip_archive *zip) {
  /* classes.dex, classes2.dex, ... at the top of the APK */
  int idx[32], n = 0;
  mz_ulong crc = mz_crc32(0, NULL, 0);
  for (int k = 1; k <= 32 && n < 32; k++) {
    char nm[32];
    if (k == 1)
      snprintf(nm, sizeof nm, "classes.dex");
    else
      snprintf(nm, sizeof nm, "classes%d.dex", k);
    int i = mz_zip_reader_locate_file(zip, nm, NULL, 0);
    mz_zip_archive_file_stat st;
    if (i < 0 || !mz_zip_reader_file_stat(zip, (mz_uint)i, &st))
      break;
    idx[n++] = i;
    uint32_t c = st.m_crc32;
    crc = mz_crc32(crc, (const unsigned char *)&c, sizeof c);
  }
  char dst[300];
  root_path(dst, sizeof dst, "classes.txt");
  Stamp *s = stamp_get("classes.txt");
  if (!n || (s && s->crc == (unsigned long)crc && s->size == (unsigned long)n && file_size(dst) > 0))
    return; /* nothing to read, or already made from these */

  setup_progress("Reading the game's Java classes", 550);
  debugPrintf("[setup] listing the Java classes of the APK (%d dex file%s)...\n", n, n > 1 ? "s" : "");
  Names ns = {0};
  for (int k = 0; k < n; k++) {
    if (k)
      setup_progress("Reading the game's Java classes", 550 + k * 400 / n);
    size_t len = 0;
    void *d = mz_zip_reader_extract_to_heap(zip, (mz_uint)idx[k], &len, 0);
    if (d) {
      dex_names(d, len, &ns);
      free(d);
    }
  }
  if (ns.n) {
    qsort(ns.v, (size_t)ns.n, sizeof *ns.v, cmp_str);
    char tmp[320];
    snprintf(tmp, sizeof tmp, "%s.part", dst);
    FILE *f = fopen(tmp, "w");
    int written = 0;
    if (f) {
      fputs("# Java classes defined by the game's APK (names only). The wrapper's JNI\n"
            "# FindClass/Class.forName report exactly these, plus the Android framework.\n", f);
      for (int i = 0; i < ns.n; i++)
        if (i == 0 || strcmp(ns.v[i], ns.v[i - 1])) {
          fputs(ns.v[i], f);
          fputc('\n', f);
          written++;
        }
      if (fclose(f) == 0) {
        unlink(dst);
        if (rename(tmp, dst) == 0) {
          stamp_set("classes.txt", (unsigned long)crc, (unsigned long)n);
          debugPrintf("[setup] classes.txt: %d Java class names\n", written);
        }
      }
    }
  }
  for (int i = 0; i < ns.n; i++)
    free(ns.v[i]);
  free(ns.v);
}

void dcr_setup_from_apk(const char *apk) {
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, apk, 0))
    return; /* main.c reports a missing or unreadable APK */
  stamp_load();
  ensure_libs(&zip, apk);
  ensure_classes(&zip);
  mz_zip_reader_end(&zip);
  stamp_save();
  if (g_setup_shown)
    setup_progress("Starting the game", 1000);
}

/* ---------------------------------------------------- updates from the NRO */
/* The newest launcher NRO in the game folder: its path and build. */
static uint64_t find_nro(char *path, size_t cap) {
  uint64_t best = 0;
  DIR *d = opendir(dcr_game_root());
  if (!d)
    return 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n < 5 || strcasecmp(e->d_name + n - 4, ".nro"))
      continue;
    char p[320];
    root_path(p, sizeof p, e->d_name);
    FILE *f = fopen(p, "rb");
    if (!f)
      continue;
    uint64_t b = nro_build(f);
    fclose(f);
    if (b > best) {
      best = b;
      snprintf(path, cap, "%s", p);
    }
  }
  closedir(d);
  return best;
}

static uint8_t *read_whole(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *len = b ? (size_t)n : 0;
  return b;
}

/* An install from before the folder was renamed (/switch/abspace, up to
 * build 202609280910): moved into this one at the first start, before
 * anything is read from it (dcr_migrate.c). */
void dcr_setup_move_old_folder(void) {
  char msg[512];
  if (dcr_migrate_folder("sdmc:" DCR_OLD_ROOT_PATH, dcr_game_root(), DCR_BUILD, NULL, msg, sizeof msg) == 1)
    debugPrintf("[setup] %s\n", msg);
}

void dcr_setup_update_from_nro(void) {
  u64 tid = 0;
  if (R_FAILED(svcGetInfo(&tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) || !exefs_is_forwarder_tid(tid))
    return;
  char ovr[128], marker[300], nro[320];
  snprintf(ovr, sizeof ovr, "sdmc:/atmosphere/contents/%016llX/exefs.nsp", (unsigned long long)tid);
  root_path(marker, sizeof marker, ".update");
  if (file_size(ovr) <= 0)
    return; /* not running through an override: nothing of ours to update */

  uint64_t attempted = 0;
  FILE *mf = fopen(marker, "r");
  if (mf) {
    if (fscanf(mf, "%llu", (unsigned long long *)&attempted) != 1)
      attempted = 0;
    fclose(mf);
    if (attempted <= DCR_BUILD)
      unlink(marker); /* that update took */
  }
  uint64_t build = find_nro(nro, sizeof nro);
  debugPrintf("[setup] build %llu%s\n", (unsigned long long)DCR_BUILD,
              build > DCR_BUILD ? "; the launcher NRO carries a newer one" : "");
  if (build <= DCR_BUILD)
    return;
  if (attempted == build) {
    debugPrintf("[setup] %s: build %llu was installed but this is still build %llu -- not retrying "
                "(delete %s to try again)\n", nro, (unsigned long long)build,
                (unsigned long long)DCR_BUILD, marker);
    return;
  }

  /* the override must be this program's own: 32-bit, this title */
  size_t cur_len = 0, npdm_len, nsp_len = 0;
  uint8_t *cur = read_whole(ovr, &cur_len);
  const uint8_t *npdm;
  uint64_t pid = 0;
  int is64 = 1;
  int ours = cur && exefs_find(cur, cur_len, "main.npdm", &npdm, &npdm_len) == 0 &&
             npdm_info(npdm, npdm_len, &pid, &is64) == 0 && pid == tid && !is64;
  free(cur);
  if (!ours)
    return;

  FILE *f = fopen(nro, "rb");
  long off;
  uint8_t *nsp = NULL, *out = NULL;
  size_t out_len = 0;
  if (f && nro_romfs_file(f, "abspace_nx.nsp", &off, &nsp_len) == 0 && (nsp = malloc(nsp_len)) &&
      fseek(f, off, SEEK_SET) == 0 && fread(nsp, 1, nsp_len, f) == nsp_len)
    exefs_build_override(nsp, nsp_len, tid, &out, &out_len);
  if (f)
    fclose(f);
  free(nsp);
  if (!out) {
    debugPrintf("[setup] %s: its copy of the wrapper is unreadable -- not updating\n", nro);
    return;
  }
  setup_progress("Updating to the new build, then restarting", 1000);
  debugPrintf("[setup] updating to build %llu from %s, then restarting...\n", (unsigned long long)build, nro);
  char tmp[160];
  snprintf(tmp, sizeof tmp, "%s.part", ovr);
  FILE *o = fopen(tmp, "wb");
  int ok = o && fwrite(out, 1, out_len, o) == out_len;
  if (o && fclose(o) != 0)
    ok = 0;
  free(out);
  if (ok) {
    unlink(ovr);
    ok = rename(tmp, ovr) == 0;
  }
  if (!ok) {
    unlink(tmp);
    debugPrintf("[setup] could not write %s -- still running build %llu\n", ovr, (unsigned long long)DCR_BUILD);
    return;
  }
  mf = fopen(marker, "w");
  if (mf) {
    fprintf(mf, "%llu\n", (unsigned long long)build);
    fclose(mf);
  }
  log_flush_ring();
  Result rc = appletRestartProgram(NULL, 0);
  fatal_error("Updated to build %llu from %s.\n\n"
              "Restarting did not work (0x%x): close the game and launch it again.",
              (unsigned long long)build, nro, (unsigned)rc);
}
