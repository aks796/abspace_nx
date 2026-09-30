/* abs_input.c -- Switch controllers and the touchscreen as the game's input.
 *
 * The engine takes touches (MyInputHandler.nativeInput(action, x, y, id):
 * 0 down, 1 up, 2 move, in surface pixels) and keys (nativeKeyInput: Android
 * keycodes, BACK = 4). The touchscreen is passed straight through. The
 * controllers play one of two ways, R switching between them:
 *
 * CONSOLE (the default): the way Angry Birds Trilogy plays on consoles,
 * turned into touches with what the controller script sees in the game
 * (abs_lua.c / abs_ctl.lua: where the bird sits, how far the band stretches,
 * whether a bird is flying with its power unused, the menu's buttons, the
 * planet carousel):
 *
 *   In a level
 *     Left stick   pull the bird back and aim (config.ini [controls] aim:
 *                  pull = the bird goes where the stick points, as in
 *                  Trilogy; push = the stick points where it will fly).
 *                  Letting go returns it to the slingshot.
 *     D-pad        with a bird pulled back: fine aim -- up/down raise and
 *                  lower the shot, left/right less/more power; the aim then
 *                  stays when the stick is let go. Otherwise left/right move
 *                  the camera to the slingshot / the target.
 *     A            launch the bird; while it flies, its power (tap). A power
 *                  aimed where it is used (the Lazer bird's dash, the Orbital
 *                  Escapade worlds' Iron egg and Pink beam): the hand cursor
 *                  comes out at the bird, the left stick moves it, A uses the
 *                  power there
 *     B            let go of the bird without shooting (it does not pause)
 *     Right stick  move the camera along the level, as a drag does (it
 *                  stays where it is put); up/down zoom
 *     ZL / ZR      zoom out / in            L  camera: slingshot / target
 *     X (hold)     restart the level        Y  the power-ups bar (it
 *                  starts folded): open / fold   -  the Space Eagle
 *     +            pause / resume
 *   Menus, the pause page, the level's end
 *     D-pad / left stick  move between the buttons, in 8 directions (a ring
 *                  shaped like the button); past the last level of a page,
 *                  the next page. A popup takes the focus, on its best
 *                  button (the green check). A press it, B back (not on the
 *                  pause page; on the main menu it closes a tab, never quits)
 *     L / ZL, ZR   previous / next page     Right stick  the hand cursor
 *   The planets (episode selection): the planet in the middle is focused,
 *     with no ring; left/right turn the carousel, the other directions go to
 *     the buttons (and back to the planet). The tiny planets: the cursor.
 *
 * CURSOR: Angry Birds Reloaded's hand cursor (abs_cursor.c): the stick moves
 * it, A / ZL / ZR touch and drag, B back, L centres it, + hides it, - gyro.
 *
 * The port's own pages (abs_extras.c, abs_video.c) and the game's dialogs
 * (abs_dialog.c) take the controller while they are up.
 *
 * Synthesised touches use pointer id 0, as a first finger does, and are held
 * off while a real finger is on the screen. Runs on the engine's update
 * thread, right before each nativeUpdate; the pictures (focus ring, cursor,
 * restart bar, messages) are drawn on the render thread before each present.
 * MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "dcr_config.h"
#include "util.h"

#define ACT_DOWN 0
#define ACT_UP 1
#define ACT_MOVE 2
#define KEY_BACK 4
#define SYN_ID 0

/* slingshot.lua: the band's full stretch and the pull under which a release
 * cancels the shot, in physics units */
#define BAND_MAX 5.4f
#define BAND_CANCEL 3.0f

typedef void (*fn_input)(void *env, void *thiz, jint action, jfloat x, jfloat y, jint id);
typedef void (*fn_key)(void *env, void *thiz, jint code, jint event, jint unicode, jint device);

static struct {
  fn_input input;
  fn_key key;
  JObj *handler;
} N;

static PadState g_pad;
static int g_ready;

/* ---------------------------------------------------------------- output */
void abs_touch(int action, float x, float y, int id) {
  if (N.input)
    N.input(g_jni_env, N.handler, action, x, y, id);
}

void abs_key(int keycode, int down) {
  if (N.key)
    N.key(g_jni_env, N.handler, keycode, down ? 1 : 0, 0, 1);
}

/* the one synthesised finger */
static struct {
  int down;
  float x, y;
  int up_next;  /* a tap: lift it on the next update */
} g_syn;

static void syn_down(float x, float y) {
  if (g_syn.down)
    abs_touch(ACT_UP, g_syn.x, g_syn.y, SYN_ID);
  g_syn.down = 1;
  g_syn.x = x, g_syn.y = y;
  abs_touch(ACT_DOWN, x, y, SYN_ID);
}
static void syn_move(float x, float y) {
  if (!g_syn.down)
    return;
  if (fabsf(x - g_syn.x) < 0.25f && fabsf(y - g_syn.y) < 0.25f)
    return;
  g_syn.x = x, g_syn.y = y;
  abs_touch(ACT_MOVE, x, y, SYN_ID);
}
static void syn_up(void) {
  if (!g_syn.down)
    return;
  g_syn.down = 0;
  abs_touch(ACT_UP, g_syn.x, g_syn.y, SYN_ID);
}
static void syn_tap(float x, float y) {
  syn_down(x, y);
  g_syn.up_next = 1;
}

/* abs_cursor.c's touches: 0 down, 1 up, 2 move */
void abs_input_syn(int phase, float x, float y) {
  if (phase == 0)
    syn_down(x, y);
  else if (phase == 2)
    syn_move(x, y);
  else {
    g_syn.x = x, g_syn.y = y;
    syn_up();
  }
}

void abs_cursor_init(void);
void abs_cursor_enter(float x, float y);
void abs_cursor_leave(void);
void abs_cursor_pos(float *x, float *y);
const char *abs_cursor_update(PadState *pad, u64 down, u64 held, float mx, float my, u64 k_a, u64 k_b,
                              int real_touch, int menu);
void abs_cursor_draw(void);
void abs_cursor_style(int style);
void abs_cursor_aim_start(float x, float y);
void abs_cursor_aim(float lsx, float lsy);
void abs_cursor_click(void);

/* ------------------------------------------------------------ the state */
#define MEMS 8
static struct {
  int scheme;      /* ABS_SCHEME_* */
  /* aiming */
  int aiming;      /* our finger holds the bird */
  int held;        /* fine-aimed: the aim stays when the stick is let go */
  float ax, ay;    /* the pull, in units of a full stretch (screen axes) */
  int returning;   /* stick let go: easing back to the slingshot */
  int pending_up;  /* lift the finger on the next update */
  int cam_castle;  /* L: the camera was last sent to the target */
  u64 cam_back_at; /* when the camera was last sent back to the slingshot for a grab */
  /* menus */
  int focus_on;    /* an item is focused (and ringed) */
  int touch_mode;  /* the touchscreen was used last: no ring until a button */
  float fx, fy;    /* its centre (kept across frames: the list reorders) */
  int fkind;       /* its kind (ABS_ITEM_*) */
  AbsItem fitem;
  char sig[24];    /* the screen the focus is on */
  int moved;       /* the player moved the focus on this screen */
  int mcursor;     /* menus: the hand cursor is out (the right stick brought it) */
  int pcur;        /* a level: the aiming cursor is out (a flying bird's aimed power) */
  int carousel;    /* the planet carousel: 1 settled, 2 turning */
  struct {
    char sig[24];
    float fx, fy;
    int kind;
  } mem[MEMS];     /* where the focus was on the last few screens */
  int mem_next;
  u64 nav_next;    /* stick navigation repeat */
  int nav_dir;
  u64 nav_since;   /* the stick has been held this way since */
  u64 nav_last;    /* ... and moved the focus (or turned the carousel) last at */
  u64 hidden_since; /* the focus went to an item off the screen (its page is turning) */
  u64 turn_since;   /* a level-selection page has been turning since */
  u64 repage;       /* ZL/ZR (or a push past the last item) turned the page: the focus follows */
  /* restart hold */
  u64 x_since;
  int x_fired;
  u32 last_tap_seq;
  int real_touch;  /* a finger is on the screen */
  /* a short message at the top */
  char toast[96];
  u64 toast_until;
} S;

static Mutex g_draw_lock;
static struct {
  int focus_on, cursor, restart;
  AbsItem fitem;
  float restart_frac;
  char toast[96];
  float toast_alpha;
} g_draw;

static void toast(const char *msg) {
  snprintf(S.toast, sizeof S.toast, "%s", msg);
  S.toast_until = armGetSystemTick() + armNsToTicks(2500000000ull);
}

/* ------------------------------------------------------------- setup */
void abs_input_init(void) {
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  N.input = (fn_input)abs_native("Java_com_rovio_fusion_MyInputHandler_nativeInput");
  N.key = (fn_key)abs_native("Java_com_rovio_fusion_MyInputHandler_nativeKeyInput");
  N.handler = jni_singleton("com/rovio/fusion/MyInputHandler");
  g_ready = N.input && N.key;
  S.scheme = dcr_config()->scheme;
  abs_cursor_init();
  abs_font_init();
  debugPrintf("[input] natives: touch %p key %p; controls: %s (aim %s)%s; R switches\n", (void *)N.input,
              (void *)N.key, S.scheme == ABS_SCHEME_CURSOR ? "cursor" : "console",
              dcr_config()->aim == ABS_AIM_PULL ? "pull" : "push", dcr_config()->swap_ab ? ", A/B swapped" : "");
}

/* ------------------------------------------------------------ touch */
static void touchscreen(void) {
  HidTouchScreenState ts = {0};
  static int active[16];
  static float lx[16], ly[16];
  int now[16] = {0};
  if (hidGetTouchScreenStates(&ts, 1) == 0)
    ts.count = 0;
  const float sx = (float)abs_surface_w() / 1280.0f, sy = (float)abs_surface_h() / 720.0f;
  int any = 0;
  for (int i = 0; i < ts.count && i < 16; i++) {
    int id = (int)(ts.touches[i].finger_id % 10);
    float x = ts.touches[i].x * sx, y = ts.touches[i].y * sy;
    now[id] = 1;
    any = 1;
    if (!S.real_touch && g_syn.down) {
      /* a finger takes over from the controller's */
      syn_up();
      S.aiming = S.held = S.returning = S.pending_up = 0;
    }
    if (!active[id]) {
      abs_touch(ACT_DOWN, x, y, id);
    } else if (x != lx[id] || y != ly[id]) {
      abs_touch(ACT_MOVE, x, y, id);
    }
    lx[id] = x, ly[id] = y;
    active[id] = 1;
  }
  for (int id = 0; id < 16; id++)
    if (active[id] && !now[id]) {
      abs_touch(ACT_UP, lx[id], ly[id], id);
      active[id] = 0;
    }
  S.real_touch = any;
  if (any)
    S.touch_mode = 1; /* touching: no ring until a button is used again */
}

/* ------------------------------------------------------------ helpers */
static inline float stick(s32 v) { return (float)v / 32767.0f; }

static void dz(float *x, float *y, float d) {
  float m = sqrtf(*x * *x + *y * *y);
  if (m < d) {
    *x = *y = 0;
    return;
  }
  float s = (m > 1.0f ? 1.0f : m);
  float k = (s - d) / (1.0f - d) / m;
  *x *= k, *y *= k;
}

/* ----------------------------------------------------------- the level */
static void aim_release_cancel(void) {
  /* back to the slingshot, then let go: under the cancel limit the game
   * puts the bird back */
  S.returning = 1;
  S.held = 0;
}

static void level(const AbsLuaState *st, u64 down, u64 held, float lsx, float lsy, float rsx, float rsy,
                  u64 k_a, u64 k_b) {
  const DcrConfig *cfg = dcr_config();
  /* ---- camera: the right stick moves it along the level (up/down zoom in
   * a level that goes sideways), ZR / ZL zoom (ZL is the fine aim while a
   * bird is pulled back) ---- */
  float zoom = 0;
  if (held & HidNpadButton_ZR)
    zoom += 1.0f;
  if ((held & HidNpadButton_ZL) && !S.aiming)
    zoom -= 1.0f;
  abs_lua_set_analog(rsx, rsy, zoom);
  if (down & HidNpadButton_L) {
    /* the slingshot's view and the target's, in turn */
    S.cam_castle = !S.cam_castle;
    abs_lua_command(S.cam_castle ? ABS_CMD_CAMERA_CASTLE : ABS_CMD_CAMERA_SLING);
  }
  if (down & HidNpadButton_Plus)
    abs_lua_command(ABS_CMD_PAUSE);
  if (down & HidNpadButton_Y)
    abs_lua_command(ABS_CMD_POWERUPS);
  if (down & HidNpadButton_Minus)
    abs_lua_command(ABS_CMD_EAGLE);
  /* X held half a second: restart */
  if (held & HidNpadButton_X) {
    if (!S.x_since)
      S.x_since = armGetSystemTick();
    if (!S.x_fired && armTicksToNs(armGetSystemTick() - S.x_since) >= 500000000ull) {
      S.x_fired = 1;
      if (S.aiming) {
        syn_up();
        S.aiming = S.held = S.returning = S.pending_up = 0;
      }
      abs_lua_command(ABS_CMD_RESTART);
    }
  } else {
    S.x_since = 0;
    S.x_fired = 0;
  }

  if (!(st->mode == ABS_MODE_FLIGHT && st->special == 2))
    S.pcur = 0;
  const float R = st->pull > 1.0f ? st->pull : abs_surface_h() * 0.12f;
  float sx = lsx, sy = -lsy; /* screen axes: y down */
  if (cfg->aim == ABS_AIM_PUSH)
    sx = -sx, sy = -sy;
  float m = sqrtf(sx * sx + sy * sy);

  /* ---- aiming ----
   * Each step is its own update, so the game's Lua sees it as a phone's
   * finger would be seen: the touch lands on the bird (select_bird hit-tests
   * that spot), THEN the pull moves; the release happens where the last move
   * left the bird. */
  if (st->mode == ABS_MODE_AIM || S.aiming) {
    const float ox = st->sling_x >= 0 ? st->sling_x : st->bird_x;
    const float oy = st->sling_y >= 0 ? st->sling_y : st->bird_y;
    if (S.pending_up) {
      /* the shot (or the let-go) decided last update: lift the finger now */
      S.pending_up = 0;
      syn_up();
      S.aiming = S.held = S.returning = S.pending_up = 0;
      return;
    }
    if (!S.aiming && m > 0.0f && st->mode == ABS_MODE_AIM && !S.real_touch) {
      if (st->bird_x < 0 || st->bird_y < 0 || st->bird_x > abs_surface_w() || st->bird_y > abs_surface_h()) {
        /* the camera is away from the slingshot: back to it first (a touch
         * can only take a bird it sees) */
        u64 now = armGetSystemTick();
        if (armTicksToNs(now - S.cam_back_at) > 600000000ull) {
          S.cam_back_at = now;
          S.cam_castle = 0;
          abs_lua_command(ABS_CMD_CAMERA_SLING);
        }
        return;
      }
      /* grab the bird where it sits; the pull starts next update */
      syn_down(st->bird_x, st->bird_y);
      S.aiming = 1;
      S.held = S.returning = 0;
      S.ax = S.ay = 0;
      return;
    }
    if (S.aiming) {
      if (down & k_a) {
        /* launch from where the bird is now (under the cancel limit the
         * game would put it back: then keep aiming) */
        if (sqrtf(S.ax * S.ax + S.ay * S.ay) * BAND_MAX > BAND_CANCEL) {
          syn_up();
          S.aiming = S.held = S.returning = S.pending_up = 0;
          return;
        }
      } else if (down & k_b) {
        aim_release_cancel();
      }
      if (m > 0.0f && !S.returning) {
        /* the stick's travel covers the pulls that fire: just past the
         * cancel limit up to a full stretch */
        const float lo = (BAND_CANCEL + 0.15f) / BAND_MAX, hi = 1.02f;
        float want = lo + (hi - lo) * m;
        float tx = sx / m * want, ty = sy / m * want;
        if (held & HidNpadButton_ZL) {
          /* precision: the stick nudges the aim */
          S.ax += sx * 0.01f * cfg->aim_speed;
          S.ay += sy * 0.01f * cfg->aim_speed;
          S.held = 1;
        } else {
          S.ax += (tx - S.ax) * 0.55f;
          S.ay += (ty - S.ay) * 0.55f;
          S.held = 0;
        }
      } else if (m > 0.0f && S.returning && !(down & k_b)) {
        S.returning = 0; /* the stick again: aiming again */
      } else if (!S.held) {
        S.returning = 1;
      }
      /* fine aim with the D-pad: angle and power of the shot */
      int du = !!(down & HidNpadButton_Up) - !!(down & HidNpadButton_Down);
      int dp = !!(down & HidNpadButton_Right) - !!(down & HidNpadButton_Left);
      if ((du || dp) && !S.returning) {
        float len = sqrtf(S.ax * S.ax + S.ay * S.ay);
        if (len < 0.05f) {
          /* nothing yet: a level shot toward the target side */
          S.ax = st->bird_x <= abs_surface_w() * 0.5f ? -0.8f : 0.8f;
          S.ay = 0.0f, len = 0.8f;
        }
        float ang = atan2f(S.ay, S.ax);
        /* the shot goes the other way from the pull: "up" raises it */
        float fire_x = -S.ax;
        ang += (float)du * 0.0087f * cfg->aim_speed * (fire_x >= 0 ? 1.0f : -1.0f);
        len += (float)dp * 0.01f * cfg->aim_speed;
        if (len > 1.02f)
          len = 1.02f;
        if (len < 0.1f)
          len = 0.1f;
        S.ax = cosf(ang) * len, S.ay = sinf(ang) * len;
        S.held = 1;
      }
      if (S.returning) {
        S.ax *= 0.5f, S.ay *= 0.5f;
        if (fabsf(S.ax) < 0.02f && fabsf(S.ay) < 0.02f) {
          /* at the slingshot: lift next update, under the cancel limit */
          S.ax = S.ay = 0;
          syn_move(ox, oy);
          S.pending_up = 1;
          return;
        }
      }
      syn_move(ox + S.ax * R, oy + S.ay * R);
      return;
    }
  }

  /* ---- not aiming ---- */
  if (down & HidNpadButton_Left) {
    S.cam_castle = 0;
    abs_lua_command(ABS_CMD_CAMERA_SLING);
  }
  if (down & HidNpadButton_Right) {
    S.cam_castle = 1;
    abs_lua_command(ABS_CMD_CAMERA_CASTLE);
  }
  if (st->mode == ABS_MODE_FLIGHT && st->special == 2) {
    /* a bird in the air whose power is aimed (the Lazer bird's dash, the
     * Iron bird's egg, the Pink bird's beam): the left stick moves the
     * cursor from where the bird was, A uses the power there */
    if (!S.pcur) {
      S.pcur = 1;
      const float W = (float)abs_surface_w(), H = (float)abs_surface_h();
      int on = st->bird_x >= 0 && st->bird_y >= 0 && st->bird_x < W && st->bird_y < H;
      abs_cursor_aim_start(on ? st->bird_x : W * 0.5f, on ? st->bird_y : H * 0.55f);
    }
    abs_cursor_aim(lsx, lsy);
    if (down & k_a) {
      float x, y;
      abs_cursor_pos(&x, &y);
      syn_tap(x, y);
      abs_cursor_click();
    }
    return;
  }
  if (down & k_a) {
    /* a bird in the air: its power. Otherwise a tap on the scene (tutorial
     * notes, skipping the camera tour) */
    syn_tap(abs_surface_w() * 0.5f, abs_surface_h() * 0.55f);
  }
  /* B does not pause (+ does) */
}

/* ------------------------------------------------------------- menus */
static float icx(const AbsItem *r) { return r->x + r->w * 0.5f; }
static float icy(const AbsItem *r) { return r->y + r->h * 0.5f; }

#define K(kind) (1u << (kind))
#define K_FOCUSABLE (K(ABS_ITEM_BUTTON) | K(ABS_ITEM_CENTRE))

/* the item of these kinds nearest (x, y), or -1 */
static int nearest(const AbsLuaState *st, float x, float y, unsigned kinds) {
  int best = -1;
  float bd = 1e18f;
  for (int i = 0; i < st->nbuttons; i++) {
    if (!(kinds & K(st->buttons[i].kind)))
      continue;
    float dx = icx(&st->buttons[i]) - x, dy = icy(&st->buttons[i]) - y;
    float d = dx * dx + dy * dy;
    if (d < bd)
      bd = d, best = i;
  }
  return best;
}

/* the focused item in this frame's list: the same item (its id), or the
 * nearest of its kind */
static int refind(const AbsLuaState *st) {
  for (int i = 0; i < st->nbuttons; i++)
    if (st->buttons[i].id == S.fitem.id && S.fitem.id)
      return i;
  int i = nearest(st, S.fx, S.fy, K(S.fkind));
  if (i < 0)
    i = nearest(st, S.fx, S.fy, K_FOCUSABLE);
  return i;
}

/* how far apart two spans are on one axis (0 when they overlap) */
static float span_gap(float a0, float a1, float b0, float b1) {
  return b0 > a1 ? b0 - a1 : a0 > b1 ? a0 - b1 : 0.0f;
}

/* The best item from the focus in direction (dx, dy) (8 directions), among
 * these kinds, or -1. By the items' boxes, not only their centres:
 *   straight -- in line first (the boxes overlap across the move, or within
 *     35 degrees), the nearest edge to edge, one off to the side counting
 *     double; then anything within 60 degrees. On the planet screen left
 *     and right leave the planet out (they turn the carousel), and a move up
 *     or down that finds nothing in line goes to the planet.
 *   diagonal -- anything that way on both axes, the nearest, those closer to
 *     the diagonal first (down-left from the planet: the shop).
 * Items on another page count only when nothing on this one does (one off to
 * the side within 60 degrees comes after them), and then the nearest that
 * way: the next page's near edge, not one in line far across it. */
static int step(const AbsLuaState *st, float dx, float dy, unsigned kinds) {
  const int straight = dx == 0 || dy == 0;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len <= 0)
    return -1;
  dx /= len, dy /= len;
  const float cw = S.fitem.w > 0 ? S.fitem.w : 40, ch = S.fitem.h > 0 ? S.fitem.h : 40;
  const float c0x = S.fx - cw * 0.5f, c1x = S.fx + cw * 0.5f, c0y = S.fy - ch * 0.5f, c1y = S.fy + ch * 0.5f;
  const int carousel = st->carousel;
  int wide_vis = -1;
  for (int pass_vis = 1; pass_vis >= 0; pass_vis--) {
    int best = -1, wide = -1, centre = -1;
    float bs = 1e18f, ws = 1e18f;
    for (int i = 0; i < st->nbuttons; i++) {
      const AbsItem *it = &st->buttons[i];
      if (!(kinds & K(it->kind)) || it->vis != pass_vis || it->id == S.fitem.id)
        continue;
      if (carousel && it->kind == ABS_ITEM_CENTRE && dy == 0)
        continue;
      const float vx = icx(it) - S.fx, vy = icy(it) - S.fy;
      const float along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
      if (along < 8.0f)
        continue;
      if (straight && !pass_vis) {
        /* another page */
        if (across <= along * 1.73f) {
          const float d = sqrtf(vx * vx + vy * vy);
          if (d < bs)
            bs = d, best = i;
        }
      } else if (straight) {
        float edge = dx > 0 ? it->x - c1x : dx < 0 ? c0x - (it->x + it->w) : dy > 0 ? it->y - c1y : c0y - (it->y + it->h);
        if (edge < 0)
          edge = 0;
        const float side = dx != 0 ? span_gap(c0y, c1y, it->y, it->y + it->h) : span_gap(c0x, c1x, it->x, it->x + it->w);
        const float score = edge + side * 2.0f + across * 0.25f;
        if (carousel && it->kind == ABS_ITEM_CENTRE)
          centre = i;
        if (side == 0 || across <= along * 0.70f) {
          if (score < bs)
            bs = score, best = i;
        } else if (across <= along * 1.73f && score < ws) {
          ws = score, wide = i;
        }
      } else {
        if (vx * dx <= 4.0f || vy * dy <= 4.0f)
          continue; /* not that way on both axes */
        const float dist = sqrtf(vx * vx + vy * vy);
        const float score = dist * (2.0f - along / dist); /* along / dist: 1 on the diagonal */
        if (score < bs)
          bs = score, best = i;
      }
    }
    if (best >= 0)
      return best;
    if (centre >= 0)
      return centre;
    if (wide >= 0 && !(straight && pass_vis))
      return wide;
    if (wide >= 0)
      wide_vis = wide; /* off to the side on this page: after the next page's items straight on */
  }
  return wide_vis;
}

static void focus_set(const AbsLuaState *st, int i) {
  const AbsItem *it = &st->buttons[i];
  S.fx = icx(it);
  S.fy = icy(it);
  S.fkind = it->kind;
  S.fitem = *it;
  S.focus_on = 1;
}

/* the first focus on a screen: its best item (a popup's check, the planet in
 * the middle, Play...), the one nearest the middle among equals */
static int best_item(const AbsLuaState *st) {
  int best = -1, bp = -1;
  float bd = 1e18f;
  const float mx = abs_surface_w() * 0.5f, my = abs_surface_h() * 0.5f;
  for (int i = 0; i < st->nbuttons; i++) {
    const AbsItem *it = &st->buttons[i];
    if (!(K_FOCUSABLE & K(it->kind)) || !it->vis)
      continue;
    float dx = icx(it) - mx, dy = icy(it) - my, d = dx * dx + dy * dy;
    if (it->prio > bp || (it->prio == bp && d < bd))
      best = i, bp = it->prio, bd = d;
  }
  return best;
}

/* a new screen (or popup): remember where the focus was on the old one; on
 * the new one go back to where it was, or to its best item */
static void screen_changed(const AbsLuaState *st) {
  if (S.sig[0] && S.focus_on) {
    int slot = -1;
    for (int i = 0; i < MEMS; i++)
      if (!strcmp(S.mem[i].sig, S.sig))
        slot = i;
    if (slot < 0)
      slot = S.mem_next++ % MEMS;
    snprintf(S.mem[slot].sig, sizeof S.mem[slot].sig, "%s", S.sig);
    S.mem[slot].fx = S.fx, S.mem[slot].fy = S.fy, S.mem[slot].kind = S.fkind;
  }
  snprintf(S.sig, sizeof S.sig, "%s", st->sig);
  S.moved = 0;
  for (int i = 0; i < MEMS; i++)
    if (!strcmp(S.mem[i].sig, st->sig)) {
      S.fx = S.mem[i].fx, S.fy = S.mem[i].fy, S.fkind = S.mem[i].kind;
      S.fitem.id = 0;
      S.moved = 1; /* the player's own choice: keep it */
      int n = refind(st);
      if (n >= 0)
        focus_set(st, n);
      return;
    }
  int n = best_item(st);
  S.focus_on = 0;
  if (n >= 0)
    focus_set(st, n);
}

/* B in the menus: the game's back (on the main menu, abs_ctl.lua's key
 * handler closes the tab that is out and never asks to quit), except on the
 * pause page, where + resumes and B does nothing. */
static void back_press(const AbsLuaState *st) {
  if (st->mm & 8)
    return;
  abs_key(KEY_BACK, 1), abs_key(KEY_BACK, 0);
}

/* The planet carousel with the stick: a push turns it at once, holding
 * repeats after SPIN_FIRST_NS, then sooner and sooner down to SPIN_FAST_NS
 * (about 5.5 planets a second) after SPIN_RAMP_NS of holding. */
#define SPIN_MS 340
#define SPIN_FIRST_NS 300000000ull
#define SPIN_FAST_NS 180000000ull
#define SPIN_RAMP_NS 1200000000ull

static void menus(const AbsLuaState *st, u64 down, u64 held, float lsx, float lsy, float rsx, float rsy, u64 k_a,
                  u64 k_b) {
  const int carousel = st->carousel;

  /* the right stick brings out the hand cursor, the left stick and the
   * D-pad bring the ring back */
  const u64 dpad = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right;
  int returned = 0;
  if (!S.mcursor && rsx * rsx + rsy * rsy > 0.25f * 0.25f) {
    S.mcursor = 1;
    abs_cursor_enter(S.focus_on ? S.fitem.ax : -1, S.focus_on ? S.fitem.ay : -1);
  } else if (S.mcursor && ((down & dpad) || lsx * lsx + lsy * lsy > 0.5f * 0.5f)) {
    S.mcursor = 0;
    abs_cursor_leave();
    S.nav_dir = 9; /* the push that brought the ring back does not also move it */
    returned = 1;
  }
  if (S.mcursor) {
    abs_cursor_update(&g_pad, down, held, rsx, rsy, k_a, k_b, S.real_touch, 1);
    if (down & k_b)
      back_press(st);
    if ((down & HidNpadButton_Plus) && st->mode == ABS_MODE_PAUSED)
      abs_lua_command(ABS_CMD_PAUSE);
    return;
  }

  /* a carousel turn's time (abs_ctl.lua spin): a press, a little quicker
   * than the game's 0.5 s */
  int spin_ms = SPIN_MS;
  if (down & (HidNpadButton_L | HidNpadButton_ZL)) {
    abs_lua_command(carousel ? ABS_CMD_ARG(ABS_CMD_SPIN_LEFT, spin_ms) : ABS_CMD_PAGE_PREV);
    if (!carousel)
      S.repage = armGetSystemTick();
  }
  if (down & HidNpadButton_ZR) {
    abs_lua_command(carousel ? ABS_CMD_ARG(ABS_CMD_SPIN_RIGHT, spin_ms) : ABS_CMD_PAGE_NEXT);
    if (!carousel)
      S.repage = armGetSystemTick();
  }
  if ((down & HidNpadButton_Plus) && st->mode == ABS_MODE_PAUSED)
    abs_lua_command(ABS_CMD_PAUSE);

  /* a direction, in 8: the D-pad (two held together make a diagonal), or
   * the stick with repeat */
  float dx = 0, dy = 0;
  if (down & dpad) {
    dx = (float)(!!(held & HidNpadButton_Right) - !!(held & HidNpadButton_Left));
    dy = (float)(!!(held & HidNpadButton_Down) - !!(held & HidNpadButton_Up));
  } else {
    int sd = 0;
    if (lsx * lsx + lsy * lsy > 0.6f * 0.6f) {
      float a = atan2f(-lsy, lsx); /* screen axes */
      sd = 1 + ((int)lroundf(a / 0.78539816f) + 8) % 8;
    }
    u64 now = armGetSystemTick();
    if (sd && S.nav_dir == 9) {
      /* still the push that brought the ring back */
    } else if (sd && (sd != S.nav_dir || now >= S.nav_next)) {
      float a = (float)(sd - 1) * 0.78539816f;
      dx = roundf(cosf(a)), dy = roundf(sinf(a));
      int spin = carousel && S.fkind == ABS_ITEM_CENTRE && dy == 0;
      u64 wait;
      if (sd != S.nav_dir) {
        S.nav_since = now;
        wait = spin ? SPIN_FIRST_NS : 350000000ull;
      } else if (spin) {
        /* held on the carousel: sooner and sooner, to SPIN_FAST_NS after
         * SPIN_RAMP_NS of holding; each turn lasts twice the time since the
         * last, so the planets are still moving when the next is asked for */
        const u64 h = armTicksToNs(now - S.nav_since);
        wait = h >= SPIN_RAMP_NS ? SPIN_FAST_NS
                                 : SPIN_FIRST_NS - (SPIN_FIRST_NS - SPIN_FAST_NS) * h / SPIN_RAMP_NS;
        int ms = (int)(armTicksToNs(now - S.nav_last) / 500000ull); /* twice, in ms */
        spin_ms = ms < 200 ? 200 : ms > 400 ? 400 : ms;
      } else {
        wait = 140000000ull;
      }
      S.nav_last = now;
      S.nav_next = now + armNsToTicks(wait);
    }
    if (!sd || S.nav_dir != 9)
      S.nav_dir = sd;
  }
  int dir = (dx != 0 || dy != 0) && !returned;
  if (dir || (down & k_a)) {
    if (S.touch_mode) {
      /* back from the touchscreen: show the focus first */
      S.touch_mode = 0;
      dir = 0;
      down &= ~k_a;
    }
  }

  if (st->nbuttons <= 0 || !st->sig[0] || !strcmp(st->sig, "busy")) {
    S.focus_on = 0;
    if ((down & k_a) && st->nbuttons <= 0 && strcmp(st->sig, "busy"))
      syn_tap(abs_surface_w() * 0.5f, abs_surface_h() * 0.5f); /* nothing known to press: the middle */
    if (down & k_b)
      back_press(st);
    return;
  }
  /* a level-selection page turning (abs_ctl.lua): the ring stays on its
   * level, followed by id as the page slides, and waits -- no push moves it,
   * and a screen that seems new for a moment mid-turn (a frame taking the
   * touches) does not take the focus, which would turn the page back. At
   * most 1.5 s. */
  if ((st->mm & 16) && S.focus_on) {
    const u64 now = armGetSystemTick();
    if (!S.turn_since)
      S.turn_since = now;
    if (armTicksToNs(now - S.turn_since) < 1500000000ull) {
      for (int i = 0; i < st->nbuttons; i++)
        if (S.fitem.id && st->buttons[i].id == S.fitem.id) {
          focus_set(st, i);
          break;
        }
      if (down & k_b)
        back_press(st);
      return;
    }
  } else {
    S.turn_since = 0;
  }
  /* the page was turned from under the focus (ZL/ZR): once it rests, the
   * focus comes onto it, to the item nearest where it was */
  if (S.repage) {
    const u64 now = armGetSystemTick();
    if (armTicksToNs(now - S.repage) > 3000000000ull) {
      S.repage = 0;
    } else if (S.focus_on && !S.fitem.vis && !(st->mm & 16) && !strcmp(st->sig, S.sig)) {
      const float W = abs_surface_w(), H = abs_surface_h();
      const float x = S.fx < 0 ? 0 : S.fx > W ? W : S.fx, y = S.fy < 0 ? 0 : S.fy > H ? H : S.fy;
      int n = -1;
      float bd = 1e18f;
      for (int i = 0; i < st->nbuttons; i++) {
        const AbsItem *it = &st->buttons[i];
        if (!(K_FOCUSABLE & K(it->kind)) || !it->vis)
          continue;
        float ddx = icx(it) - x, ddy = icy(it) - y, d = ddx * ddx + ddy * ddy;
        if (d < bd)
          bd = d, n = i;
      }
      if (n >= 0)
        focus_set(st, n);
      S.repage = 0;
    }
  }
  if (strcmp(st->sig, S.sig))
    screen_changed(st);
  int cur = S.focus_on ? refind(st) : -1;
  if (cur >= 0)
    focus_set(st, cur);
  else {
    cur = best_item(st);
    if (cur < 0)
      return;
    focus_set(st, cur);
  }
  /* until the player moves it, the focus goes to a better first choice when
   * one shows up: the planet, found a few updates after the buttons; a
   * comic's check, which comes when the comic ends */
  if (!S.moved) {
    int b = best_item(st);
    if (b >= 0 && st->buttons[b].prio > S.fitem.prio)
      focus_set(st, b);
  }

  /* the focus is on the next page's item while that page turns: the next
   * push waits for it to arrive (a second at most), so the ring never runs
   * pages ahead of the screen */
  if (S.focus_on && !S.fitem.vis) {
    const u64 now = armGetSystemTick();
    if (!S.hidden_since)
      S.hidden_since = now;
    if (dir && armTicksToNs(now - S.hidden_since) < 1500000000ull)
      dir = 0;
  } else {
    S.hidden_since = 0;
  }
  if (dir) {
    int n = -1;
    S.moved = 1;
    S.repage = 0;
    if (carousel && S.fkind == ABS_ITEM_CENTRE && dy == 0) {
      abs_lua_command(ABS_CMD_ARG(dx < 0 ? ABS_CMD_SPIN_LEFT : ABS_CMD_SPIN_RIGHT, spin_ms));
    } else {
      n = step(st, dx, dy, K_FOCUSABLE);
      if (n < 0 && carousel && S.fkind == ABS_ITEM_CENTRE && dx == 0) {
        /* up / down from the planet, nothing in the cone: the nearest button
         * on that side */
        float bd = 1e18f;
        for (int i = 0; i < st->nbuttons; i++) {
          const AbsItem *it = &st->buttons[i];
          if (it->kind != ABS_ITEM_BUTTON || !it->vis || (icy(it) - S.fy) * dy <= 0)
            continue;
          float ddx = icx(it) - S.fx, ddy = icy(it) - S.fy, d = ddx * ddx + ddy * ddy;
          if (d < bd)
            bd = d, n = i;
        }
      }
      if (n < 0 && !carousel && dy == 0) {
        abs_lua_command(dx < 0 ? ABS_CMD_PAGE_PREV : ABS_CMD_PAGE_NEXT);
        S.repage = armGetSystemTick();
      }
    }
    if (n >= 0)
      focus_set(st, n);
  }
  if ((down & k_a) && S.fitem.vis)
    syn_tap(S.fitem.ax, S.fitem.ay);
  if (down & k_b)
    back_press(st);
}

void abs_input_popup_closed(void) {
  /* back from a link's popup: the ring, not the cursor */
  if (S.mcursor) {
    S.mcursor = 0;
    abs_cursor_leave();
  }
}

/* ------------------------------------------------------------- per update */
static void set_scheme(int scheme) {
  if (scheme == S.scheme)
    return;
  S.scheme = scheme;
  if (S.aiming || g_syn.down) {
    syn_up();
    S.aiming = S.held = S.returning = S.pending_up = 0;
  }
  if (scheme == ABS_SCHEME_CURSOR) {
    abs_cursor_enter(-1, -1);
    toast("Cursor controls  -  R: console controls");
  } else {
    abs_cursor_leave();
    toast("Console controls  -  R: cursor controls");
  }
  debugPrintf("[input] controls: %s\n", scheme == ABS_SCHEME_CURSOR ? "cursor" : "console");
}

void abs_input_update(void) {
  if (!g_ready)
    return;
  padUpdate(&g_pad);
  u64 down = padGetButtonsDown(&g_pad), held = padGetButtons(&g_pad);
  const u64 k_a = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  const u64 k_b = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;
  int cursor = 0;

  if (g_syn.up_next) {
    g_syn.up_next = 0;
    syn_up();
  }
  HidAnalogStickState ls = padGetStickPos(&g_pad, 0), rs = padGetStickPos(&g_pad, 1);
  float lsx = stick(ls.x), lsy = stick(ls.y), rsx = stick(rs.x), rsy = stick(rs.y);
  dz(&lsx, &lsy, 0.18f);
  dz(&rsx, &rsy, 0.25f);

  if (abs_video_active() || abs_extras_active() || abs_dialog_active()) {
    /* the videos, the port's pages and the game's dialogs take every input */
    HidTouchScreenState ts = {0};
    static int was_touching;
    int n = hidGetTouchScreenStates(&ts, 1) ? ts.count : 0;
    int tap = n && !was_touching;
    float tx = n ? ts.touches[0].x * (float)abs_surface_w() / 1280.0f : 0;
    float ty = n ? ts.touches[0].y * (float)abs_surface_h() / 720.0f : 0;
    was_touching = n > 0;
    if (S.aiming || g_syn.down) {
      syn_up();
      S.aiming = S.held = S.returning = S.pending_up = 0;
    }
    if (abs_video_active()) {
      abs_video_input(down, held, lsx);
      /* a tap outside the picture goes to the game: the popup's X */
      if (tap && !abs_video_tap(tx, ty))
        syn_tap(tx, ty);
    }
    else if (abs_extras_active())
      abs_extras_input(down, held, lsx, lsy, tap, tx, ty);
    else
      abs_dialog_input(down, tap, tx, ty);
    abs_lua_set_analog(0, 0, 0);
    goto publish;
  }
  touchscreen();

  AbsLuaState st;
  abs_lua_snapshot(&st);
  if (st.tap_seq != S.last_tap_seq) {
    S.last_tap_seq = st.tap_seq;
    if (!S.real_touch)
      syn_tap(st.tap_x, st.tap_y);
  }

  const int in_level =
      st.mode == ABS_MODE_AIM || st.mode == ABS_MODE_FLIGHT || st.mode == ABS_MODE_WAIT || S.aiming;
  /* R: the controls, in a level (in the menus the right stick brings the
   * cursor out whenever it is wanted) */
  if ((down & HidNpadButton_R) && in_level)
    set_scheme(S.scheme == ABS_SCHEME_CURSOR ? ABS_SCHEME_CONSOLE : ABS_SCHEME_CURSOR);

  /* the cursor in a level: chosen; anywhere, when the script cannot see the
   * game; in the menus, when config.ini turns the ring off */
  cursor = (in_level && S.scheme == ABS_SCHEME_CURSOR) || !abs_lua_active() || (!in_level && !dcr_config()->menu_focus);
  if (cursor) {
    S.focus_on = 0;
    S.mcursor = 0;
    if (S.aiming) {
      syn_up();
      S.aiming = S.held = S.returning = S.pending_up = 0;
    }
    abs_lua_set_analog(rsx, rsy, 0); /* the right stick still moves the camera */
    /* B: back, but it does not pause a level, nor resume from the pause page */
    const u64 cdown = (in_level || (st.mm & 8)) ? down & ~k_b : down;
    const char *say = abs_cursor_update(&g_pad, cdown, held, lsx, lsy, k_a, k_b, S.real_touch, 0);
    if (say)
      toast(say);
  } else if (in_level) {
    S.focus_on = 0;
    S.sig[0] = 0; /* the next menu is a new screen */
    if (S.mcursor) {
      S.mcursor = 0;
      abs_cursor_leave();
    }
    if (!S.real_touch)
      level(&st, down, held, lsx, lsy, rsx, rsy, k_a, k_b);
  } else {
    abs_lua_set_analog(0, 0, 0);
    if (S.aiming) {
      syn_up();
      S.aiming = S.held = S.returning = S.pending_up = 0;
    }
    if (!S.real_touch)
      menus(&st, down, held, lsx, lsy, rsx, rsy, k_a, k_b);
  }
  if (!in_level || cursor)
    S.pcur = 0;
  /* which hand: pointing where a power is aimed, open in a level, pointing
   * in the menus */
  abs_cursor_style((S.pcur || (cursor && in_level && st.special == 2)) ? ABS_CURSOR_POINT
                   : in_level                                            ? ABS_CURSOR_LEVEL
                                                                         : ABS_CURSOR_MENU);
  abs_lua_set_focus(S.focus_on && !S.mcursor && !cursor ? S.fitem.id : 0);
  S.carousel = st.carousel;

publish:
  mutexLock(&g_draw_lock);
  g_draw.focus_on = S.focus_on && !cursor && !S.mcursor && !S.touch_mode && S.fitem.vis && !abs_extras_active() &&
                    !abs_video_active() && S.fitem.kind != ABS_ITEM_CENTRE; /* the planet: no ring */
  g_draw.fitem = S.fitem;
  g_draw.cursor = (cursor || S.mcursor || S.pcur) && !abs_extras_active() && !abs_video_active() && !abs_dialog_active();
  g_draw.restart = S.x_since && !S.x_fired;
  g_draw.restart_frac =
      S.x_since ? (float)armTicksToNs(armGetSystemTick() - S.x_since) / 500000000.0f : 0.0f;
  {
    u64 now = armGetSystemTick();
    if (S.toast_until > now) {
      float left = (float)armTicksToNs(S.toast_until - now) / 1e9f;
      g_draw.toast_alpha = left > 0.5f ? 1.0f : left / 0.5f;
      snprintf(g_draw.toast, sizeof g_draw.toast, "%s", S.toast);
    } else {
      g_draw.toast_alpha = 0;
    }
  }
  mutexUnlock(&g_draw_lock);
}

/* ------------------------------------------------------------- drawing */
/* The focus ring: the shape of what it is on -- round buttons and planets get
 * an ellipse, the others a rounded rectangle -- a soft glow, then the ring. */
static void ring(const AbsItem *it, float t) {
  const float H = (float)abs_surface_h(), k = H / 720.0f;
  const float pad = 5.0f * k, th = 4.0f * k;
  const float pulse = 0.5f + 0.5f * sinf(t * 6.2831853f);
  float x = it->x - pad, y = it->y - pad, w = it->w + 2 * pad, h = it->h + 2 * pad;
  float r = it->round ? -1.0f : (w < h ? w : h) * 0.28f;
  if (it->kind == ABS_ITEM_CENTRE) {
    /* the planet in the middle: its wooden name sign */
    r = (w < h ? w : h) * 0.22f;
  } else if (it->round) {
    /* round buttons: a circle around the button itself. Their pictures'
     * boxes take in a shadow underneath (taller than wide): the circle sits
     * at the top of the box, as wide as the button */
    if (h > w) {
      h = w;
    } else if (w > h) {
      x += (w - h) * 0.5f;
      w = h;
    }
  }
  uint32_t glow_a = (uint32_t)(40 + 50 * pulse);
  abs_ov_ring(x - th * 0.5f, y - th * 0.5f, w + th, h + th, r < 0 ? r : r + th * 0.5f, th * 2.2f,
              0xffd23000u | glow_a);
  abs_ov_ring(x, y, w, h, r, th, 0x2a1a00a0u);                           /* dark edge inside */
  abs_ov_ring(x + th * 0.25f, y + th * 0.25f, w - th * 0.5f, h - th * 0.5f, r, th,
              0xffe63c00u | (uint32_t)(190 + 65 * pulse));               /* the ring: warm yellow */
}

void abs_input_draw(void) {
  mutexLock(&g_draw_lock);
  typeof(g_draw) d = g_draw;
  mutexUnlock(&g_draw_lock);
  int dialog = abs_dialog_active(), extras = abs_extras_active();
  int vs = abs_video_state();
  if (!d.focus_on && !d.cursor && !d.restart && !dialog && !extras && vs == ABS_VS_IDLE && d.toast_alpha <= 0)
    return;
  const int W = abs_surface_w(), H = abs_surface_h();
  if (!abs_ov_begin(W, H))
    return;
  const float t = (float)(armTicksToNs(armGetSystemTick()) % 1000000000ull) / 1e9f;
  abs_video_draw(); /* in the popup's rectangle, or the whole screen */
  if (d.focus_on)
    ring(&d.fitem, t);
  if (d.restart) {
    float f = d.restart_frac > 1 ? 1 : d.restart_frac;
    const float bw = W * 0.24f, bh = 10.0f * H / 720.0f, bx = (W - bw) * 0.5f, by = H * 0.08f;
    abs_ov_rect(bx - 2, by - 2, bw + 4, bh + 4, 0x000000a0u);
    abs_ov_rect(bx, by, bw * f, bh, 0xffffffe0u);
  }
  if (extras)
    abs_extras_draw();
  if (dialog)
    abs_dialog_draw();
  if (d.toast_alpha > 0 && d.toast[0]) {
    const float k = H / 720.0f, px = 24 * k;
    float tw = abs_ov_text_width(px, d.toast) + 36 * k, th = px * 1.9f;
    float x = (W - tw) * 0.5f, y = 18 * k;
    uint32_t a = (uint32_t)(d.toast_alpha * 255);
    abs_ov_rrect(x, y, tw, th, th * 0.5f, 0x10182800u | (a * 200 / 255));
    abs_ov_text(x + 18 * k, y + (th - px) * 0.5f, px, 0xffffff00u | a, d.toast);
  }
  if (d.cursor)
    abs_cursor_draw();
  abs_ov_end();
}
