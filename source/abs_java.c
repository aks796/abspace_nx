/* abs_java.c -- the Java side of Angry Birds Space, answered in C.
 *
 * The game's Java (com.rovio.fusion.*: App, NativeApplication, MySurfaceView,
 * AudioOutput, EGLWrapper, ...) does not run here. The engine calls back into
 * it through JNI, and Fusion's JNI helper builds each method's signature at
 * run time from C++ types, so the handlers below match by class and name (a
 * NULL signature: any) and read their arguments by the signature the engine
 * asked for (jni_core.c decodes them). Classes are found the way Fusion finds
 * them: Globals.getActivity().getClassLoader().findClass(name), answered from
 * the APK's own class list (classes.txt, dcr_setup.c).
 *
 * What each does is what the decompiled Java does on a device with no
 * network, no store, no browser and no notifications. Rovio's cloud SDK
 * (com.rovio.rcs.*: ads, identity, payments, social) and Flurry get neutral
 * answers. Anything the engine asks for that has no handler is logged once
 * ("[jni] unhandled ..."): that list is the to-do list. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "abs.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "jni.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

#define PKG PORT_PACKAGE
#define A_CACHE "/data/data/" PKG "/cache/"
#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity, *g_asset_manager, *g_class_loader, *g_surface_view;

/* ================================================================ generic */
/* What an unhandled getter of this return type answers without complaint. */
H(h_neutral) {
  switch (m->ret) {
  case 'L': {
    const char *r = strchr(m->sig, ')');
    if (r && !strcmp(r + 1, "Ljava/lang/String;"))
      return jv_l(jni_str(""));
    return jv_l(NULL);
  }
  case 'J': return jv_j(0);
  case 'F': return jv_f(0.0f);
  case 'D': return jv_d(0.0);
  default: return jv_none();
  }
}
H(h_void) { return jv_none(); }
H(h_false) { return jv_z(0); }
H(h_null) { return jv_l(NULL); }
H(h_self) { return jv_l(jni_retain(self)); }

/* ============================================================ java.* */
/* ---- java/util/ArrayList: p = an 'L' array (capacity), v[0] = size ---- */
static void list_finalize(JObj *l) { jni_release(l->p); }

static void list_init(JObj *l) {
  if (l->p)
    return;
  l->p = jni_array('L', 8);
  l->v[0] = 0;
  l->finalize = list_finalize;
}

JObj *abs_list_new(void) {
  JObj *l = jni_new("java/util/ArrayList");
  list_init(l);
  return l;
}

void abs_list_add(JObj *l, JObj *item) {
  list_init(l);
  JObj *arr = l->p;
  if (l->v[0] >= arr->a.len) {
    JObj *bigger = jni_array('L', arr->a.len * 2);
    for (jsize i = 0; i < arr->a.len; i++)
      ((JObj **)bigger->a.data)[i] = jni_retain(((JObj **)arr->a.data)[i]);
    jni_release(arr);
    l->p = arr = bigger;
  }
  ((JObj **)arr->a.data)[l->v[0]++] = jni_retain(item);
}

H(h_list_init) {
  list_init(self);
  return jv_none();
}
H(h_list_add) {
  abs_list_add(self, a[0].l);
  return jv_z(1);
}
H(h_list_size) { return jv_i(self && self->p ? (jint)self->v[0] : 0); }
H(h_list_get) {
  if (!self || !self->p || a[0].i < 0 || a[0].i >= self->v[0])
    return jv_l(NULL);
  return jv_l(jni_retain(((JObj **)((JObj *)self->p)->a.data)[a[0].i]));
}

/* ---- java.lang.ClassLoader: what Fusion finds its Java classes with ---- */
H(h_findClass) {
  char name[128];
  snprintf(name, sizeof name, "%s", jni_utf(a[0].l));
  for (char *p = name; *p; p++)
    if (*p == '.')
      *p = '/';
  if (!name[0] || !jni_class_exists(name)) {
    jni_throw("java/lang/ClassNotFoundException", name);
    return jv_l(NULL);
  }
  return jv_l(jni_class(name)->obj);
}

/* ---- Locale / Currency / UUID ---- */
static char g_locale[8] = "en_US";
const char *abs_locale(void) { return g_locale; }

/* config.ini [game] language, or the console's. */
static void choose_locale(void) {
  const char *want = dcr_config()->locale;
  if (want[0]) {
    if (!strcasecmp(want, "zh"))
      snprintf(g_locale, sizeof g_locale, "zh_CN");
    else if (!strcasecmp(want, "zh_tw"))
      snprintf(g_locale, sizeof g_locale, "zh_TW");
    else if (strlen(want) == 2) {
      static const struct { const char *l, *c; } region[] = {
          {"en", "US"}, {"ja", "JP"}, {"ko", "KR"}, {"sv", "SE"}, {"da", "DK"},
          {"nb", "NO"}, {"pt", "BR"}, {NULL, NULL}};
      const char *c = NULL;
      for (int i = 0; region[i].l; i++)
        if (!strcasecmp(want, region[i].l))
          c = region[i].c;
      snprintf(g_locale, sizeof g_locale, "%c%c_%c%c", want[0] | 0x20, want[1] | 0x20,
               c ? c[0] : want[0] & ~0x20, c ? c[1] : want[1] & ~0x20);
    } else
      snprintf(g_locale, sizeof g_locale, "%s", want);
    debugPrintf("[java] language: %s (config.ini)\n", g_locale);
    return;
  }
  u64 code = 0;
  SetLanguage lang = SetLanguage_ENUS;
  if (R_SUCCEEDED(setInitialize())) {
    if (R_SUCCEEDED(setGetSystemLanguage(&code)))
      setMakeLanguage(code, &lang);
    setExit();
  }
  static const char *const map[] = {
      [SetLanguage_JA] = "ja_JP",   [SetLanguage_ENUS] = "en_US", [SetLanguage_FR] = "fr_FR",
      [SetLanguage_DE] = "de_DE",   [SetLanguage_IT] = "it_IT",   [SetLanguage_ES] = "es_ES",
      [SetLanguage_ZHCN] = "zh_CN", [SetLanguage_KO] = "ko_KR",   [SetLanguage_NL] = "nl_NL",
      [SetLanguage_PT] = "pt_PT",   [SetLanguage_RU] = "ru_RU",   [SetLanguage_ZHTW] = "zh_TW",
      [SetLanguage_ENGB] = "en_GB", [SetLanguage_FRCA] = "fr_CA", [SetLanguage_ES419] = "es_MX",
      [SetLanguage_ZHHANS] = "zh_CN", [SetLanguage_ZHHANT] = "zh_TW", [SetLanguage_PTBR] = "pt_BR",
  };
  const char *l = (unsigned)lang < sizeof map / sizeof map[0] && map[lang] ? map[lang] : "en_US";
  snprintf(g_locale, sizeof g_locale, "%s", l);
  debugPrintf("[java] language: %s (the console's)\n", g_locale);
}

H(h_locale_getDefault) { return jv_l(jni_singleton("java/util/Locale")); }
H(h_locale_toString) { return jv_l(jni_str(g_locale)); }
H(h_locale_language) {
  char l[3] = {g_locale[0], g_locale[1], 0};
  return jv_l(jni_str(l));
}
H(h_locale_country) { return jv_l(jni_str(strlen(g_locale) >= 5 ? g_locale + 3 : "")); }
H(h_currency_getInstance) { return jv_l(jni_singleton("java/util/Currency")); }
H(h_currency_code) { return jv_l(jni_str("USD")); }

H(h_uuid_random) {
  u8 r[16];
  randomGet(r, sizeof r);
  r[6] = (r[6] & 0x0f) | 0x40;
  r[8] = (r[8] & 0x3f) | 0x80;
  JObj *u = jni_new("java/util/UUID");
  char s[40];
  snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", r[0],
           r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14],
           r[15]);
  jni_set_field(u, "value", jv_l(jni_str(s)), 1);
  return jv_l(u);
}
H(h_uuid_toString) {
  jvalue v = jni_get_field(self, "value");
  return jv_l(v.l ? jni_retain(v.l) : jni_str("00000000-0000-4000-8000-000000000000"));
}

H(h_throwable_msg) { return jv_l(jni_str(self && self->p ? (const char *)self->p : "")); }

/* =============================================== android.* / the activity */
H(h_getAssets) { return jv_l(jni_retain(g_asset_manager)); }
H(h_getClassLoader) { return jv_l(jni_retain(g_class_loader)); }
H(h_getPackageName) { return jv_l(jni_str(PKG)); }
H(h_isSilentProfile) { return jv_z(0); }
H(h_quitRequested) {
  debugPrintf("[java] App.quitRequested(): the game is closing\n");
  abs_request_exit();
  return jv_none();
}
H(h_allowSleep) {
  /* the screen stays on while the game plays (it dims with the console's
   * own setting when the game allows sleep) */
  static int last = -1;
  if (a[0].z != last) {
    last = a[0].z;
    appletSetIdleTimeDetectionExtension(a[0].z ? AppletIdleTimeDetectionExtension_None
                                               : AppletIdleTimeDetectionExtension_Extended);
  }
  return jv_none();
}

/* ---- com.rovio.fusion.Globals (static) ---- */
H(h_getActivity) { return jv_l(jni_retain(g_activity)); }
H(h_getAPILevel) { return jv_i(22); } /* Android 5.1: no run-time permissions */
H(h_getConnectivityManager) { return jv_l(jni_singleton("android/net/ConnectivityManager")); }
H(h_getCacheDir) { return jv_l(jni_str(A_CACHE)); }
H(h_runAppThreadRunnables) { return jv_none(); }

/* ---- DeviceInfoWrapper (static): a 6.2" 1280x720 panel ---- */
H(h_ppi) { return jv_i(237); }
H(h_disp_w) { return jv_i(abs_surface_w()); }
H(h_disp_h) { return jv_i(abs_surface_h()); }
H(h_density_group) { return jv_i(240); } /* DENSITY_HIGH */
H(h_config_group) { return jv_i(3); }    /* SCREENLAYOUT_SIZE_LARGE */
H(h_hasSystemFeature) {
  const char *f = jni_utf(a[0].l);
  int yes = !strncmp(f, "android.hardware.touchscreen", 28) || !strcmp(f, "android.hardware.gamepad");
  return jv_z(yes);
}

/* ---- ApplicationVersion / DeviceIDCreator ---- */
H(h_version) {
  const char *v = dcr_manifest_version_name();
  return jv_l(jni_str(v && *v ? v : ABS_VERSION));
}

/* One id per installation, kept next to the saves (the Java stores a UUID in
 * its preferences the same way). */
H(h_unique_id) {
  static char id[40];
  if (!id[0]) {
    char path[300];
    snprintf(path, sizeof path, "%s/data/files/.deviceid", dcr_game_root());
    FILE *f = fopen(path, "r");
    if (f) {
      if (!fgets(id, sizeof id, f))
        id[0] = 0;
      fclose(f);
      id[strcspn(id, "\r\n")] = 0;
    }
    if (strlen(id) < 16) {
      u8 r[16];
      randomGet(r, sizeof r);
      for (int i = 0; i < 16; i++)
        snprintf(id + 2 * i, 3, "%02x", r[i]);
      if ((f = fopen(path, "w"))) {
        fputs(id, f);
        fclose(f);
      }
    }
  }
  return jv_l(jni_str(id));
}

/* ---- AudioOutput: the engine's mixer (abs_audio.c) ---- */
H(h_ao_init) {
  abs_audio_new(self, a[0].j, a[1].i, a[2].i, a[3].i, a[4].i);
  return jv_none();
}
H(h_ao_start) { return jv_z(abs_audio_start(self)); }
H(h_ao_stop) {
  abs_audio_stop(self);
  return jv_none();
}

/* ---- EGLWrapper (static): shared contexts for the loading thread ---- */
H(h_egl_init) {
  abs_eglw_init();
  return jv_none();
}
H(h_egl_current) { return jv_i(abs_eglw_current()); }
H(h_egl_create) { return jv_i(abs_eglw_create_shared(a[0].i)); }
H(h_egl_destroy) {
  abs_eglw_destroy_shared(a[0].i);
  return jv_none();
}
H(h_egl_register) { return jv_z(abs_eglw_register_thread(a[0].i)); }
H(h_egl_unregister) {
  abs_eglw_unregister_thread();
  return jv_none();
}

/* ---- SystemFontRenderer: text in Android's fonts (Rovio's cloud UI; the
 * game's own text is bitmap fonts). Metrics from the size; pictures from the
 * Switch's font when abs_font.c has it, else blank. ---- */
typedef struct {
  int size, color, outline, outline_color;
} SysFont;
int abs_font_measure(float px, const char *utf8, int *w, int *left);
int abs_font_render_argb(float px, const char *utf8, uint32_t color, int outline,
                         uint32_t outline_color, uint32_t *out, int w, int h, int ascent);

static SysFont *sysfont(JObj *self) { return self ? (SysFont *)self->p : NULL; }
static void sysfont_free(JObj *o) { free(o->p); }

H(h_sf_init) {
  SysFont *f = calloc(1, sizeof *f);
  if (f) {
    f->size = a[1].i > 0 ? a[1].i : 16;
    f->color = a[2].i;
    f->outline = a[3].i > 0 ? a[3].i : 0;
    f->outline_color = a[4].i;
  }
  self->p = f;
  self->finalize = sysfont_free;
  static int logged;
  if (!logged++)
    debugPrintf("[java] SystemFontRenderer(\"%s\", %d): text drawn with the console's font\n",
                jni_utf(a[0].l), a[1].i);
  return jv_none();
}
static int sf_ascent(const SysFont *f) { return (f->size * 93 + 50) / 100; }
static int sf_descent(const SysFont *f) { return (f->size * 24 + 50) / 100; }
static int sf_width(const SysFont *f, const char *s, int *left) {
  int w = 0, l = 0;
  if (abs_font_measure((float)f->size, s, &w, &l) != 0) {
    w = (int)(strlen(s) * f->size * 55 / 100);
    l = 0;
  }
  if (left)
    *left = l;
  return w;
}
H(h_sf_width) {
  SysFont *f = sysfont(self);
  return jv_i(f ? sf_width(f, jni_utf(a[0].l), NULL) : 0);
}
H(h_sf_height) {
  SysFont *f = sysfont(self);
  return jv_i(f ? sf_ascent(f) + sf_descent(f) + 1 : 0);
}
H(h_sf_left) {
  SysFont *f = sysfont(self);
  int l = 0;
  if (f)
    sf_width(f, jni_utf(a[0].l), &l);
  return jv_i(l);
}
H(h_sf_top) {
  SysFont *f = sysfont(self);
  return jv_i(f ? -sf_ascent(f) : 0);
}
H(h_sf_leading) { return jv_i(0); }
H(h_sf_ascender) {
  SysFont *f = sysfont(self);
  return jv_i(f ? sf_ascent(f) : 0);
}
H(h_sf_descender) {
  SysFont *f = sysfont(self);
  return jv_i(f ? sf_descent(f) : 0);
}
H(h_sf_draw) {
  SysFont *f = sysfont(self);
  const char *s = jni_utf(a[0].l);
  if (!f)
    return jv_l(jni_array('I', 0));
  int w = sf_width(f, s, NULL) + 2 * f->outline, h = sf_ascent(f) + sf_descent(f) + 1 + 2 * f->outline;
  if (w <= 0 || h <= 0)
    return jv_l(jni_array('I', 0));
  JObj *arr = jni_array('I', w * h);
  abs_font_render_argb((float)f->size, s, (uint32_t)f->color, f->outline, (uint32_t)f->outline_color,
                       (uint32_t *)arr->a.data, w, h, sf_ascent(f) + f->outline);
  return jv_l(arr);
}

/* ---- AlertDialogWrapper: the port's own dialog over the game ---- */
H(h_alert_init) {
  /* (long owner, long listener, int id, title, message, positive, neutral, negative) */
  jni_set_field(self, "owner", jv_j(a[0].j), 0);
  jni_set_field(self, "listener", jv_j(a[1].j), 0);
  jni_set_field(self, "id", jv_i(a[2].i), 0);
  static const char *const names[] = {"title", "message", "positive", "neutral", "negative"};
  for (int i = 0; i < 5; i++)
    jni_set_field(self, names[i], jv_l(a[3 + i].l), 1);
  return jv_none();
}
H(h_alert_show) {
  abs_dialog_show(self);
  return jv_none();
}
H(h_alert_dismiss) {
  abs_dialog_dismiss(self);
  return jv_none();
}

/* ---- Launcher / AppStoreLauncher: no browser, mail or store ----
 * A link the game opens (the tiny planets', normally caught earlier in its
 * Lua: abs_ctl.lua) gets the port's page about its topic (abs_extras.c). */
H(h_open_url) {
  const char *url = a[0].l ? jni_utf(a[0].l) : "";
  if (dcr_config()->planet_info && !strcmp(m->name, "openURL") && strstr(url, "t=")) {
    debugPrintf("[java] %s(%s): the port's page\n", m->name, url);
    abs_extras_open(url);
    return m->ret == 'Z' ? jv_z(1) : jv_none();
  }
  debugPrintf("[java] %s(%s): not available on this console\n", m->name, url);
  return m->ret == 'Z' ? jv_z(0) : jv_none();
}
H(h_text_input) {
  debugPrintf("[java] TextInput.enableTextInput(%d)\n", a[0].z);
  return jv_none();
}

/* ---- rovio cloud services: an offline device ---- */
H(h_rcs_utils) {
  const char *n = m->name;
  if (!strcmp(n, "packageName"))
    return jv_l(jni_str(PKG));
  if (!strcmp(n, "userAgentString"))
    return jv_l(jni_str("Mozilla/5.0 (Linux; Android 5.1; Nintendo Switch)"));
  if (!strcmp(n, "getPPI"))
    return jv_i(237);
  if (!strcmp(n, "getViewWidth"))
    return jv_i(abs_surface_w());
  if (!strcmp(n, "getViewHeight"))
    return jv_i(abs_surface_h());
  if (!strcmp(n, "targetSdkVersion"))
    return jv_i(26);
  if (!strcmp(n, "networkType"))
    return m->ret == 'I' ? jv_i(-1) : jv_l(jni_str("none"));
  return h_neutral(self, a, m);
}
H(h_rcs_locale) { return jv_l(jni_str(g_locale)); }

/* =============================================================== table */
#define F "com/rovio/fusion/"
#define RCS "com/rovio/rcs/"

const JMethodDef jni_method_defs[] = {
    {"java/util/ArrayList", "<init>", NULL, h_list_init},
    {"java/util/ArrayList", "add", NULL, h_list_add},
    {"java/util/ArrayList", "size", NULL, h_list_size},
    {"java/util/ArrayList", "get", NULL, h_list_get},
    {"java/util/List", "size", NULL, h_list_size},
    {"java/util/List", "get", NULL, h_list_get},
    {"java/util/HashMap", "<init>", NULL, h_void},
    {"java/util/HashMap", "put", NULL, h_null},
    {"java/lang/ClassLoader", "findClass", NULL, h_findClass},
    {"java/lang/ClassLoader", "loadClass", NULL, h_findClass},
    {"java/lang/Throwable", "toString", NULL, h_throwable_msg},
    {"java/lang/Throwable", "getMessage", NULL, h_throwable_msg},
    {"java/util/Locale", "getDefault", NULL, h_locale_getDefault},
    {"java/util/Locale", "toString", NULL, h_locale_toString},
    {"java/util/Locale", "getLanguage", NULL, h_locale_language},
    {"java/util/Locale", "getCountry", NULL, h_locale_country},
    {"java/util/Currency", "getInstance", NULL, h_currency_getInstance},
    {"java/util/Currency", "getCurrencyCode", NULL, h_currency_code},
    {"java/util/Currency", "toString", NULL, h_currency_code},
    {"java/util/UUID", "randomUUID", NULL, h_uuid_random},
    {"java/util/UUID", "toString", NULL, h_uuid_toString},
    {"android/location/Criteria", "<init>", NULL, h_void},
    {"android/location/Criteria", "setAccuracy", NULL, h_void},
    {"android/net/ConnectivityManager", "getActiveNetwork", NULL, h_null},
    {"android/net/ConnectivityManager", "getActiveNetworkInfo", NULL, h_null},

    {"android/content/Context", "getAssets", NULL, h_getAssets},
    {"android/content/Context", "getClassLoader", NULL, h_getClassLoader},
    {"android/content/Context", "getPackageName", NULL, h_getPackageName},
    {F "App", "isSilentProfile", NULL, h_isSilentProfile},
    {F "App", "quitRequested", NULL, h_quitRequested},
    {F "App", "allowSleep", NULL, h_allowSleep},

    {F "Globals", "getActivity", NULL, h_getActivity},
    {F "Globals", "getAPILevel", NULL, h_getAPILevel},
    {F "Globals", "getConnectivityManager", NULL, h_getConnectivityManager},
    {F "Globals", "getPathToFileCacheDirectory", NULL, h_getCacheDir},
    {F "Globals", "runAppThreadRunnables", NULL, h_runAppThreadRunnables},
    {F "DeviceInfoWrapper", "getPPI", NULL, h_ppi},
    {F "DeviceInfoWrapper", "getDisplayWidth", NULL, h_disp_w},
    {F "DeviceInfoWrapper", "getDisplayHeight", NULL, h_disp_h},
    {F "DeviceInfoWrapper", "getDisplayDensityGroup", NULL, h_density_group},
    {F "DeviceInfoWrapper", "getDisplayConfigurationGroup", NULL, h_config_group},
    {F "DeviceInfoWrapper", "hasSystemFeature", NULL, h_hasSystemFeature},
    {F "ApplicationVersion", "getApplicationVersionString", NULL, h_version},
    {F "DeviceIDCreator", "getUniqueId", NULL, h_unique_id},
    {F "AudioOutput", "<init>", NULL, h_ao_init},
    {F "AudioOutput", "startOutput", NULL, h_ao_start},
    {F "AudioOutput", "stopOutput", NULL, h_ao_stop},
    {F "AudioOutput", "requestExclusiveAudio", NULL, h_void},
    {F "EGLWrapper", "init", NULL, h_egl_init},
    {F "EGLWrapper", "getCurrentContext", NULL, h_egl_current},
    {F "EGLWrapper", "createSharedContext", NULL, h_egl_create},
    {F "EGLWrapper", "destroySharedContext", NULL, h_egl_destroy},
    {F "EGLWrapper", "registerThread", NULL, h_egl_register},
    {F "EGLWrapper", "unregisterThread", NULL, h_egl_unregister},
    {F "SystemFontRenderer", "<init>", NULL, h_sf_init},
    {F "SystemFontRenderer", "drawString", NULL, h_sf_draw},
    {F "SystemFontRenderer", "getWidth", NULL, h_sf_width},
    {F "SystemFontRenderer", "getHeight", NULL, h_sf_height},
    {F "SystemFontRenderer", "getLeft", NULL, h_sf_left},
    {F "SystemFontRenderer", "getTop", NULL, h_sf_top},
    {F "SystemFontRenderer", "getLeading", NULL, h_sf_leading},
    {F "SystemFontRenderer", "getAscender", NULL, h_sf_ascender},
    {F "SystemFontRenderer", "getDescender", NULL, h_sf_descender},
    {F "TextInput", "enableTextInput", NULL, h_text_input},
    {F "AlertDialogWrapper", "<init>", NULL, h_alert_init},
    {F "AlertDialogWrapper", "show", NULL, h_alert_show},
    {F "AlertDialogWrapper", "dismiss", NULL, h_alert_dismiss},
    {F "Launcher", "openURL", NULL, h_open_url},
    {F "Launcher", "openProgram", NULL, h_open_url},
    {F "Launcher", "canOpenProgram", NULL, h_false},
    {F "Launcher", "canOpenEmail", NULL, h_false},
    {F "Launcher", "openEmail", NULL, h_open_url},
    {F "AppStoreLauncher", "isSupported_Amazon", NULL, h_false},
    {F "AppStoreLauncher", "isSupported_GooglePlay", NULL, h_false},
    {F "AppStoreLauncher", "launch_GooglePlay", NULL, h_open_url},
    {F "AppStoreLauncher", "launch_Amazon", NULL, h_open_url},
    {F "LocalNotificationsWrapper", "<init>", NULL, h_void},
    {F "LocalNotificationsWrapper", "clearNotificationStack", NULL, h_void},
    {F "LocalNotificationsWrapper", "removeNotification", NULL, h_void},
    {F "LocalNotificationsWrapper", "notifyAfter", NULL, h_neutral},
    {F "RemoteNotificationsClientWrapper", "<init>", NULL, h_void},
    {F "RemoteNotificationsClientWrapper", "areRemoteNotificationsEnabled", NULL, h_false},
    {F "RemoteNotificationsClientWrapper", "areSettingsProvidedByThePlatform", NULL, h_false},
    {F "RemoteNotificationsClientWrapper", "setEnabled", NULL, h_void},
    {F "RemoteNotificationsClientWrapper", "stop", NULL, h_void},

    {"com/flurry/android/FlurryAgent", NULL, NULL, h_neutral},
    {RCS "core/Utils", NULL, NULL, h_rcs_utils},
    {RCS "Localization", "deviceLocale", NULL, h_rcs_locale},
    {RCS "Localization", "systemLocale", NULL, h_rcs_locale},
    {RCS "Localization", NULL, NULL, h_neutral},
    {RCS "InstallReferrerReceiver", NULL, NULL, h_void},
    {RCS "ads/Utils", NULL, NULL, h_neutral},
    {RCS "ads/AdsSdk", NULL, NULL, h_neutral},
    {RCS "ads/VideoPlayerBridge", NULL, NULL, h_neutral},
    {RCS "ads/WebViewWrapper", NULL, NULL, h_neutral},
    {RCS "ads/VASTParser", NULL, NULL, h_null},
    {RCS "IdentityLoginUI", NULL, NULL, h_neutral},
    {RCS "socialnetwork/SocialManagerWrapper", "createSocialManagerWrapper", NULL, h_self},
    {RCS "socialnetwork/SocialManagerWrapper", "numOfServices", NULL, h_neutral},
    {RCS "socialnetwork/SocialManagerWrapper", NULL, NULL, h_neutral},
    {RCS "payment/google/GooglePlayPaymentProvider", NULL, NULL, h_neutral},
    {RCS "payment/google/ReceiptHelper", NULL, NULL, h_false},
    {F "payment/amazon/AmazonPaymentProvider", NULL, NULL, h_neutral},
    {NULL, NULL, NULL, NULL},
};

/* android.os.Build's constants, as the engine's device report reads them
 * (static fields: a NULL string there would be read as one) */
const JFieldDef jni_field_defs[] = {
    {"android/os/Build$VERSION", "RELEASE", NULL, 0, "5.1"},
    {"android/os/Build$VERSION", "SDK_INT", NULL, 22, NULL},
    {"android/os/Build$VERSION", "CODENAME", NULL, 0, "REL"},
    {"android/os/Build", "MODEL", NULL, 0, "Switch"},
    {"android/os/Build", "MANUFACTURER", NULL, 0, "Nintendo"},
    {"android/os/Build", "BRAND", NULL, 0, "Nintendo"},
    {"android/os/Build", "PRODUCT", NULL, 0, "switch"},
    {"android/os/Build", "DEVICE", NULL, 0, "switch"},
    {"android/os/Build", "BOARD", NULL, 0, "tegra"},
    {"android/os/Build", "HARDWARE", NULL, 0, "tegra"},
    {"android/os/Build", "CPU_ABI", NULL, 0, "armeabi-v7a"},
    {"android/os/Build", "CPU_ABI2", NULL, 0, "armeabi"},
    {"android/os/Build", "SERIAL", NULL, 0, "unknown"},
    {"android/os/Build", "FINGERPRINT", NULL, 0, "nintendo/switch/switch:5.1/abspace_nx/1:user/release-keys"},
    {"android/location/Criteria", "ACCURACY_COARSE", NULL, 2, NULL},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {F "App", "android/support/v7/app/AppCompatActivity"},
    {"android/support/v7/app/AppCompatActivity", "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {F "MySurfaceView", "android/opengl/GLSurfaceView"},
    {"android/opengl/GLSurfaceView", "android/view/SurfaceView"},
    {"android/view/SurfaceView", "android/view/View"},
    {"java/util/ArrayList", "java/util/List"},
    {"java/nio/DirectByteBuffer", "java/nio/ByteBuffer"},
    {"dalvik/system/PathClassLoader", "java/lang/ClassLoader"},
    {"java/lang/ClassNotFoundException", "java/lang/Throwable"},
    {"java/lang/NoClassDefFoundError", "java/lang/Throwable"},
    {NULL, NULL},
};

/* Only used when classes.txt (the APK's class list) is missing. */
const char *const jni_missing_classes[] = {
    "com/rovio/fusion/HockeyAppWrapper", "com/rovio/fusion/Haptics", NULL,
};

/* ================================================================ setup */
void abs_java_init(void) {
  jni_init();
  g_jni_log = dcr_config()->log_jni;
  choose_locale();
  g_activity = jni_singleton(F "App");
  g_surface_view = jni_singleton(F "MySurfaceView");
  g_class_loader = jni_singleton("dalvik/system/PathClassLoader");
  g_asset_manager = jni_singleton("android/content/res/AssetManager");
  debugPrintf("[java] objects ready: activity %p view %p class loader %p assets %p\n",
              (void *)g_activity, (void *)g_surface_view, (void *)g_class_loader,
              (void *)g_asset_manager);
}
