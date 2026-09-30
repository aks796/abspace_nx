/* abs_boot.c -- plays the part of the game's Android activity and its
 * GLSurfaceView.
 *
 * com.rovio.fusion.App.onCreate, in Android's order, and what happens here:
 *   Globals.loadLibraries()     System.loadLibrary: the engine's constructors
 *                               (abs_loader.c), then JNI_OnLoad
 *   Globals.setActivity(this)   the activity object (abs_java.c)
 *   new MySurfaceView(this)     -> new NativeApplication(view, getFilesDir()):
 *                               nativeConfig(files dir), nativeGetPossible-
 *                               Orientations (landscape), nativeRenderThread()
 *                               which picks the Updater:
 *                                 false: SingleThreadWrapper -- every frame the
 *                                        GL thread runs doUpdate() (input +
 *                                        nativeUpdate, which also draws)
 *                                 true:  MultiThreadWrapper -- an update thread
 *                                        loops doUpdate() on a SHARED context,
 *                                        the GL thread draws with nativeRender()
 *   onStart/onResume            MyRenderer.onResume (queued: doResume)
 *   GL thread: onSurfaceCreated EGLWrapper.init; onSurfaceChanged(w, h) ->
 *                               initialize: nativeInit(w, h), nativeResume(),
 *                               nativeResize(w, h)
 * Then this (the main) thread IS the GL thread: it presents a frame per
 * display refresh. HOME is onPause (nativePause, the game saves) and onResume;
 * closing the game from HOME is Android's onDestroy, which kills the process
 * -- so: pause (the save), then exit, with a 5 s backstop. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "bionic.h"
#include "bionic_pthread.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_time.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "util.h"

void dcr_watchdog_start(void);
void dcr_boost_poll(void);
void dcr_boost_report(void);
void dcr_boost_launch_end(void);
void dcr_apkcache_report(void);
void dcr_dircache_report(void);
void abs_assets_report(void);
int b_pthread_create(b_pthread_t *out, const b_pthread_attr_t *attr, void *(*start)(void *), void *arg);
int b_pthread_attr_init(b_pthread_attr_t *a);
int b_pthread_attr_setstacksize(b_pthread_attr_t *a, size_t s);

#define A_FILES "/data/data/" DCR_PACKAGE "/files"
#define NA_ "Java_com_rovio_fusion_NativeApplication_"

/* ------------------------------------------------------------ natives */
typedef jint (*fn_onload)(void *vm, void *reserved);
typedef void (*fn_s)(void *env, void *thiz, void *s);
typedef jint (*fn_i)(void *env, void *thiz);
typedef jboolean (*fn_z)(void *env, void *thiz);
typedef void (*fn_v)(void *env, void *thiz);
typedef void (*fn_ii)(void *env, void *thiz, jint a, jint b);
typedef jboolean (*fn_zii)(void *env, void *thiz, jint a, jint b);

static struct {
  fn_s config;
  fn_i orientations;
  fn_z render_thread, update, render;
  fn_ii init;
  fn_zii resize;
  fn_v pause, resume, deinit, frame_clear, interrupt_render;
} N;

static JObj *g_native_app; /* the NativeApplication object natives are called on */
static int g_w, g_h, g_mt;
static volatile int g_exit, g_game_quit, g_focused = 1, g_focus_changed, g_started;
static volatile int g_resumed; /* NativeState.RENDER_READY */

int abs_surface_w(void) { return g_w ? g_w : DCR_FORCE_SCREEN_W; }
int abs_surface_h(void) { return g_h ? g_h : DCR_FORCE_SCREEN_H; }
int abs_is_multithreaded(void) { return g_mt; }
void abs_request_exit(void) { g_exit = 1; }

static void *need(const char *sym) {
  void *p = abs_native(sym);
  if (!p)
    debugPrintf("[boot] MISSING native %s\n", sym);
  return p;
}

/* For the watchdog: frames presented, and whether a stop is expected. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }
int dcr_boot_in_focus(void) { return g_focused && g_started && !g_exit; }

/* ------------------------------------------- work for the update thread */
/* Globals.runOnGLThread / runOnAppThread, and the MultiThreadWrapper's queue:
 * run at the top of the next update, on the update thread. */
#define MAX_POSTS 32
static struct {
  void (*fn)(void *);
  void *arg;
} g_posts[MAX_POSTS];
static int g_nposts;
static Mutex g_post_lock;
static CondVar g_post_cv;

void abs_post_update(void (*fn)(void *), void *arg) {
  mutexLock(&g_post_lock);
  if (g_nposts < MAX_POSTS) {
    g_posts[g_nposts].fn = fn;
    g_posts[g_nposts].arg = arg;
    g_nposts++;
  } else {
    debugPrintf("[boot] update queue full: dropped a request\n");
  }
  condvarWakeAll(&g_post_cv);
  mutexUnlock(&g_post_lock);
}

static void run_posts(void) {
  for (;;) {
    mutexLock(&g_post_lock);
    if (!g_nposts) {
      mutexUnlock(&g_post_lock);
      return;
    }
    void (*fn)(void *) = g_posts[0].fn;
    void *arg = g_posts[0].arg;
    memmove(&g_posts[0], &g_posts[1], sizeof g_posts[0] * (size_t)--g_nposts);
    mutexUnlock(&g_post_lock);
    fn(arg);
  }
}

/* A request the caller waits for (the Java's SyncToken). */
typedef struct {
  void (*fn)(void);
  volatile int done;
} Sync;

static void sync_run(void *p) {
  Sync *s = p;
  s->fn();
  s->done = 1;
}

/* ------------------------------------------------------ NativeApplication */
static void do_resume(void) {
  if (!g_resumed) {
    debugPrintf("[boot] nativeResume\n");
    N.resume(g_jni_env, g_native_app);
    g_resumed = 1;
  }
}

static void do_pause(void) {
  if (g_resumed) {
    debugPrintf("[boot] nativePause\n");
    N.pause(g_jni_env, g_native_app);
    g_resumed = 0;
  }
}

/* doUpdate(): MyInputHandler.handleEvents (our input, then the queued Java
 * work), then nativeUpdate. False: the game is finished. */
static int do_update(void) {
  abs_input_update();
  run_posts();
  abs_lua_frame_tick();
  return N.update(g_jni_env, g_native_app) != 0;
}

/* ------------------------------------------------ the update thread (MT) */
static int g_shared_ctx;
static volatile int g_update_thread_ready;

static void *update_thread(void *arg) {
  (void)arg;
  int b_pthread_setname_np(b_pthread_t th, const char *name);
  b_pthread_setname_np((b_pthread_t)b_thread_self(), "UpdateThread");
  if (!abs_eglw_register_thread(g_shared_ctx))
    debugPrintf("[boot] update thread: its shared GL context could not be made current\n");
  g_update_thread_ready = 1;
  debugPrintf("[boot] update thread running\n");
  while (!g_game_quit) {
    if (!g_resumed) {
      /* paused: wait for queued work (the resume) */
      run_posts();
      mutexLock(&g_post_lock);
      if (!g_nposts && !g_resumed && !g_game_quit)
        condvarWaitTimeout(&g_post_cv, &g_post_lock, 50000000ll);
      mutexUnlock(&g_post_lock);
      continue;
    }
    if (!do_update()) {
      debugPrintf("[boot] nativeUpdate returned false: the game is closing\n");
      g_game_quit = 1;
      N.interrupt_render(g_jni_env, g_native_app);
    }
  }
  abs_eglw_unregister_thread();
  debugPrintf("[boot] update thread finished\n");
  return NULL;
}

/* MultiThreadWrapper.b(): run on the update thread and wait, with a
 * nativeFrameClear so a render in progress does not hold it up. */
static void on_update_thread(void (*fn)(void), int clear) {
  if (!g_mt) {
    fn();
    return;
  }
  Sync s = {fn, 0};
  abs_post_update(sync_run, &s);
  if (clear)
    N.frame_clear(g_jni_env, g_native_app);
  u64 t0 = armGetSystemTick();
  while (!s.done && !g_game_quit) {
    svcSleepThread(1000000ll);
    if (armTicksToNs(armGetSystemTick() - t0) > 10000000000ull) {
      debugPrintf("[boot] the update thread did not take a request in 10 s\n");
      break;
    }
  }
}

/* ------------------------------------------------------------- lifecycle */
static AppletHookCookie g_hook;

static void on_applet(AppletHookType type, void *param) {
  static const char *const names[] = {"focus state", "operation mode", "performance mode",
                                      "EXIT REQUEST", "resume", "capture button",
                                      "screenshot taken", "request to display"};
  if (type == AppletHookType_OnExitRequest)
    debugPrintf("[applet] the system asked the game to close\n");
  else if ((unsigned)type < sizeof names / sizeof names[0])
    debugPrintf("[applet] %s (focus %d, mode %d)\n", names[type], (int)appletGetFocusState(),
                (int)appletGetOperationMode());
  if (type == AppletHookType_OnFocusState || type == AppletHookType_OnOperationMode) {
    int focused = appletGetFocusState() == AppletFocusState_InFocus;
    if (focused != g_focused) {
      g_focused = focused;
      g_focus_changed = 1;
    }
  }
}

static void apply_focus(void) {
  if (!g_focus_changed || !g_started)
    return;
  g_focus_changed = 0;
  if (!g_focused) {
    debugPrintf("[boot] focus lost: onPause\n");
    abs_audio_pause(1);
    on_update_thread(do_pause, 1);
    log_flush_ring();
    dcr_time_suspend();
  } else {
    dcr_time_resume();
    on_update_thread(do_resume, 0);
    abs_audio_pause(0);
    debugPrintf("[boot] focus regained: onResume\n");
  }
}

static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[boot] the game did not close within 5 s: ending the process\n");
  log_flush_ring();
  svcExitProcess();
}

static void exit_guard_start(void) {
  static Thread t;
  if (R_FAILED(threadCreate(&t, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)) ||
      R_FAILED(threadStart(&t)))
    debugPrintf("[boot] no exit backstop thread\n");
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  u64 tick = armGetSystemTick();
  unsigned long presented = (unsigned long)dcr_gl_frames();
  double fps = last_tick ? (double)(presented - last_presented) * 1e9 /
                               (double)armTicksToNs(tick - last_tick)
                         : 0.0;
  last_tick = tick;
  last_presented = presented;
  debugPrintf("[boot] %lu frames presented (%.1f fps), %lu audio pulls, %d Java objects\n", presented,
              fps, (unsigned long)abs_audio_mixes(), jni_live_objects());
  dcr_boost_report();
  dcr_apkcache_report();
  dcr_dircache_report();
  abs_assets_report();
}

/* before each present: the port's pictures over the game's */
static void present(void) { abs_input_draw(); }

static void housekeeping(int *launch_done, unsigned long *quiet_at, u64 *last_report) {
  dcr_boost_poll();
  unsigned long frames = (unsigned long)dcr_gl_frames();
  if (!*launch_done && frames > 0) {
    *launch_done = 1;
    dcr_boost_launch_end();
    debugPrintf("[boot] first frame presented\n");
    *quiet_at = frames + 180;
  }
  /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
   * written out every 10 s and by the watchdog. */
  if (*quiet_at && frames >= *quiet_at) {
    *quiet_at = 0;
    log_set_quiet(1);
  }
  u64 now = armGetSystemTick();
  if (armTicksToNs(now - *last_report) >= 10000000000ull) {
    *last_report = now;
    report();
    log_flush_ring();
  }
}

int abs_boot_run(void) {
  N.config = need(NA_ "nativeConfig");
  N.orientations = need(NA_ "nativeGetPossibleOrientations");
  N.render_thread = need(NA_ "nativeRenderThread");
  N.init = need(NA_ "nativeInit");
  N.resize = need(NA_ "nativeResize");
  N.pause = need(NA_ "nativePause");
  N.resume = need(NA_ "nativeResume");
  N.update = need(NA_ "nativeUpdate");
  N.render = need(NA_ "nativeRender");
  N.deinit = need(NA_ "nativeDeinit");
  N.frame_clear = need(NA_ "nativeFrameClear");
  N.interrupt_render = need(NA_ "nativeInterruptRender");
  if (!N.config || !N.init || !N.update || !N.resume || !N.pause)
    fatal_error("%s has no NativeApplication natives: is the APK Angry Birds Space HD 2.2.14?",
                ABS_LIB_GAME);

  /* the surface size first: the engine may ask for the display's size
   * (DeviceInfoWrapper) from its very first call */
  g_w = dcr_config()->res_w;
  g_h = dcr_config()->res_h;
  abs_java_init();
  g_native_app = jni_singleton("com/rovio/fusion/NativeApplication");
  dcr_watchdog_start();

  /* ---- App.onCreate: System.loadLibrary's JNI_OnLoad ---- */
  fn_onload onload = (fn_onload)so_try_find_addr_rx(&g_mod_game, "JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));

  /* ---- new NativeApplication(view, getFilesDir()) ---- */
  JObj *files = jni_str(A_FILES);
  debugPrintf("[boot] nativeConfig(%s)\n", A_FILES);
  N.config(g_jni_env, g_native_app, files);
  jni_release(files);
  jni_exception_report("nativeConfig");
  int orient = N.orientations ? N.orientations(g_jni_env, g_native_app) : 0;
  g_mt = N.render_thread ? N.render_thread(g_jni_env, g_native_app) != 0 : 0;
  debugPrintf("[boot] orientations 0x%x (%s), %s\n", orient,
              (orient & 0xA) ? "landscape" : "NO landscape: the picture may be wrong",
              g_mt ? "multi-threaded (update thread + render thread)" : "single-threaded");

  /* ---- the GL thread: onSurfaceCreated, onSurfaceChanged ---- */
  if (abs_egl_init() != 0)
    fatal_error("Could not set up the graphics (EGL): see debug.log.");
  abs_eglw_init();
  abs_input_init();
  dcr_present_hook = present;
  appletHook(&g_hook, on_applet, NULL);

  debugPrintf("[boot] nativeInit(%d, %d)\n", g_w, g_h);
  N.init(g_jni_env, g_native_app, g_w, g_h);
  jni_exception_report("nativeInit");
  do_resume(); /* MyRenderer.onResume ran first: the doInit resumes at once */
  if (!g_mt) {
    debugPrintf("[boot] nativeResize(%d, %d)\n", g_w, g_h);
    N.resize(g_jni_env, g_native_app, g_w, g_h);
  } else {
    g_shared_ctx = abs_eglw_create_shared(abs_eglw_current());
    b_pthread_attr_t attr;
    b_pthread_attr_init(&attr);
    b_pthread_attr_setstacksize(&attr, 2u << 20);
    b_pthread_t th;
    if (b_pthread_create(&th, &attr, update_thread, NULL) != 0)
      fatal_error("Could not start the game's update thread.");
    /* MultiThreadWrapper.initialize: nativeInit only (the first
     * onSurfaceChanged sends no resize); later resizes go to this thread */
  }
  g_started = 1;
  debugPrintf("[boot] activity up; this thread draws the frames now\n");
  log_flush_ring();

  /* ---- the frames ---- */
  u64 last_report = armGetSystemTick();
  int launch_done = 0;
  unsigned long quiet_at = 0;
  while (!g_exit && !g_game_quit && appletMainLoop()) {
    apply_focus();
    if (!g_focused) {
      svcSleepThread(50000000ll);
      continue;
    }
    if (!g_mt) {
      if (!do_update()) {
        debugPrintf("[boot] nativeUpdate returned false: the game is closing\n");
        g_game_quit = 1;
        break;
      }
    } else if (!g_resumed || !N.render(g_jni_env, g_native_app)) {
      /* MyRenderer.onDrawFrame with nothing to draw: clear to black */
      static void (*clear_color)(float, float, float, float);
      static void (*clear)(unsigned);
      if (!clear) {
        clear_color = (void (*)(float, float, float, float))dcr_gl_lookup("glClearColor");
        clear = (void (*)(unsigned))dcr_gl_lookup("glClear");
      }
      if (clear && clear_color) {
        clear_color(0, 0, 0, 1);
        clear(0x4000);
      }
    }
    abs_egl_swap();
    housekeeping(&launch_done, &quiet_at, &last_report);
  }

  /* ---- the way out ---- */
  log_set_quiet(0);
  appletUnhook(&g_hook);
  exit_guard_start();
  if (g_game_quit) {
    /* the game ended itself (its quit): doShutdown = nativeDeinit */
    debugPrintf("[boot] doShutdown: nativeFrameClear, nativeDeinit\n");
    if (N.frame_clear)
      N.frame_clear(g_jni_env, g_native_app);
    if (N.deinit)
      N.deinit(g_jni_env, g_native_app);
  } else {
    /* HOME > Close: onPause (the game saves), then the process ends, as
     * Android's onDestroy kills it */
    debugPrintf("[boot] leaving: onPause, then the process ends\n");
    if (!g_focused)
      dcr_time_resume(); /* frozen clocks would stall a timed wait in the pause */
    abs_audio_pause(1);
    on_update_thread(do_pause, 1); /* where the engine runs: the update thread in MT */
    g_game_quit = 1;               /* then that thread stops */
  }
  debugPrintf("[boot] the game has closed\n");
  log_flush_ring();
  return 0;
}

