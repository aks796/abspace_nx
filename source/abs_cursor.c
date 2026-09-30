/* abs_cursor.c -- the hand cursor: Angry Birds Reloaded's controls (the
 * Switch port of Reloaded, abreloaded_nx's nx_pointer), as the second way to
 * play (R switches, abs_input.c).
 *
 *   Left stick   move the cursor            A / ZL / ZR  touch (hold: drag)
 *   B            back                       L            cursor to the middle
 *   +            show / hide the cursor     -            gyro pointing on/off
 *   D-pad up/down  faster / slower: whichever moves the cursor (a USB mouse if
 *                  one is plugged in, else the gyro if on, else the stick)
 *   USB mouse    moves it, left button touches
 *
 * The pictures: Orbital Escapade's hand, built into the program
 * (source/cursor/, abs_res.S) -- the PC game's four, cut from the mod's
 * CURSORS_SHEET_1 at their sprites with their hot spots: pointing and
 * clicking in the menus, open and grabbing in a level, pointing where a
 * flying bird's power is aimed -- drawn as the PC draws them, from the hot
 * spot; an arrow should one not decode. Where the cursor is and what it
 * does stays this port's. The three speeds are kept in pointer.cfg, written
 * 3 s after the last change. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

/* abs_input.c: the synthesised finger */
void abs_input_syn(int phase, float x, float y); /* 0 down, 1 up, 2 move */

#define STICK_MIN 2.0f
#define STICK_MAX 60.0f
#define SENS_MIN 0.25f
#define SENS_MAX 8.0f
#define GYRO_MIN 0.10f
#define GYRO_MAX 8.0f
#define GYRO_GAIN 120.0f /* Reloaded's: angular velocity -> px per frame at 1080p */
#define SAVE_AFTER_NS 3000000000ull

static struct {
  int visible;
  float x, y;           /* the hot spot, surface pixels */
  int tap_prev;         /* touching last update */
  float stick, mouse, gyro;
  int gyro_ready, gyro_on, mouse_on;
  u64 mouse_seen;
  HidSixAxisSensorHandle six[4];
  int dpad_hold;
  int dirty;
  u64 dirty_at;
} C;

/* the hand: its pictures (abs_res.S) and hot spots, from the picture's top
 * left (the sprites' pivots in the mod's sheet); decoded at start, made into
 * textures on the render thread */
enum { HAND_POINT, HAND_CLICK, HAND_HOVER, HAND_GRAB, HANDS };
extern const uint8_t abs_hand_point_png[], abs_hand_click_png[], abs_hand_hover_png[], abs_hand_grab_png[];
extern const uint32_t abs_hand_point_png_size, abs_hand_click_png_size, abs_hand_hover_png_size,
    abs_hand_grab_png_size;
static const int k_hot[HANDS][2] = {{7, 7}, {8, -1}, {7, 7}, {2, -6}};
static struct {
  uint8_t *rgba;
  int w, h, px, py;
  unsigned tex;
} g_hand[HANDS];
static int g_hands; /* all four decoded */
static Mutex g_lock;
static struct {
  int visible, grab;
  int style;       /* ABS_CURSOR_MENU / _LEVEL / _POINT: which hand */
  u64 click_until; /* a tap just made (the aiming cursor): its click shows */
  float x, y;
} g_draw;

static void clamp(void) {
  const float W = (float)abs_surface_w(), H = (float)abs_surface_h();
  C.x = C.x < 0 ? 0 : C.x > W - 1 ? W - 1 : C.x;
  C.y = C.y < 0 ? 0 : C.y > H - 1 ? H - 1 : C.y;
}

static void limits(void) {
  C.stick = C.stick < STICK_MIN ? STICK_MIN : C.stick > STICK_MAX ? STICK_MAX : C.stick;
  C.mouse = C.mouse < SENS_MIN ? SENS_MIN : C.mouse > SENS_MAX ? SENS_MAX : C.mouse;
  C.gyro = C.gyro < GYRO_MIN ? GYRO_MIN : C.gyro > GYRO_MAX ? GYRO_MAX : C.gyro;
}

/* ------------------------------------------------------------ files */
static void cfg_path(char *out, size_t cap) { snprintf(out, cap, "%s/pointer.cfg", dcr_game_root()); }

static void settings_load(void) {
  char path[300];
  cfg_path(path, sizeof path);
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[128];
  float v;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, "stick=%f", &v) == 1) C.stick = v;
    else if (sscanf(line, "mouse=%f", &v) == 1) C.mouse = v;
    else if (sscanf(line, "gyro=%f", &v) == 1) C.gyro = v;
  }
  fclose(f);
  limits();
  debugPrintf("[cursor] pointer.cfg: stick %.1f, mouse %.2f, gyro %.2f\n", (double)C.stick, (double)C.mouse,
              (double)C.gyro);
}

static void settings_save(void) {
  char path[300];
  cfg_path(path, sizeof path);
  FILE *f = fopen(path, "w");
  C.dirty = 0;
  if (!f)
    return;
  fprintf(f, "# the hand cursor's speeds -- saved by the game (D-pad up/down), safe to edit\n");
  fprintf(f, "stick=%.2f\nmouse=%.2f\ngyro=%.2f\n", (double)C.stick, (double)C.mouse, (double)C.gyro);
  fclose(f);
}

/* ------------------------------------------------------------ setup */
void abs_cursor_init(void) {
  C.stick = dcr_config()->pointer_speed * (float)abs_surface_h() / 720.0f;
  C.mouse = 1.0f;
  C.gyro = 1.0f;
  C.x = abs_surface_w() * 0.5f;
  C.y = abs_surface_h() * 0.5f;
  C.visible = 1;
  settings_load();
  const uint8_t *const png[HANDS] = {abs_hand_point_png, abs_hand_click_png, abs_hand_hover_png, abs_hand_grab_png};
  const uint32_t len[HANDS] = {abs_hand_point_png_size, abs_hand_click_png_size, abs_hand_hover_png_size,
                               abs_hand_grab_png_size};
  g_hands = 1;
  for (int i = 0; i < HANDS; i++) {
    g_hand[i].rgba = abs_png_decode(png[i], len[i], &g_hand[i].w, &g_hand[i].h);
    g_hand[i].px = k_hot[i][0], g_hand[i].py = k_hot[i][1];
    if (!g_hand[i].rgba)
      g_hands = 0;
  }
  if (g_hands) {
    debugPrintf("[cursor] Orbital Escapade's hand (%dx%d)\n", g_hand[HAND_POINT].w, g_hand[HAND_POINT].h);
  } else {
    debugPrintf("[cursor] the hand's pictures did not decode: an arrow instead\n");
    for (int i = 0; i < HANDS; i++) {
      free(g_hand[i].rgba);
      g_hand[i].rgba = NULL;
    }
  }
  hidInitializeMouse();
  Result r0 = hidGetSixAxisSensorHandles(&C.six[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  Result r1 = hidGetSixAxisSensorHandles(&C.six[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
  Result r2 = hidGetSixAxisSensorHandles(&C.six[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
  if (R_SUCCEEDED(r0) && R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) {
    for (int i = 0; i < 4; i++)
      hidStartSixAxisSensor(C.six[i]);
    C.gyro_ready = 1;
  } else {
    debugPrintf("[cursor] no gyro (%x %x %x)\n", r0, r1, r2);
  }
}

/* entering the cursor controls: the cursor shows (where the finger would be) */
void abs_cursor_enter(float x, float y) {
  C.visible = 1;
  if (x >= 0 && y >= 0)
    C.x = x, C.y = y;
  clamp();
}

/* leaving them: a held touch is let go */
void abs_cursor_leave(void) {
  if (C.tap_prev)
    abs_input_syn(1, C.x, C.y);
  C.tap_prev = 0;
}

void abs_cursor_pos(float *x, float *y) { *x = C.x, *y = C.y; }
int abs_cursor_visible(void) { return C.visible; }

/* which hand: the menus', a level's, or pointing (abs_input.c, each update) */
void abs_cursor_style(int style) {
  mutexLock(&g_lock);
  g_draw.style = style;
  mutexUnlock(&g_lock);
}

/* The aiming cursor (abs_input.c, the console controls): while a flying
 * bird's power is aimed, the left stick moves the cursor from where the bird
 * was, and A taps there (abs_cursor_click shows the click). */
void abs_cursor_aim_start(float x, float y) {
  C.x = x, C.y = y;
  clamp();
}
void abs_cursor_aim(float lsx, float lsy) {
  if (lsx || lsy) {
    C.x += lsx * C.stick;
    C.y -= lsy * C.stick;
    clamp();
  }
  mutexLock(&g_lock);
  g_draw.visible = 1;
  g_draw.grab = 0;
  g_draw.x = C.x, g_draw.y = C.y;
  mutexUnlock(&g_lock);
}
void abs_cursor_click(void) {
  mutexLock(&g_lock);
  g_draw.click_until = armGetSystemTick() + armNsToTicks(180000000ull);
  mutexUnlock(&g_lock);
}

/* ------------------------------------------------------------ devices */
static int read_gyro(PadState *pad, HidSixAxisSensorState *out, float *sx, float *sy) {
  if (!C.gyro_ready)
    return 0;
  const u64 style = padGetStyleSet(pad);
  *sx = -1.0f, *sy = -1.0f; /* Pro Controller */
  if (style & HidNpadStyleTag_NpadFullKey)
    return hidGetSixAxisSensorStates(C.six[1], out, 1) > 0;
  *sx = 1.0f, *sy = 1.0f; /* Joy-Con: mirrored */
  if (style & HidNpadStyleTag_NpadHandheld)
    return hidGetSixAxisSensorStates(C.six[0], out, 1) > 0;
  if (style & HidNpadStyleTag_NpadJoyDual) {
    const u64 attr = padGetAttributes(pad);
    if (attr & HidNpadAttribute_IsRightConnected)
      return hidGetSixAxisSensorStates(C.six[3], out, 1) > 0;
    if (attr & HidNpadAttribute_IsLeftConnected)
      return hidGetSixAxisSensorStates(C.six[2], out, 1) > 0;
  }
  return 0;
}

/* every mouse sample since the last update (a wheel notch is in only one) */
static int mouse(void) {
  HidMouseState st[16];
  int n = (int)hidGetMouseStates(st, 16);
  if (n <= 0)
    return 0;
  s32 dx = 0, dy = 0, wheel = 0;
  u32 buttons = 0, attrs = 0;
  u64 newest = C.mouse_seen, btn_sn = 0;
  for (int i = 0; i < n; i++) {
    if (st[i].sampling_number > btn_sn)
      btn_sn = st[i].sampling_number, buttons = st[i].buttons, attrs = st[i].attributes;
    if (st[i].sampling_number <= C.mouse_seen)
      continue;
    dx += st[i].delta_x, dy += st[i].delta_y;
    wheel += st[i].wheel_delta_y ? st[i].wheel_delta_y : st[i].wheel_delta_x;
    if (st[i].sampling_number > newest)
      newest = st[i].sampling_number;
  }
  int on = (attrs & HidMouseAttribute_IsConnected) || dx || dy || wheel || buttons;
  if (on != C.mouse_on) {
    C.mouse_on = on;
    debugPrintf("[cursor] mouse %s\n", on ? "connected" : "gone");
    if (on)
      C.gyro_on = 0;
  }
  if (!C.mouse_seen) {
    C.mouse_seen = newest; /* first sight: no jump */
    return (buttons & HidMouseButton_Left) ? 1 : 0;
  }
  C.mouse_seen = newest;
  if (wheel) {
    for (int i = 0; i < (wheel > 0 ? wheel : -wheel) && i < 8; i++)
      C.mouse *= wheel > 0 ? 1.15f : 1.0f / 1.15f;
    limits();
    C.dirty = 1, C.dirty_at = armGetSystemTick();
  }
  if (dx || dy) {
    C.x += (float)dx * C.mouse;
    C.y += (float)dy * C.mouse;
    clamp();
    C.visible = 1;
  }
  return (buttons & HidMouseButton_Left) ? 1 : 0;
}

/* ------------------------------------------------------------ per update */
/* Returns the text of a change worth showing (a speed), or NULL. mx, my: the
 * stick that moves it (the left in a level, the right in the menus). menu:
 * the menus' cursor, which only moves and touches (the D-pad, B and + are
 * the menus'). */
const char *abs_cursor_update(PadState *pad, u64 down, u64 held, float lsx, float lsy, u64 k_a, u64 k_b,
                              int real_touch, int menu) {
  static char msg[64];
  const char *say = NULL;
  const float k1080 = (float)abs_surface_h() / 1080.0f;
  if (menu) {
    down &= ~(HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_L | HidNpadButton_Up | HidNpadButton_Down | k_b);
    held &= ~(HidNpadButton_Up | HidNpadButton_Down);
    C.visible = 1;
  }
  if (down & HidNpadButton_Plus) {
    C.visible = !C.visible;
    if (!C.visible)
      abs_cursor_leave();
  }
  if (down & HidNpadButton_Minus) {
    if (C.mouse_on)
      say = "Gyro: off while a mouse is connected";
    else if (!C.gyro_ready)
      say = "Gyro: not available";
    else {
      C.gyro_on = !C.gyro_on;
      if (C.gyro_on)
        C.visible = 1;
      say = C.gyro_on ? "Gyro pointing: on" : "Gyro pointing: off";
    }
  }
  if (down & HidNpadButton_L) {
    C.x = abs_surface_w() * 0.5f, C.y = abs_surface_h() * 0.5f;
    C.visible = 1;
  }
  /* D-pad up/down: the speed of what moves the cursor */
  int step = 0;
  if (held & (HidNpadButton_Up | HidNpadButton_Down)) {
    if (++C.dpad_hold > 24 && C.dpad_hold % 3 == 0)
      step = (held & HidNpadButton_Up) ? 1 : -1;
  } else {
    C.dpad_hold = 0;
  }
  if (down & HidNpadButton_Up) step = 1;
  if (down & HidNpadButton_Down) step = -1;
  if (step) {
    const float f = step > 0 ? 1.15f : 1.0f / 1.15f;
    if (C.mouse_on) {
      C.mouse *= f, limits();
      snprintf(msg, sizeof msg, "Mouse speed %.2f", (double)C.mouse);
    } else if (C.gyro_on) {
      C.gyro *= f, limits();
      snprintf(msg, sizeof msg, "Gyro speed %.2f", (double)C.gyro);
    } else {
      C.stick *= f, limits();
      snprintf(msg, sizeof msg, "Cursor speed %.0f", (double)C.stick);
    }
    say = msg;
    C.dirty = 1, C.dirty_at = armGetSystemTick();
  }
  if (C.dirty && armTicksToNs(armGetSystemTick() - C.dirty_at) >= SAVE_AFTER_NS)
    settings_save();

  const int mouse_tap = mouse();
  if (C.gyro_on && !C.mouse_on) {
    HidSixAxisSensorState g = {0};
    float sx, sy;
    if (read_gyro(pad, &g, &sx, &sy)) {
      const float gain = GYRO_GAIN * C.gyro * k1080;
      float dx = sx * g.angular_velocity.y * gain, dy = sy * g.angular_velocity.x * gain;
      if (fabsf(dx) > 0.05f || fabsf(dy) > 0.05f) {
        C.x += dx, C.y += dy;
        clamp();
      }
    }
  }
  if (!C.visible) {
    C.tap_prev = 0;
    goto out;
  }
  if (lsx || lsy) {
    C.x += lsx * C.stick;
    C.y -= lsy * C.stick;
    clamp();
  }
  {
    int tap = ((held & (k_a | HidNpadButton_ZL | HidNpadButton_ZR)) ? 1 : 0) | mouse_tap;
    if (real_touch)
      tap = 0; /* a finger on the screen wins */
    if (tap && !C.tap_prev)
      abs_input_syn(0, C.x, C.y);
    else if (tap && C.tap_prev)
      abs_input_syn(2, C.x, C.y);
    else if (!tap && C.tap_prev)
      abs_input_syn(1, C.x, C.y);
    C.tap_prev = tap;
  }
  if ((down & k_b) && !menu)
    abs_key(4 /* BACK */, 1), abs_key(4, 0);
out:
  mutexLock(&g_lock);
  g_draw.visible = C.visible;
  g_draw.grab = C.tap_prev;
  g_draw.x = C.x, g_draw.y = C.y;
  mutexUnlock(&g_lock);
  return say;
}

/* ------------------------------------------------------------ drawing */
/* Inside abs_ov_begin/end, render thread. */
void abs_cursor_draw(void) {
  mutexLock(&g_lock);
  typeof(g_draw) d = g_draw;
  mutexUnlock(&g_lock);
  if (!d.visible)
    return;
  const float H = (float)abs_surface_h();
  if (g_hands) {
    /* the mod's hand, as the PC game draws it: its own size on a 768-line
     * screen, from its hot spot */
    const int press = d.grab || armGetSystemTick() < d.click_until;
    int k = d.style == ABS_CURSOR_LEVEL ? (press ? HAND_GRAB : HAND_HOVER) : (press ? HAND_CLICK : HAND_POINT);
    if (g_hand[k].rgba && !g_hand[k].tex) {
      g_hand[k].tex = abs_ov_texture(g_hand[k].rgba, g_hand[k].w, g_hand[k].h);
      free(g_hand[k].rgba);
      g_hand[k].rgba = NULL;
    }
    if (g_hand[k].tex) {
      const float s = H / 768.0f * dcr_config()->cursor_size;
      abs_ov_image(g_hand[k].tex, d.x - g_hand[k].px * s, d.y - g_hand[k].py * s, g_hand[k].w * s, g_hand[k].h * s,
                   0xffffffffu);
      return;
    }
  }
  /* no pictures: an arrow, dark outline and white fill (yellow held) */
  const float s = 1.6f * H / 720.0f * dcr_config()->cursor_size;
  static const float arrow[][2] = {{0, 0}, {0, 16}, {4, 12}, {7, 18}, {10, 16.5f}, {7, 10.5f}, {12, 10}};
  for (int pass = 0; pass < 2; pass++) {
    float sc = pass ? s : s * 1.25f;
    float ox = pass ? 0 : -1.5f * s, oy = pass ? 0 : -1.5f * s;
    uint32_t col = pass ? (d.grab ? 0xffe63cffu : 0xffffffffu) : 0x000000d8u;
    for (int i = 1; i + 1 < 7; i++) {
      float tri[6] = {d.x + ox + arrow[0][0] * sc, d.y + oy + arrow[0][1] * sc,
                      d.x + ox + arrow[i][0] * sc, d.y + oy + arrow[i][1] * sc,
                      d.x + ox + arrow[i + 1][0] * sc, d.y + oy + arrow[i + 1][1] * sc};
      abs_ov_tri(tri, col);
    }
  }
}
