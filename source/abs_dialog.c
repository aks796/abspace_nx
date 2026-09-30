/* abs_dialog.c -- com.rovio.fusion.AlertDialogWrapper: the game's native
 * dialogs (Rovio's terms of service and privacy notices, errors), shown by
 * the port over the game.
 *
 * The engine makes the Java object -- new AlertDialogWrapper(owner, listener,
 * id, title, message, positive, neutral, negative) -- and calls show(); the
 * Java shows an Android AlertDialog (not cancellable) and, when a button is
 * pressed, runs showAlertResultCallback(owner, listener, id, button) on the
 * GL thread, button 0 = positive, 1 = neutral, 2 = negative. dismiss() closes
 * it without an answer. Here the dialog is drawn with the console's font
 * (abs_font.c) and answered with the controller or the touchscreen:
 * left/right choose, A presses, B presses the negative button if there is one.
 * The answer is delivered on the engine's update thread, where the Java's
 * runOnGLThread would have put it. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "dcr_config.h"
#include "util.h"

typedef void (*fn_result)(void *env, void *thiz, jlong owner, jlong listener, jint id, jint button);

static Mutex g_lock;
static struct {
  int active;
  JObj *obj;
  char title[160];
  char message[1200];
  char button[3][48]; /* positive, neutral, negative ("" = none) */
  int sel;
} D;

static void copy_field(JObj *o, const char *name, char *out, size_t cap) {
  jvalue v = jni_get_field(o, name);
  snprintf(out, cap, "%s", v.l ? jni_utf(v.l) : "");
}

void abs_dialog_show(JObj *obj) {
  abs_font_init();
  mutexLock(&g_lock);
  if (D.obj)
    jni_release(D.obj);
  D.obj = jni_retain(obj);
  copy_field(obj, "title", D.title, sizeof D.title);
  copy_field(obj, "message", D.message, sizeof D.message);
  copy_field(obj, "positive", D.button[0], sizeof D.button[0]);
  copy_field(obj, "neutral", D.button[1], sizeof D.button[1]);
  copy_field(obj, "negative", D.button[2], sizeof D.button[2]);
  D.sel = D.button[0][0] ? 0 : D.button[1][0] ? 1 : 2;
  D.active = 1;
  mutexUnlock(&g_lock);
  debugPrintf("[dialog] \"%s\": \"%.200s\" [%s | %s | %s]\n", D.title, D.message, D.button[0],
              D.button[1], D.button[2]);
  if (!abs_font_ready())
    debugPrintf("[dialog] no font to show it with: it will be answered with its first button\n");
}

void abs_dialog_dismiss(JObj *obj) {
  mutexLock(&g_lock);
  if (D.obj == obj) {
    D.active = 0;
    jni_release(D.obj);
    D.obj = NULL;
  }
  mutexUnlock(&g_lock);
}

int abs_dialog_active(void) { return D.active; }

typedef struct {
  JObj *obj;
  int button;
} Answer;

static void deliver(void *p) {
  Answer *a = p;
  fn_result f = (fn_result)abs_native("Java_com_rovio_fusion_AlertDialogWrapper_showAlertResultCallback");
  if (f) {
    jvalue owner = jni_get_field(a->obj, "owner"), listener = jni_get_field(a->obj, "listener"),
           id = jni_get_field(a->obj, "id");
    if (listener.j)
      f(g_jni_env, a->obj, owner.j, listener.j, id.i, a->button);
  }
  jni_release(a->obj);
  free(a);
}

static void answer(int button) {
  Answer *a = malloc(sizeof *a);
  mutexLock(&g_lock);
  if (!D.active || !a) {
    mutexUnlock(&g_lock);
    free(a);
    return;
  }
  a->obj = D.obj; /* the reference passes to the answer */
  a->button = button;
  D.obj = NULL;
  D.active = 0;
  mutexUnlock(&g_lock);
  debugPrintf("[dialog] answered with button %d\n", button);
  abs_post_update(deliver, a);
}

/* ------------------------------------------------------------ layout */
static struct {
  float x, y, w, h;
} g_btn[3];
static int g_btn_valid;

void abs_dialog_input(uint64_t down, int touch, float tx, float ty) {
  if (!D.active)
    return;
  if (!abs_font_ready()) {
    answer(D.sel); /* nothing can be shown: the first button (the Java's default focus) */
    return;
  }
  const uint64_t k_a = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  const uint64_t k_b = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;
  int n = 0, idx[3];
  for (int i = 0; i < 3; i++)
    if (D.button[i][0])
      idx[n++] = i;
  if (!n) {
    if (down & (k_a | k_b))
      answer(0);
    return;
  }
  int pos = 0;
  for (int i = 0; i < n; i++)
    if (idx[i] == D.sel)
      pos = i;
  if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft))
    pos = (pos + n - 1) % n;
  if (down & (HidNpadButton_Right | HidNpadButton_StickLRight))
    pos = (pos + 1) % n;
  D.sel = idx[pos];
  if (touch && g_btn_valid) {
    for (int i = 0; i < 3; i++)
      if (D.button[i][0] && tx >= g_btn[i].x && tx < g_btn[i].x + g_btn[i].w && ty >= g_btn[i].y &&
          ty < g_btn[i].y + g_btn[i].h) {
        answer(i);
        return;
      }
  }
  if (down & k_a)
    answer(D.sel);
  else if ((down & k_b) && D.button[2][0])
    answer(2);
}

/* the message cut into lines no wider than `w` */
static int wrap(const char *msg, float px, float w, char lines[][160], int max) {
  int n = 0;
  char cur[160] = "";
  const char *p = msg;
  while (*p && n < max) {
    if (*p == '\n') {
      snprintf(lines[n++], 160, "%s", cur);
      cur[0] = 0;
      p++;
      continue;
    }
    const char *e = p;
    while (*e && *e != ' ' && *e != '\n')
      e++;
    char word[160];
    snprintf(word, sizeof word, "%.*s", (int)(e - p), p);
    char trial[320];
    snprintf(trial, sizeof trial, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (cur[0] && abs_ov_text_width(px, trial) > w) {
      snprintf(lines[n++], 160, "%s", cur);
      snprintf(cur, sizeof cur, "%s", word);
    } else {
      snprintf(cur, sizeof cur, "%.159s", trial);
    }
    p = *e == ' ' ? e + 1 : e;
  }
  if (cur[0] && n < max)
    snprintf(lines[n++], 160, "%s", cur);
  return n;
}

void abs_dialog_draw(void) {
  mutexLock(&g_lock);
  if (!D.active) {
    mutexUnlock(&g_lock);
    return;
  }
  char title[160], message[1200], button[3][48];
  memcpy(title, D.title, sizeof title);
  memcpy(message, D.message, sizeof message);
  memcpy(button, D.button, sizeof button);
  int sel = D.sel;
  mutexUnlock(&g_lock);

  const float W = (float)abs_surface_w(), H = (float)abs_surface_h();
  const float k = H / 720.0f;
  abs_ov_rect(0, 0, W, H, 0x000000a0u);
  const float pw = W * 0.62f, px = (W - pw) * 0.5f;
  const float tpx = 30 * k, mpx = 24 * k, bpx = 26 * k, gap = 18 * k;
  static char lines[16][160];
  int nl = wrap(message, mpx, pw - 2 * gap, lines, 16);
  float ph = gap + (title[0] ? tpx * 1.4f : 0) + nl * mpx * 1.3f + gap + bpx * 2.0f + gap;
  if (ph > H * 0.9f)
    ph = H * 0.9f;
  const float py = (H - ph) * 0.5f;
  abs_ov_rect(px, py, pw, ph, 0x1d2b4af4u);
  abs_ov_frame(px, py, pw, ph, 3 * k, 0x9fc2ffffu);
  float y = py + gap;
  if (title[0]) {
    abs_ov_text(px + gap, y, tpx, 0xffffffffu, title);
    y += tpx * 1.4f;
  }
  for (int i = 0; i < nl && y + mpx < py + ph - bpx * 2.5f; i++, y += mpx * 1.3f)
    abs_ov_text(px + gap, y, mpx, 0xdce6ffffu, lines[i]);
  /* buttons, right-aligned: negative, neutral, positive (Android's order) */
  float bx = px + pw - gap;
  const float by = py + ph - gap - bpx * 1.6f, bh = bpx * 1.6f;
  g_btn_valid = 0;
  static const int order[3] = {0, 1, 2};
  for (int o = 0; o < 3; o++) {
    int i = order[o];
    if (!button[i][0])
      continue;
    float tw = abs_ov_text_width(bpx, button[i]) + 2 * gap;
    bx -= tw;
    g_btn[i].x = bx, g_btn[i].y = by, g_btn[i].w = tw, g_btn[i].h = bh;
    abs_ov_rect(bx, by, tw, bh, i == sel ? 0xf2b705ffu : 0x3a4f7fffu);
    abs_ov_text(bx + gap, by + (bh - bpx) * 0.5f, bpx, i == sel ? 0x1d2b4affu : 0xffffffffu, button[i]);
    bx -= gap;
  }
  g_btn_valid = 1;
}
