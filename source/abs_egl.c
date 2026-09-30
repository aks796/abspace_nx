/* abs_egl.c -- the game's EGL, as its GLSurfaceView and EGLWrapper made it.
 *
 * The engine imports no EGL at all: on Android, MySurfaceView (a
 * GLSurfaceView: ES 2, setEGLConfigChooser(true) = a depth buffer) creates the
 * display, the window surface and the context, and runs MyRenderer on its GL
 * thread; com.rovio.fusion.EGLWrapper keeps a table of contexts --
 *   [0] a blank entry, [1] the GL thread's (added by init/getCurrentContext),
 *   [2..] contexts SHARED with one of them, made for the engine's other
 *         threads (the update thread in multi-threaded mode, loaders), each
 *         with a 64x64 pbuffer to be current on --
 * and the engine calls its static methods through JNI (abs_java.c). This file
 * does both with Mesa's EGL on the Switch's default window.
 *
 * Mesa's Switch platform has window surfaces only; a shared context with no
 * pbuffer is made current without a surface (EGL_KHR_surfaceless_context),
 * which is all a loading thread needs to upload textures. MIT.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "util.h"

#if DCR_GL_MESA
#include <EGL/egl.h>
#include <EGL/eglext.h>

void dcr_window_prepare(void); /* android_ndk.c */
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s); /* gl_mesa.c: frame count, hooks */

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_win = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;

#define MAX_CTX 16
typedef struct {
  EGLContext ctx;
  EGLSurface draw, read;
} CtxEntry;
static CtxEntry g_ctxs[MAX_CTX];
static int g_nctx;
static Mutex g_ctx_lock;

static int choose(const EGLint *attrs) {
  EGLint n = 0;
  return eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &n) && n > 0;
}

int abs_egl_init(void) {
  if (log_console_active())
    debugPrintf("[egl] handing the screen from the boot log to the game\n");
  log_console_close(); /* for good: see util.c */
  dcr_window_prepare();
  g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj = 0, min = 0;
  if (!eglInitialize(g_dpy, &maj, &min)) {
    debugPrintf("[egl] eglInitialize failed 0x%x\n", eglGetError());
    return -1;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint full[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  static const EGLint depth16[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                   EGL_DEPTH_SIZE, 16, EGL_NONE};
  static const EGLint any[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
  if (!choose(full) && !choose(depth16) && !choose(any)) {
    debugPrintf("[egl] no ES2 config (0x%x)\n", eglGetError());
    return -1;
  }
  EGLint r, g, b, al, d, s;
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_RED_SIZE, &r);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_GREEN_SIZE, &g);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_BLUE_SIZE, &b);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_ALPHA_SIZE, &al);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_DEPTH_SIZE, &d);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_STENCIL_SIZE, &s);
  g_win = eglCreateWindowSurface(g_dpy, g_cfg, (EGLNativeWindowType)nwindowGetDefault(), NULL);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g_win == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, g_win, g_win, g_ctx)) {
    debugPrintf("[egl] surface %p / context %p / make current failed (0x%x)\n", g_win, g_ctx,
                eglGetError());
    return -1;
  }
  eglSwapInterval(g_dpy, 1);
  const char *(*gs)(unsigned) = (const char *(*)(unsigned))eglGetProcAddress("glGetString");
  debugPrintf("[egl] EGL %d.%d, config R%dG%dB%dA%d depth %d stencil %d; %s | %s\n", maj, min, r, g, b, al,
              d, s, gs ? gs(0x1F01) : "?", gs ? gs(0x1F02) : "?");
  return 0;
}

void abs_egl_swap(void) { b_eglSwapBuffers(g_dpy, g_win); }

void abs_egl_release_current(void) { eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); }

void abs_egl_make_current_main(void) { eglMakeCurrent(g_dpy, g_win, g_win, g_ctx); }

/* ---------------------------------------------------------------- EGLWrapper */
static int add_locked(EGLContext c, EGLSurface dr, EGLSurface rd) {
  if (g_nctx >= MAX_CTX)
    return 0;
  g_ctxs[g_nctx] = (CtxEntry){c, dr, rd};
  return g_nctx++;
}

void abs_eglw_init(void) {
  mutexLock(&g_ctx_lock);
  if (!g_nctx)
    add_locked(EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE); /* [0]: blank, as the Java's */
  mutexUnlock(&g_ctx_lock);
  abs_eglw_current(); /* [1]: this (the GL) thread's */
}

int abs_eglw_current(void) {
  EGLContext cur = eglGetCurrentContext();
  mutexLock(&g_ctx_lock);
  for (int i = 0; i < g_nctx; i++)
    if (g_ctxs[i].ctx == cur) {
      mutexUnlock(&g_ctx_lock);
      return i;
    }
  int i = add_locked(cur, eglGetCurrentSurface(EGL_DRAW), eglGetCurrentSurface(EGL_READ));
  mutexUnlock(&g_ctx_lock);
  return i;
}

int abs_eglw_create_shared(int parent) {
  mutexLock(&g_ctx_lock);
  if (parent <= 0 || parent >= g_nctx) {
    mutexUnlock(&g_ctx_lock);
    return 0;
  }
  EGLContext share = g_ctxs[parent].ctx;
  mutexUnlock(&g_ctx_lock);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext c = eglCreateContext(g_dpy, g_cfg, share, ctx_attrs);
  if (c == EGL_NO_CONTEXT) {
    debugPrintf("[egl] createSharedContext(%d) failed 0x%x\n", parent, eglGetError());
    return 0;
  }
  static const EGLint pb[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface s = eglCreatePbufferSurface(g_dpy, g_cfg, pb);
  if (s == EGL_NO_SURFACE)
    eglGetError(); /* none on this platform: surfaceless it is */
  mutexLock(&g_ctx_lock);
  int i = add_locked(c, s, s);
  mutexUnlock(&g_ctx_lock);
  debugPrintf("[egl] shared context %d (of %d)%s\n", i, parent,
              s == EGL_NO_SURFACE ? ", surfaceless" : ", 64x64 pbuffer");
  return i;
}

void abs_eglw_destroy_shared(int idx) {
  mutexLock(&g_ctx_lock);
  if (idx <= 1 || idx >= g_nctx) {
    mutexUnlock(&g_ctx_lock);
    return;
  }
  CtxEntry e = g_ctxs[idx];
  g_ctxs[idx] = (CtxEntry){EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE};
  mutexUnlock(&g_ctx_lock);
  if (e.draw != EGL_NO_SURFACE)
    eglDestroySurface(g_dpy, e.draw);
  if (e.ctx != EGL_NO_CONTEXT)
    eglDestroyContext(g_dpy, e.ctx);
}

int abs_eglw_register_thread(int idx) {
  mutexLock(&g_ctx_lock);
  CtxEntry e = idx > 0 && idx < g_nctx ? g_ctxs[idx] : (CtxEntry){EGL_NO_CONTEXT, 0, 0};
  mutexUnlock(&g_ctx_lock);
  if (e.ctx == EGL_NO_CONTEXT)
    return 0;
  EGLBoolean ok = eglMakeCurrent(g_dpy, e.draw, e.read, e.ctx);
  if (!ok)
    debugPrintf("[egl] registerThread(%d) failed 0x%x\n", idx, eglGetError());
  return ok ? 1 : 0;
}

void abs_eglw_unregister_thread(void) { eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); }

#else /* the null renderer: every call succeeds, nothing is drawn */
int abs_egl_init(void) { return 0; }
void abs_egl_swap(void) {}
void abs_egl_release_current(void) {}
void abs_egl_make_current_main(void) {}
void abs_eglw_init(void) {}
int abs_eglw_current(void) { return 1; }
int abs_eglw_create_shared(int parent) { return 2; }
void abs_eglw_destroy_shared(int idx) {}
int abs_eglw_register_thread(int idx) { return 1; }
void abs_eglw_unregister_thread(void) {}
#endif
