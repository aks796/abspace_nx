/* dcr_config.c -- <game folder>/config.ini, the user's settings.
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file is appended to (options a newer build adds,
 * at the end, with their defaults), so edits and comments survive updates.
 * Plain INI: [section], key = value, # comments; booleans take true/false,
 * yes/no, on/off, 1/0. Read once at start-up: changes apply the next time the
 * game starts. (The machinery is the Crossy Road port's; the options are
 * Angry Birds Space's.) MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "dcr_build.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_game_root(void);        /* main.c */
void dcr_window_set_size(int w, int h); /* android_ndk.c */

static DcrConfig g_cfg = {
    .aim = ABS_AIM_PULL,
    .aim_speed = 1.0f,
    .pointer_speed = 14.0f,
    .menu_focus = 1,
    .scheme = ABS_SCHEME_CONSOLE,
    .cursor_size = 1.0f,
    .free_purchases = 1,
    .planet_info = 1,
    .froot_loops = 1,
    .tex_mb = 200,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
    .lua_bridge = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

enum { K_BOOL, K_CHOICE, K_TEXT };

typedef struct {
  const char *section, *key, *def, *help;
  int kind;
  const char *choices; /* K_CHOICE: comma-separated, index = value */
} Opt;

static const Opt k_opts[] = {
    {"game", "language", "auto",
     "auto (the console's language) or one of: en fr de it es pt ru nl sv da fi\n"
     "# nb pl tr ja ko zh (zh = simplified, zh_tw = traditional).",
     K_TEXT, NULL},
    {"game", "free_purchases", "true",
     "The shop is long closed: true makes everything in it free (Space Eagles,\n"
     "# power-ups, the paid episodes), granted the way a purchase was.", K_BOOL, NULL},
    {"game", "planet_info", "true",
     "The tiny planets on the episode screen (NASA, the Mars rover, Facebook...)\n"
     "# and the Solar System's planets opened web pages. true: they open a popup\n"
     "# about their topic in the game instead, with its video: streamed from\n"
     "# YouTube when the console is online, or from the videos/ folder.", K_BOOL, NULL},
    {"game", "froot_loops", "true",
     "Froot Loops Bloopers, the Kellogg's episode (5 levels and 2 comics, a sign\n"
     "# by the Brass Hogs planet). The game showed it only where Rovio's server\n"
     "# offered it (the US, during the promotion). true: always shown.", K_BOOL, NULL},
    {"controls", "swap_a_b", "false",
     "Swap A and B (true: B launches and selects, A goes back).", K_BOOL, NULL},
    {"controls", "aim", "pull",
     "pull: the left stick pulls the bird back the way it points, as in Angry\n"
     "# Birds Trilogy (push the stick left to fire right). push: the stick points\n"
     "# the way the bird will fly.",
     K_CHOICE, "pull,push"},
    {"controls", "fine_aim_speed", "1.0",
     "How far one D-pad press (or ZL held with the stick) moves the aim while a\n"
     "# bird is pulled back: 0.25 to 4.", K_TEXT, NULL},
    {"controls", "pointer_speed", "14",
     "Speed of the hand cursor (cursor controls): pixels per frame at full\n"
     "# stick, 4 to 40. D-pad up/down changes it while playing (saved in\n"
     "# pointer.cfg, which then wins over this).", K_TEXT, NULL},
    {"controls", "menu_focus", "true",
     "Menus: the D-pad and the left stick jump between the buttons on screen and\n"
     "# A presses the highlighted one. false: the hand cursor in menus instead.", K_BOOL, NULL},
    {"controls", "scheme", "console",
     "The controls at start. console: Angry Birds Trilogy's (aim with the stick,\n"
     "# highlighted buttons). cursor: Angry Birds Reloaded's hand cursor (tap and\n"
     "# drag with A, ZL or ZR). R switches between them while playing.",
     K_CHOICE, "console,cursor"},
    {"controls", "cursor_size", "1.0",
     "Size of the hand cursor (Orbital Escapade's hand), 0.5 to 3.", K_TEXT, NULL},
    {"display", "resolution", "720",
     "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
     "# starts). The Switch scales the picture to the screen either way; the\n"
     "# game's art is made for 1024x768.",
     K_CHOICE, NULL},
    {"performance", "boost_cpu_when_loading", "true",
     "CPU at 1785 MHz while the game starts (until its first picture) and\n"
     "# inside loading frames (those over 50 ms), normal otherwise.",
     K_BOOL, NULL},
    {"performance", "texture_memory_mb", "200",
     "Memory the game may keep its pictures in, in MB (16 to 1024; 0 = the\n"
     "# 60 MB it allows itself on Android, which makes it reload them often).",
     K_TEXT, NULL},
    {"debug", "gl_selftest", "false", "Graphics self-test picture at start-up.", K_BOOL, NULL},
    {"debug", "boot_log_on_screen", "false",
     "Show the start-up log on screen at every launch. Off: the log appears only\n"
     "# while something is being set up (first launch, a new APK or NRO).",
     K_BOOL, NULL},
    {"debug", "log_java_calls", "false",
     "Write every Java method the game calls to debug.log (slow; for bug reports).", K_BOOL,
     NULL},
    {"debug", "lua_bridge", "true",
     "The controller script that runs inside the game (aiming, camera, menu\n"
     "# buttons). false: touch and the free pointer only.", K_BOOL, NULL},
    {"debug", "log_lua", "false", "Log the controller script's view of the game once a second.",
     K_BOOL, NULL},
    {"config", "version", "1", "Settings file format; leave as it is.", K_TEXT, NULL},
};
#define O_COUNT ((int)(sizeof k_opts / sizeof k_opts[0]))

static char g_val[O_COUNT][24];
static int g_have[O_COUNT];

static int opt_index(const char *section, const char *key) {
  for (int i = 0; i < O_COUNT; i++)
    if (!strcmp(k_opts[i].section, section) && !strcmp(k_opts[i].key, key))
      return i;
  return -1;
}

static void path_of(char *out, size_t cap, const char *name) {
  snprintf(out, cap, "%s/%s", dcr_game_root(), name);
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    *--e = 0;
  return s;
}

static void parse(FILE *f) {
  char line[256], section[32] = "";
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      char *e = strchr(s, ']');
      if (e) {
        *e = 0;
        snprintf(section, sizeof section, "%s", trim(s + 1));
      }
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq)
      continue;
    *eq = 0;
    char *key = trim(s), *val = trim(eq + 1);
    char *hash = strpbrk(val, "#;");
    if (hash) {
      *hash = 0;
      val = trim(val);
    }
    for (int i = 0; i < O_COUNT; i++)
      if (!strcasecmp(section, k_opts[i].section) && !strcasecmp(key, k_opts[i].key)) {
        snprintf(g_val[i], sizeof g_val[i], "%s", val);
        g_have[i] = 1;
      }
  }
}

static void write_opts(FILE *f, int only_missing) {
  const char *last = NULL;
  for (int i = 0; i < O_COUNT; i++) {
    if (only_missing && g_have[i])
      continue;
    if (!last || strcmp(last, k_opts[i].section)) {
      fprintf(f, "\n[%s]\n", k_opts[i].section);
    }
    last = k_opts[i].section;
    if (k_opts[i].help)
      fprintf(f, "# %s\n", k_opts[i].help);
    fprintf(f, "%s = %s\n", k_opts[i].key, g_val[i]);
  }
}

static int as_bool(int i) {
  const char *v = g_val[i];
  if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcmp(v, "1"))
    return 1;
  if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off") || !strcmp(v, "0"))
    return 0;
  debugPrintf("[config] %s = %s: not true/false, using %s\n", k_opts[i].key, v, k_opts[i].def);
  return !strcmp(k_opts[i].def, "true");
}

/* index of the value in the option's choice list, 0 (the first) if unknown */
static int as_choice(int i) {
  const char *v = g_val[i];
  const char *c = k_opts[i].choices;
  for (int idx = 0; c && *c; idx++) {
    const char *e = strchr(c, ',');
    size_t n = e ? (size_t)(e - c) : strlen(c);
    if (strlen(v) == n && !strncasecmp(v, c, n))
      return idx;
    if (!e)
      break;
    c = e + 1;
  }
  if (strcasecmp(v, k_opts[i].def))
    debugPrintf("[config] %s = %s: not one of %s, using %s\n", k_opts[i].key, v,
                k_opts[i].choices, k_opts[i].def);
  return 0;
}

static float as_float(int i, float lo, float hi) {
  float v = (float)atof(g_val[i]);
  if (!(v >= lo && v <= hi)) {
    debugPrintf("[config] %s = %s: not %g..%g, using %s\n", k_opts[i].key, g_val[i], lo, hi,
                k_opts[i].def);
    v = (float)atof(k_opts[i].def);
  }
  return v;
}

void dcr_config_load(void) {
  for (int i = 0; i < O_COUNT; i++)
    snprintf(g_val[i], sizeof g_val[i], "%s", k_opts[i].def);
  char path[300];
  path_of(path, sizeof path, "config.ini");
  FILE *f = fopen(path, "r");
  if (f) {
    parse(f);
    fclose(f);
    int missing = 0;
    for (int i = 0; i < O_COUNT; i++)
      missing += !g_have[i];
    if (missing && (f = fopen(path, "a"))) {
      fprintf(f, "\n# Added by build %llu (new options, at their defaults):\n",
              (unsigned long long)DCR_BUILD);
      write_opts(f, 1);
      fclose(f);
      debugPrintf("[config] added %d new option%s to config.ini\n", missing, missing > 1 ? "s" : "");
    }
  } else if ((f = fopen(path, "w"))) {
    fputs("# Angry Birds Space for Switch -- settings.\n"
          "# Changes apply the next time the game starts. Delete this file to get\n"
          "# the defaults back.\n",
          f);
    write_opts(f, 0);
    fclose(f);
    debugPrintf("[config] wrote config.ini with the defaults\n");
  }

  const char *lang = g_val[opt_index("game", "language")];
  g_cfg.locale[0] = 0;
  if (strcasecmp(lang, "auto"))
    snprintf(g_cfg.locale, sizeof g_cfg.locale, "%s", lang);
  g_cfg.swap_ab = as_bool(opt_index("controls", "swap_a_b"));
  g_cfg.aim = as_choice(opt_index("controls", "aim"));
  g_cfg.aim_speed = as_float(opt_index("controls", "fine_aim_speed"), 0.25f, 4.0f);
  g_cfg.pointer_speed = as_float(opt_index("controls", "pointer_speed"), 4.0f, 40.0f);
  g_cfg.menu_focus = as_bool(opt_index("controls", "menu_focus"));
  g_cfg.scheme = as_choice(opt_index("controls", "scheme"));
  g_cfg.cursor_size = as_float(opt_index("controls", "cursor_size"), 0.5f, 3.0f);
  g_cfg.free_purchases = as_bool(opt_index("game", "free_purchases"));
  g_cfg.planet_info = as_bool(opt_index("game", "planet_info"));
  g_cfg.froot_loops = as_bool(opt_index("game", "froot_loops"));
  {
    int i = opt_index("performance", "texture_memory_mb");
    int mb = atoi(g_val[i]);
    if (mb != 0 && (mb < 16 || mb > 1024)) {
      debugPrintf("[config] texture_memory_mb = %s: not 0 or 16..1024, using %s\n", g_val[i], k_opts[i].def);
      mb = atoi(k_opts[i].def);
    }
    g_cfg.tex_mb = mb;
  }
  g_cfg.boost = as_bool(opt_index("performance", "boost_cpu_when_loading"));
  g_cfg.gl_selftest = as_bool(opt_index("debug", "gl_selftest"));
  g_cfg.boot_log = as_bool(opt_index("debug", "boot_log_on_screen"));
  g_cfg.log_jni = as_bool(opt_index("debug", "log_java_calls"));
  g_cfg.lua_bridge = as_bool(opt_index("debug", "lua_bridge"));
  g_cfg.log_lua = as_bool(opt_index("debug", "log_lua"));

  const char *r = g_val[opt_index("display", "resolution")];
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  int h = !strcmp(r, "720") ? 720 : !strcmp(r, "1080") ? 1080 : !strcasecmp(r, "auto") ? (docked ? 1080 : 720) : 0;
  if (!h) {
    debugPrintf("[config] resolution = %s: not 720, 1080 or auto, using 720\n", r);
    h = 720;
  }
  g_cfg.res_h = h;
  g_cfg.res_w = h * 16 / 9;
  dcr_window_set_size(g_cfg.res_w, g_cfg.res_h);

  debugPrintf("[config] language %s; %dx%d (%s, %s); A/B %s; aim %s, fine aim %.2f, pointer %.0f; "
              "menu focus %s; controls %s; free shop %s; planet pages %s; Froot Loops %s; textures %d MB; CPU boost %s; "
              "Lua bridge %s\n",
              g_cfg.locale[0] ? g_cfg.locale : "auto", g_cfg.res_w, g_cfg.res_h, r,
              docked ? "docked" : "handheld", g_cfg.swap_ab ? "swapped" : "normal",
              g_cfg.aim == ABS_AIM_PULL ? "pull" : "push", (double)g_cfg.aim_speed,
              (double)g_cfg.pointer_speed, g_cfg.menu_focus ? "on" : "off",
              g_cfg.scheme == ABS_SCHEME_CURSOR ? "cursor" : "console", g_cfg.free_purchases ? "on" : "off",
              g_cfg.planet_info ? "on" : "off", g_cfg.froot_loops ? "on" : "off", g_cfg.tex_mb, g_cfg.boost ? "on" : "off",
              g_cfg.lua_bridge ? "on" : "off");
}
