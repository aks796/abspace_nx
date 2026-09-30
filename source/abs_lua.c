/* abs_lua.c -- the controller bridge into the game's Lua.
 *
 * Angry Birds Space's gameplay and menus are Lua 5.1 scripts (compiled,
 * encrypted, in the APK), and none of them reads a gamepad: aiming, the
 * camera and every button are touch. To play it the way Angry Birds Trilogy
 * plays on consoles, the port has to know where the bird sits on screen, how
 * far the rubber band stretches, whether a bird is in the air with its power
 * unused, and where the buttons of the current menu are. All of that is in
 * the game's Lua globals, so the port asks there:
 *
 *   - Lua's C API is inside libAngryBirdsSpace.so (statically linked, not
 *     exported). The functions it needs were located by hand in 2.2.14
 *     (luaB_loadstring / luaB_pcall / luaB_tonumber / ... in the base
 *     library's luaL_Reg table lead to luaL_loadbuffer, lua_pcall,
 *     lua_gettop, lua_settop, lua_tolstring); their first two instruction
 *     words are checked before anything is used. Another build of the game
 *     fails the check, and the bridge stays off (touch and the pointer still
 *     work).
 *   - A way in, on the right thread and at a safe moment: before any Lua
 *     state exists, a few entries of the math library's luaL_Reg table
 *     (math.floor, min, max, abs, sqrt, sin, cos, atan2) are pointed at
 *     wrappers here. The game calls them all the time from its own Lua;
 *     the first call after each update request runs abs_ctl.lua's
 *     __abs.frame() -- inside the game's state, on the game's thread, with a
 *     Lua call already in progress -- and then does the real math. Nothing
 *     in the engine's code is changed.
 *   - abs_ctl.lua (built in: abs_res.S) returns one line of numbers, parsed
 *     into the snapshot abs_input.c reads (the level, the menu's buttons,
 *     the planet carousel), and carries out the camera, pause, paging and
 *     carousel requests queued here. It also makes the shop free, opens the
 *     tiny planets' links in a popup of the game's own kind (its "Watch
 *     video" comes back here: abs_video.c), shows the Froot Loops Bloopers
 *     episode and gives the game a bigger texture budget (config.ini). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "dcr_config.h"
#include "so_util.h"
#include "abs_net.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *L);

extern const char abs_ctl_lua[];          /* abs_res.S */
extern const uint32_t abs_ctl_lua_size;
extern const char abs_orbital_lua[]; /* abs_res.S: Orbital Escapade's worlds */
extern const uint32_t abs_orbital_lua_size;

/* ------------------------------------------------ 2.2.14's Lua, located */
typedef struct {
  const char *name;
  uint32_t off;         /* in libAngryBirdsSpace.so */
  uint32_t w0, w1;      /* its first two instructions (ARM) */
} Fn;

enum { F_LOADBUFFER, F_PCALL, F_GETTOP, F_SETTOP, F_TOLSTRING, F_COUNT };
static const Fn k_fn[F_COUNT] = {
    [F_LOADBUFFER] = {"luaL_loadbuffer", 0x3f2c28, 0xe52de004, 0xe24dd00c},
    [F_PCALL] = {"lua_pcall", 0x3ffe94, 0xe3530000, 0xe92d4030},
    [F_GETTOP] = {"lua_gettop", 0x3fc8a8, 0xe5902008, 0xe590300c},
    [F_SETTOP] = {"lua_settop", 0x3fc8bc, 0xe3510000, 0xba00000b},
    [F_TOLSTRING] = {"lua_tolstring", 0x3fdf94, 0xe92d4070, 0xe2515000},
};
#define MATHLIB_REG 0x6ea650 /* static const luaL_Reg mathlib[] (lmathlib.c) */
#define MATHLIB_N 28

static int (*p_loadbuffer)(lua_State *L, const char *buf, size_t sz, const char *name);
static int (*p_pcall)(lua_State *L, int nargs, int nresults, int errfunc);
static int (*p_gettop)(lua_State *L);
static void (*p_settop)(lua_State *L, int idx);
static const char *(*p_tolstring)(lua_State *L, int idx, size_t *len);

/* ------------------------------------------------ the math wrappers */
static void tick(lua_State *L);

#define HOOKS(X) X(floor) X(min) X(max) X(abs) X(sqrt) X(sin) X(cos) X(atan2)
#define DEF(n)                                                                   \
  static lua_CFunction o_##n;                                                    \
  static int h_##n(lua_State *L) {                                               \
    tick(L);                                                                     \
    return o_##n(L);                                                             \
  }
HOOKS(DEF)
#undef DEF

/* math.frexp takes one number; called with two strings it is the script's
 * way to ask the port for something (Orbital Escapade's files), and the
 * answer is what it returns:
 *   math.frexp('orb:present', '')   '1' when the mod is on the SD card
 *   math.frexp('orb:load', path)    the mod's Lua file (data/<path>), compiled
 *                                   into a function (or an error string)
 *   math.frexp('orb:sheets', names) its sheets holding these new sprites
 *   math.frexp('orb:gamesheets', names)  the game's sheets holding these
 *   math.frexp('orb:text', path)    a source file of the mod, as text */
static lua_CFunction o_frexp;
static int push_string(lua_State *L, const char *str) {
  size_t n = strlen(str);
  char *chunk = malloc(n + 32);
  if (!chunk)
    return 0;
  int len = snprintf(chunk, n + 32, "return [==[%s]==]", str);
  int ok = p_loadbuffer(L, chunk, (size_t)len, "=abs_answer") == 0 && p_pcall(L, 0, 1, 0) == 0;
  free(chunk);
  return ok ? 1 : 0;
}
static int h_frexp(lua_State *L) {
  if (p_gettop(L) != 2) {
    tick(L);
    return o_frexp(L);
  }
  const char *op = p_tolstring(L, 1, NULL), *arg = p_tolstring(L, 2, NULL);
  if (!op || !arg || strncmp(op, "orb:", 4))
    return o_frexp(L);
  if (!strcmp(op, "orb:present"))
    return push_string(L, abs_orbital_present() ? "1" : "");
  if (!strcmp(op, "orb:load")) {
    size_t n;
    uint8_t *plain = abs_orbital_lua_plain(arg, &n);
    if (!plain)
      return push_string(L, "not in orbital/data");
    char name[200];
    snprintf(name, sizeof name, "@orbital/%s", arg);
    p_loadbuffer(L, (const char *)plain, n, name); /* the function, or the error */
    free(plain);
    return 1;
  }
  if (!strcmp(op, "orb:sheets") || !strcmp(op, "orb:gamesheets") || !strcmp(op, "orb:parts") ||
      !strcmp(op, "orb:quoted") || !strcmp(op, "orb:size") || !strcmp(op, "orb:fullsheets")) {
    char *list = !strcmp(op, "orb:sheets")       ? abs_orbital_sheets_for(arg)
                 : !strcmp(op, "orb:gamesheets") ? abs_orbital_game_sheets_for(arg)
                 : !strcmp(op, "orb:parts")      ? abs_orbital_parts(arg)
                 : !strcmp(op, "orb:size")       ? abs_orbital_sprite_size(arg)
                 : !strcmp(op, "orb:fullsheets") ? abs_orbital_full_sheets_for(arg)
                                                 : abs_orbital_quoted(arg);
    int r = push_string(L, list ? list : "");
    free(list);
    return r;
  }
  if (!strcmp(op, "orb:trimmed"))
    return push_string(L, abs_orbital_trimmed() ? "1" : "");
  if (!strcmp(op, "orb:prune")) {
    char *r = abs_orbital_prune(arg, 0);
    int k = push_string(L, r ? r : "");
    free(r);
    return k;
  }
  if (!strcmp(op, "orb:compos")) {
    char n[16];
    snprintf(n, sizeof n, "%d", abs_orbital_group_composites(arg));
    return push_string(L, n);
  }
  if (!strcmp(op, "orb:text")) {
    /* a source file as a string: in a long bracket its text cannot close */
    size_t n;
    uint8_t *plain = abs_orbital_lua_plain(arg, &n);
    if (!plain || (n && plain[0] == 0x1b)) {
      free(plain);
      return push_string(L, "");
    }
    char eq[16] = "";
    for (int k = 0; k < 12; k++) {
      char close[20];
      snprintf(close, sizeof close, "]%s]", eq);
      if (!strstr((const char *)plain, close))
        break;
      strcat(eq, "=");
    }
    size_t cap = n + 64;
    char *chunk = malloc(cap);
    int r = 0;
    if (chunk) {
      int len = snprintf(chunk, cap, "return [%s[\n%s]%s]", eq, (const char *)plain, eq);
      r = p_loadbuffer(L, chunk, (size_t)len, "=abs_text") == 0 && p_pcall(L, 0, 1, 0) == 0;
      free(chunk);
    }
    free(plain);
    return r ? 1 : push_string(L, "");
  }
  return push_string(L, "");
}

static const struct {
  const char *name;
  lua_CFunction hook;
  lua_CFunction *orig;
} k_hooks[] = {
#define ROW(n) {#n, h_##n, &o_##n},
    HOOKS(ROW)
#undef ROW
    {"frexp", h_frexp, &o_frexp},
};

/* ------------------------------------------------ state */
static int g_enabled;                 /* verified and installed */
static volatile uint32_t g_want;      /* bumped once per game update */
static int g_busy;                    /* inside our own Lua (re-entry) */
static int g_failed;                  /* the script would not load */
static Mutex g_lock;                  /* g_snap, g_cmd */
static AbsLuaState g_snap;
static int g_cmds[8], g_ncmds;
static float g_pan, g_pany, g_zoom;
static volatile int g_focus_id; /* the controller's focused item */

/* One entry per Lua universe (global_State): the script is loaded in each,
 * and only the game's (the one with GameSystem) receives requests. */
typedef struct {
  void *G;
  uint32_t done;
  int loaded, is_game;
} Uni;
static Uni g_uni[6];

static void *global_of(lua_State *L) { return *(void **)((char *)L + 16); /* L->l_G */ }

static Uni *uni_for(void *G) {
  for (unsigned i = 0; i < sizeof g_uni / sizeof g_uni[0]; i++)
    if (g_uni[i].G == G)
      return &g_uni[i];
  for (unsigned i = 0; i < sizeof g_uni / sizeof g_uni[0]; i++)
    if (!g_uni[i].G) {
      g_uni[i].G = G;
      debugPrintf("[lua] a Lua universe (%p): the controller script goes in\n", G);
      return &g_uni[i];
    }
  return NULL;
}

int abs_lua_active(void) { return g_enabled && !g_failed && g_snap.valid; }

void abs_lua_snapshot(AbsLuaState *out) {
  mutexLock(&g_lock);
  *out = g_snap;
  mutexUnlock(&g_lock);
}

void abs_lua_command(int cmd) {
  mutexLock(&g_lock);
  if (g_ncmds < (int)(sizeof g_cmds / sizeof g_cmds[0]))
    g_cmds[g_ncmds++] = cmd;
  mutexUnlock(&g_lock);
}

void abs_lua_set_analog(float x, float y, float zoom) {
  g_pan = x;
  g_pany = y;
  g_zoom = zoom;
}

void abs_lua_frame_tick(void) { g_want++; }

void abs_lua_set_focus(int id) { g_focus_id = id; }

/* ------------------------------------------------ the script's answer */
static float next_f(const char **p) {
  char *end;
  float v = strtof(*p, &end);
  *p = end;
  return v;
}

/* the next space-separated word into out */
static void next_word(const char **p, char *out, size_t cap) {
  const char *s = *p;
  while (*s == ' ')
    s++;
  size_t n = 0;
  while (*s && *s != ' ') {
    if (n + 1 < cap)
      out[n++] = *s;
    s++;
  }
  out[n] = 0;
  *p = s;
}

static void parse(const char *s, Uni *u) {
  AbsLuaState st;
  memset(&st, 0, sizeof st);
  const char *p = s;
  u->is_game = (int)next_f(&p);
  if (!u->is_game)
    return; /* another state (the game's settings, a loader): nothing to show */
  st.valid = 1;
  st.mode = (int)next_f(&p);
  st.scr_w = next_f(&p);
  st.scr_h = next_f(&p);
  int ready = (int)next_f(&p);
  const float W = (float)abs_surface_w(), H = (float)abs_surface_h();
  float bx = next_f(&p), by = next_f(&p), lx = next_f(&p), ly = next_f(&p);
  st.pull = next_f(&p) * W;
  st.aiming = (int)next_f(&p);
  st.special = (int)next_f(&p);
  float tx = next_f(&p), ty = next_f(&p);
  st.bird_x = bx * W, st.bird_y = by * H;
  st.sling_x = lx * W, st.sling_y = ly * H;
  if (!ready && st.mode == ABS_MODE_AIM && !st.aiming)
    st.mode = ABS_MODE_WAIT;
  char req[160];
  next_word(&p, st.sig, sizeof st.sig);
  next_word(&p, req, sizeof req);
  const float clock = next_f(&p);
  st.mm = (int)next_f(&p);
  float vx = next_f(&p), vy = next_f(&p), vw = next_f(&p), vh = next_f(&p);
  st.carousel = (int)next_f(&p);
  int n = (int)next_f(&p);
  if (n > ABS_MAX_BUTTONS)
    n = ABS_MAX_BUTTONS;
  if (n < 0)
    n = 0;
  for (int i = 0; i < n; i++) {
    AbsItem *r = &st.buttons[i];
    r->x = next_f(&p) * W;
    r->y = next_f(&p) * H;
    r->w = next_f(&p) * W;
    r->h = next_f(&p) * H;
    r->ax = next_f(&p) * W;
    r->ay = next_f(&p) * H;
    r->kind = (int)next_f(&p);
    r->round = (int)next_f(&p);
    r->prio = (int)next_f(&p);
    r->id = (int)next_f(&p);
    r->grp = (int)next_f(&p);
    r->vis = (int)next_f(&p);
  }
  st.nbuttons = n;
  mutexLock(&g_lock);
  /* a HUD button to press (restart, the Space Eagle): abs_input.c taps it
   * before the next update -- never from in here, inside the game's frame */
  st.tap_x = g_snap.tap_x, st.tap_y = g_snap.tap_y, st.tap_seq = g_snap.tap_seq;
  if (tx >= 0 && ty >= 0) {
    st.tap_x = tx * W, st.tap_y = ty * H;
    st.tap_seq++;
  }
  st.frame = g_snap.frame + 1;
  g_snap = st;
  mutexUnlock(&g_lock);

  /* where the popup plays the video */
  if (vw > 0 && vh > 0)
    abs_video_set_rect(vx * W, vy * H, vw * W, vh * H);
  else
    abs_video_set_rect(0, 0, 0, 0);

  /* what the popups ask for: a video (streamed, or the card's file), the end
   * of it, the popup closed, or (the game's popup could not be made) the
   * port's own page */
  if (strcmp(req, "-")) {
    char *save = NULL;
    for (char *r = strtok_r(req, ",", &save); r; r = strtok_r(NULL, ",", &save)) {
      if (!strncmp(r, "play:", 5)) {
        char name[64] = "", yt[32] = "";
        const char *c = strchr(r + 5, ':');
        snprintf(name, sizeof name, "%.*s", c ? (int)(c - r - 5) : 63, r + 5);
        if (c)
          snprintf(yt, sizeof yt, "%s", c + 1);
        if (yt[0] && strcmp(yt, "-")) {
          debugPrintf("[lua] %s: streaming YouTube %s\n", name, yt);
          abs_video_play_youtube(yt);
        } else {
          char path[300];
          snprintf(path, sizeof path, "%s/videos/%s.mp4", dcr_game_root(), name);
          debugPrintf("[lua] %s: %s\n", name, path);
          abs_video_play(path);
        }
      } else if (!strcmp(r, "vstop")) {
        abs_video_stop();
      } else if (!strcmp(r, "closed")) {
        abs_video_stop();
        abs_input_popup_closed();
      } else if (!strncmp(r, "page:", 5) && r[5]) {
        debugPrintf("[lua] link %s: the game's popup could not be made; the port's page\n", r + 5);
        abs_extras_open(r + 5);
      } else if (!strncmp(r, "note:", 5) && r[5]) {
        /* a line for the log (spaces come as _: requests are words) */
        for (char *c = r + 5; *c; c++)
          if (*c == '_')
            *c = ' ';
        debugPrintf("[lua] %s\n", r + 5);
      }
    }
  }

  /* the game's clock (Lua `time`, the sum of the update steps) against the
   * real one, every 10 s: is the game running at full speed? */
  {
    static u64 t0;
    static float c0 = -1;
    u64 now = armGetSystemTick();
    if (clock >= 0) {
      if (c0 < 0 || clock < c0) {
        c0 = clock, t0 = now;
      } else {
        double real = (double)armTicksToNs(now - t0) / 1e9;
        if (real >= 10.0) {
          double game = (double)(clock - c0);
          debugPrintf("[lua] game clock: %.2f s in %.2f s (%.2fx real time)%s\n", game, real, game / real,
                      game / real < 0.9 ? " -- slow" : "");
          c0 = clock, t0 = now;
        }
      }
    }
  }

  if (dcr_config()->log_lua) {
    static u64 last;
    static char last_sig[24];
    u64 now = armGetSystemTick();
    int changed = strcmp(last_sig, st.sig) != 0;
    if (changed || armTicksToNs(now - last) >= 1000000000ull) {
      last = now;
      snprintf(last_sig, sizeof last_sig, "%s", st.sig);
      debugPrintf("[lua] mode %d screen %.0fx%.0f bird %.0f,%.0f sling %.0f,%.0f pull %.0f aiming %d "
                  "special %d; %s%s, main menu %d, %d items\n",
                  st.mode, (double)st.scr_w, (double)st.scr_h, (double)st.bird_x, (double)st.bird_y,
                  (double)st.sling_x, (double)st.sling_y, (double)st.pull, st.aiming, st.special, st.sig,
                  st.carousel ? " (planets)" : "", st.mm, st.nbuttons);
      if (changed)
        for (int i = 0; i < st.nbuttons; i++) {
          const AbsItem *r = &st.buttons[i];
          debugPrintf("[lua]   item %d (id %d): %.0f,%.0f %.0fx%.0f kind %d %s prio %d grp %d%s\n", i, r->id,
                      (double)r->x, (double)r->y, (double)r->w, (double)r->h, r->kind, r->round ? "round" : "box",
                      r->prio, r->grp, r->vis ? "" : " (off screen)");
        }
    }
  }
}

static void lua_error(lua_State *L, const char *what) {
  static int logged;
  if (logged++ < 8) {
    const char *msg = p_tolstring(L, -1, NULL);
    debugPrintf("[lua] %s: %s\n", what, msg ? msg : "(no message)");
  }
}

/* Inside the game's Lua, on its thread: at most once per update per universe. */
static void tick(lua_State *L) {
  if (g_busy || g_failed)
    return;
  Uni *u = uni_for(global_of(L));
  uint32_t want = g_want;
  if (!u || u->done == want)
    return;
  g_busy = 1;
  u->done = want;
  int top = p_gettop(L);
  if (!u->loaded) {
    if (p_loadbuffer(L, abs_ctl_lua, abs_ctl_lua_size, "=abs_ctl") != 0 || p_pcall(L, 0, 0, 0) != 0) {
      lua_error(L, "the controller script did not load");
      g_failed = 1;
      p_settop(L, top);
      g_busy = 0;
      return;
    }
    u->loaded = 1;
    /* Orbital Escapade's worlds, when the player has put the mod there */
    if (abs_orbital_present() &&
        (p_loadbuffer(L, abs_orbital_lua, abs_orbital_lua_size, "=abs_orbital") != 0 || p_pcall(L, 0, 0, 0) != 0))
      lua_error(L, "the Orbital Escapade script did not load");
    p_settop(L, top);
    /* the videos on the SD card, for the links' popups */
    char chunk[600];
    int n = snprintf(chunk, sizeof chunk, "__abs.set_videos('%s')", abs_extras_videos());
    if (n > 0 && n < (int)sizeof chunk && p_loadbuffer(L, chunk, (size_t)n, "=abs_videos") == 0)
      p_pcall(L, 0, 0, 0);
    p_settop(L, top);
  }
  int cmd = 0;
  if (u->is_game) {
    mutexLock(&g_lock);
    if (g_ncmds) {
      cmd = g_cmds[0];
      memmove(&g_cmds[0], &g_cmds[1], sizeof g_cmds[0] * (size_t)--g_ncmds);
    }
    mutexUnlock(&g_lock);
  }
  /* flags: 1 the menu's items, 2 a free shop, 4 links open the game's popups,
   * 8 the console is online (asked every few seconds), 16 Froot Loops
   * Bloopers is shown */
  const DcrConfig *cfg = dcr_config();
  static int online;
  static u64 online_at;
  if (cfg->planet_info && abs_video_available() && armTicksToNs(armGetSystemTick() - online_at) > 4000000000ull) {
    online_at = armGetSystemTick();
    online = abs_net_online();
  }
  int flags = 1 | (cfg->free_purchases ? 2 : 0) | (cfg->planet_info ? 4 : 0) | (online ? 8 : 0) |
              (cfg->froot_loops ? 16 : 0);
  char chunk[160];
  int len = snprintf(chunk, sizeof chunk, "return __abs.frame(%d,%.3f,%.3f,%d,%d,%d,%d,%.3f)", cmd,
                     u->is_game ? (double)g_pan : 0.0, u->is_game ? (double)g_zoom : 0.0, flags, cfg->tex_mb,
                     abs_video_state(), g_focus_id, u->is_game ? (double)g_pany : 0.0);
  if (p_loadbuffer(L, chunk, (size_t)len, "=abs_frame") == 0 && p_pcall(L, 0, 1, 0) == 0) {
    const char *s = p_tolstring(L, -1, NULL);
    if (s)
      parse(s, u);
  } else {
    lua_error(L, "__abs.frame failed");
  }
  p_settop(L, top);
  /* Orbital Escapade's worlds: added once the game has loaded its own, then
   * kept (abs_orbital.lua); what it did, for the log */
  if (u->is_game && abs_orbital_present()) {
    static const char k_orbital[] = "local O = __abs_orbital if O then O.frame() return O.report() end";
    if (p_loadbuffer(L, k_orbital, sizeof k_orbital - 1, "=abs_orbital_frame") == 0 && p_pcall(L, 0, 1, 0) == 0) {
      const char *s = p_tolstring(L, -1, NULL);
      while (s && *s) {
        const char *e = strchr(s, '\n');
        int n = e ? (int)(e - s) : (int)strlen(s);
        debugPrintf("[orbital] %.*s\n", n, s);
        s = e ? e + 1 : NULL;
      }
    } else {
      lua_error(L, "Orbital Escapade");
    }
    p_settop(L, top);
  }
  g_busy = 0;
}

/* ------------------------------------------------ installing */
void abs_lua_install(void) {
  if (!dcr_config()->lua_bridge) {
    debugPrintf("[lua] the controller bridge is off (config.ini [debug] lua_bridge)\n");
    return;
  }
  const uintptr_t base = (uintptr_t)g_mod_game.load_virtbase;
  for (int i = 0; i < F_COUNT; i++) {
    const uint32_t *w = (const uint32_t *)(base + k_fn[i].off);
    if (w[0] != k_fn[i].w0 || w[1] != k_fn[i].w1) {
      debugPrintf("[lua] %s is not where 2.2.14 has it (%08lx %08lx): the controller bridge is off; "
                  "touch and the pointer still work\n",
                  k_fn[i].name, (unsigned long)w[0], (unsigned long)w[1]);
      return;
    }
  }
  p_loadbuffer = (void *)(base + k_fn[F_LOADBUFFER].off);
  p_pcall = (void *)(base + k_fn[F_PCALL].off);
  p_gettop = (void *)(base + k_fn[F_GETTOP].off);
  p_settop = (void *)(base + k_fn[F_SETTOP].off);
  p_tolstring = (void *)(base + k_fn[F_TOLSTRING].off);

  /* luaL_Reg mathlib[]: {name, func} pairs, relocated to this address */
  struct {
    const char *name;
    lua_CFunction func;
  } *reg = (void *)(base + MATHLIB_REG);
  int n = 0;
  for (int i = 0; i < MATHLIB_N; i++) {
    if (!reg[i].name || (uintptr_t)reg[i].name < base || (uintptr_t)reg[i].name >= base + g_mod_game.load_size) {
      debugPrintf("[lua] the math library table is not where 2.2.14 has it: the bridge is off\n");
      return;
    }
    for (unsigned k = 0; k < sizeof k_hooks / sizeof k_hooks[0]; k++)
      if (!strcmp(reg[i].name, k_hooks[k].name)) {
        *k_hooks[k].orig = reg[i].func;
        reg[i].func = k_hooks[k].hook;
        n++;
      }
  }
  if (n != (int)(sizeof k_hooks / sizeof k_hooks[0])) {
    debugPrintf("[lua] only %d of the math functions found: the bridge is off\n", n);
    for (int i = 0; i < MATHLIB_N; i++)
      for (unsigned k = 0; k < sizeof k_hooks / sizeof k_hooks[0]; k++)
        if (reg[i].func == k_hooks[k].hook)
          reg[i].func = *k_hooks[k].orig;
    return;
  }
  g_enabled = 1;
  debugPrintf("[lua] controller bridge installed (%d math functions, script %u bytes)\n", n,
              (unsigned)abs_ctl_lua_size);
}
