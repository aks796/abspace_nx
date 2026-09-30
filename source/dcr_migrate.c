/* dcr_migrate.c -- an older install's folder, moved into this one.
 *
 * Up to build 202609280910 the game folder was /switch/abspace (and the
 * launcher AngryBirdsSpace.nro); it is /switch/abspace_nx now (abspace_nx.nro).
 * When the old folder is there, what is in it moves into the new one by
 * rename -- the APK, config.ini, pointer.cfg, the saves (data/), the unpacked
 * library, videos/, the Orbital Escapade mod -- nothing is copied. What the
 * new folder has already stays, and a folder in both is merged entry by
 * entry. A launcher NRO stays behind unless it carries this build or a newer
 * one (then it comes along, for the next update), and so does the one given
 * as `skip` (the launcher that is running). The old folder goes once it is
 * empty. Free of libnx: the game program (dcr_setup.c) and the launcher both
 * run it, whichever starts first. MIT.
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dcr_formats.h"
#include "dcr_migrate.h"

typedef struct {
  unsigned moved, kept;
} Moved;

static int is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* a folder's names, read whole before any is renamed away (NULL-ended) */
static char **list_names(const char *dir) {
  DIR *d = opendir(dir);
  if (!d)
    return NULL;
  char **v = NULL;
  size_t n = 0, cap = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    if (n + 2 > cap) {
      cap = cap ? cap * 2 : 64;
      char **nv = realloc(v, cap * sizeof *nv);
      if (!nv)
        break;
      v = nv;
    }
    v[n] = strdup(e->d_name);
    if (v[n])
      n++;
  }
  closedir(d);
  if (v)
    v[n] = NULL;
  return v;
}

static void free_names(char **v) {
  for (size_t i = 0; v && v[i]; i++)
    free(v[i]);
  free(v);
}

static void move_into(const char *from, const char *to, int top, uint64_t build, const char *skip, Moved *m,
                      int depth) {
  char **names = list_names(from);
  for (size_t i = 0; names && names[i]; i++) {
    const char *nm = names[i];
    char src[768], dst[768];
    snprintf(src, sizeof src, "%s/%s", from, nm);
    snprintf(dst, sizeof dst, "%s/%s", to, nm);
    size_t l = strlen(nm);
    if (top && skip && !strcasecmp(nm, skip)) {
      m->kept++;
      continue;
    }
    if (top && l > 4 && !strcasecmp(nm + l - 4, ".nro")) {
      FILE *f = fopen(src, "rb");
      uint64_t b = f ? nro_build(f) : 0;
      if (f)
        fclose(f);
      if (!b || b < build) {
        m->kept++; /* an older launcher (or not this game's) */
        continue;
      }
    }
    struct stat ds;
    if (stat(dst, &ds) != 0) {
      if (rename(src, dst) == 0)
        m->moved++;
      else
        m->kept++;
    } else if (S_ISDIR(ds.st_mode) && is_dir(src) && depth < 8) {
      move_into(src, dst, 0, build, skip, m, depth + 1);
      rmdir(src); /* if it is empty now */
    } else {
      m->kept++; /* the new folder has its own */
    }
  }
  free_names(names);
}

int dcr_migrate_folder(const char *old_root, const char *new_root, uint64_t build, const char *skip, char *msg,
                       size_t cap) {
  if (msg && cap)
    msg[0] = 0;
  if (!is_dir(old_root))
    return 0;
  mkdir(new_root, 0777);
  Moved m = {0, 0};
  move_into(old_root, new_root, 1, build, skip, &m, 0);
  int gone = rmdir(old_root) == 0;
  if (msg && cap) {
    if (gone)
      snprintf(msg, cap, "moved %u item%s from %s into %s; the old folder is gone", m.moved, m.moved == 1 ? "" : "s",
               old_root, new_root);
    else
      snprintf(msg, cap, "moved %u item%s from %s into %s; %u left there (an older launcher, or what %s has already)",
               m.moved, m.moved == 1 ? "" : "s", old_root, new_root, m.kept, new_root);
  }
  return m.moved > 0 || gone ? 1 : (m.kept ? 2 : 0);
}
