/* abs.h -- the Angry Birds Space side of the port: the pieces that stand in
 * for the game's Java activity (com.rovio.fusion.App and its GLSurfaceView),
 * talking to each other. MIT. */
#ifndef ABS_H
#define ABS_H
#include <stddef.h>
#include <stdint.h>
#include "jni.h"
#include "so_util.h"

/* ---------------------------------------------------------- abs_loader.c */
extern so_module g_mod_game;
int abs_load_module(void);
void abs_run_constructors(void);
/* A JNI export of the engine ("Java_com_rovio_..."), NULL if absent. */
void *abs_native(const char *symbol);

/* ------------------------------------------------------------ abs_java.c */
/* The Java objects the engine is handed. */
extern JObj *g_activity, *g_asset_manager, *g_class_loader, *g_surface_view;
void abs_java_init(void);
const char *abs_locale(void);            /* "en_US": the game's language */
JObj *abs_list_new(void);                /* java.util.ArrayList */
void abs_list_add(JObj *list, JObj *item);

/* ---------------------------------------------------------- abs_assets.c */
/* The APK's assets/ as an AAssetManager (AAssetManager_open & co). */
int abs_assets_init(const char *apk_path);
/* Whole file of assets/<name> into a new malloc()ed buffer (NULL if absent). */
void *abs_asset_read(const char *name, size_t *len);
/* each file under assets/ whose path (without "assets/") starts with prefix */
void abs_assets_each(const char *prefix, void (*cb)(const char *name, void *ud), void *ud);

/* ---------------------------------------------------------- abs_orbital.c */
/* Orbital Escapade's two worlds, from the player's copy in <root>/orbital/data */
int abs_orbital_present(void);
enum { ABS_CURSOR_MENU, ABS_CURSOR_LEVEL, ABS_CURSOR_POINT }; /* abs_cursor_style: which hand */
void *abs_orbital_read(const char *name, size_t *len); /* data/levels/oe_*, data/images/oe/, data/audio/oe/ */
uint8_t *abs_orbital_lua_plain(const char *rel, size_t *len); /* a mod Lua file, decrypted */
uint8_t *abs_orbital_wrap_lua(const uint8_t *plain, size_t n, size_t *out_len);
char *abs_orbital_sheets_for(const char *names); /* malloc()ed "OE_A.dat,OE_B.dat" */
char *abs_orbital_game_sheets_for(const char *names); /* the game's own: "A.dat=NAME|NAME,..." */
char *abs_orbital_parts(const char *names); /* the sprites of the named composites: "A,B" */
char *abs_orbital_quoted(const char *files); /* the quoted strings of mod scripts, one per line */
int abs_orbital_group_composites(const char *arg); /* "GROUP|NAME,NAME": the composites a group serves */
char *abs_orbital_sprite_size(const char *name); /* "w,h" or "" */
char *abs_orbital_full_sheets_for(const char *names); /* "OEF_A.dat,...": whole, the game's names as OE_ */
char *abs_orbital_prune(const char *keep, int wait); /* remove from orbital/data what the worlds do not use */
int abs_orbital_trimmed(void); /* orbital/data holds only what the worlds use (or no mod) */

/* ------------------------------------------------------------- abs_egl.c */
/* EGL the way GLSurfaceView sets it up (ES 2, RGBA8888, depth, stencil) on
 * the default window; current on the calling thread. */
int abs_egl_init(void);
void abs_egl_swap(void);
void abs_egl_release_current(void);
void abs_egl_make_current_main(void);
/* com.rovio.fusion.EGLWrapper (abs_java.c calls these). */
void abs_eglw_init(void);
int abs_eglw_current(void);
int abs_eglw_create_shared(int parent);
void abs_eglw_destroy_shared(int idx);
int abs_eglw_register_thread(int idx);
void abs_eglw_unregister_thread(void);

/* ----------------------------------------------------------- abs_audio.c */
/* com.rovio.fusion.AudioOutput: the engine's mixer, pulled by us. */
void abs_audio_new(JObj *obj, int64_t handle, int rate, int channels, int bits, int bufsize);
int abs_audio_start(JObj *obj);
void abs_audio_stop(JObj *obj);
void abs_audio_pause(int paused); /* background: no pulls, silence out */
uint32_t abs_audio_mixes(void);
void abs_audio_selftest(void);

/* ------------------------------------------------------------ abs_boot.c */
int abs_boot_run(void);
void abs_request_exit(void);           /* App.quitRequested / the game quitting */
int abs_surface_w(void);
int abs_surface_h(void);
/* Globals.runOnGLThread / runOnAppThread: queued work for the engine's
 * update thread (it runs before the next nativeUpdate). */
void abs_post_update(void (*fn)(void *), void *arg);
int abs_is_multithreaded(void);

/* ----------------------------------------------------------- abs_input.c */
void abs_input_init(void);
/* On the update thread, right before each nativeUpdate: controllers, touch
 * and the pointer, turned into the engine's touch and key events. */
void abs_input_update(void);
/* On the render thread, before each present: the focus ring, the pointer. */
void abs_input_draw(void);
/* The engine's touch entry points (MyInputHandler.nativeInput & co). */
void abs_touch(int action, float x, float y, int id);
void abs_key(int keycode, int down);
/* A link's popup closed (the script says so): back to the focus ring. */
void abs_input_popup_closed(void);

/* ------------------------------------------------------------- abs_lua.c */
/* The controller script inside the game's Lua (see abs_lua.c). */
enum {
  ABS_MODE_NONE = 0,  /* no game state seen (yet) */
  ABS_MODE_MENU,      /* menus: buttons to focus */
  ABS_MODE_AIM,       /* a level, a bird on the slingshot waiting for us */
  ABS_MODE_FLIGHT,    /* a level, a bird in the air (A: its power, if unused) */
  ABS_MODE_WAIT,      /* a level, nothing to aim (camera, birds hopping...) */
  ABS_MODE_PAUSED,    /* the pause page */
  ABS_MODE_ENDED,     /* the level's end screens */
};
#define ABS_MAX_BUTTONS 48
/* What a menu item is (abs_ctl.lua's kinds). */
enum {
  ABS_ITEM_BUTTON = 0, /* a button of the UI */
  ABS_ITEM_SIDE,       /* the planet carousel: a planet beside the middle one */
  ABS_ITEM_CENTRE,     /* ... the planet in the middle */
  ABS_ITEM_LINK,       /* ... one of its tiny planets (NASA, Facebook...) */
};
typedef struct {
  float x, y, w, h;   /* screen pixels of the game's surface: its box */
  float ax, ay;       /* where to touch it */
  int kind;           /* ABS_ITEM_* */
  int round;          /* round (ring: a circle) or boxy (a rounded rectangle) */
  int prio;           /* how good a first focus it is (the popup's check: 9) */
  int id;             /* the same item across updates (the planet in the middle: 1) */
  int grp;            /* the main menu's tabs: 2 left, 4 right, +1 the tab's own button */
  int vis;            /* on the screen (0: on another page of a scrolling list) */
} AbsItem;
typedef struct {
  int valid;             /* the script ran in the game's state this frame */
  int mode;              /* ABS_MODE_* */
  float scr_w, scr_h;    /* the game's screen (Lua screenWidth/Height) */
  float bird_x, bird_y;  /* the bird to grab, screen */
  float sling_x, sling_y;/* the slingshot's rest point, screen */
  float pull;            /* full pull, screen pixels */
  int special;           /* the flying bird's power is still unused */
  int aiming;            /* a bird is held (touch or ours) */
  char sig[24];          /* the screen or popup on top: changes with it */
  int carousel;          /* the planet carousel is on screen (2: turning) */
  int mm;                /* the main menu: 1 on screen, +2 its left tab open, +4 its right one */
  int nbuttons;
  AbsItem buttons[ABS_MAX_BUTTONS];
  float tap_x, tap_y;    /* a HUD button to press (restart, eagle) ... */
  uint32_t tap_seq;      /* ... new when this changes */
  uint32_t frame;        /* bumps every time the script reports */
} AbsLuaState;

void abs_lua_install(void);          /* before the engine's Lua exists */
int abs_lua_active(void);            /* the bridge found the game's state */
void abs_lua_snapshot(AbsLuaState *out);
/* Requests for the script's next run (update thread). */
enum {
  ABS_CMD_CAMERA_SLING = 1,  /* the slingshot view */
  ABS_CMD_CAMERA_CASTLE,     /* the target view */
  ABS_CMD_PAUSE,             /* togglePausePage() */
  ABS_CMD_RESTART,           /* the level's restart */
  ABS_CMD_EAGLE,             /* the Space Eagle (if the level offers it) */
  ABS_CMD_PAGE_NEXT,         /* the scrolling page on screen: next ... */
  ABS_CMD_PAGE_PREV,         /* ... previous */
  ABS_CMD_SPIN_LEFT,         /* the planet carousel: the left planet to the middle */
  ABS_CMD_SPIN_RIGHT,        /* ... the right one */
  ABS_CMD_POWERUPS,          /* a level's power-ups bar (bottom left): open / fold it */
};
void abs_lua_command(int cmd);
#define ABS_CMD_ARG(cmd, n) ((cmd) + (n) * 256) /* a command with a number (abs_ctl.lua A.frame) */
/* the camera, per frame: the right stick (x, y; up positive) and the zoom
 * buttons (+1 in, -1 out) */
void abs_lua_set_analog(float x, float y, float zoom);
void abs_lua_frame_tick(void);                  /* once per update, before nativeUpdate */
void abs_lua_set_focus(int id);                 /* the focused item: brought into view */

/* ---------------------------------------------------------- abs_overlay.c */
/* Drawing over the game's frame (GLES2, the engine's state kept). */
int abs_ov_begin(int w, int h);
void abs_ov_rect(float x, float y, float w, float h, uint32_t rgba);
void abs_ov_tri(const float *xy, uint32_t rgba); /* xy: three points */
void abs_ov_frame(float x, float y, float w, float h, float t, uint32_t rgba);
/* A ring of thickness t just outside the box: a rounded rectangle with corner
 * radius r, or (r < 0) the ellipse the box holds. */
void abs_ov_ring(float x, float y, float w, float h, float r, float t, uint32_t rgba);
/* A filled rounded rectangle. */
void abs_ov_rrect(float x, float y, float w, float h, float r, uint32_t rgba);
/* Pictures: an RGBA8 image made into a texture (render thread; 0 = failed),
 * drawn tinted by rgba (0xffffffff = as it is). */
unsigned abs_ov_texture(const uint8_t *rgba, int w, int h);
void abs_ov_image(unsigned tex, float x, float y, float w, float h, uint32_t rgba);
/* YUV 4:2:0 planes (video frames): three textures kept by the overlay;
 * planes NULL draws the picture uploaded last. */
void abs_ov_yuv(const uint8_t *const planes[3], const int strides[3], int w, int h, float x, float y,
                float dw, float dh);
void abs_ov_text(float x, float y, float px, uint32_t rgba, const char *utf8);
float abs_ov_text_width(float px, const char *utf8);
void abs_ov_end(void);
/* The Switch's shared font (pl), rasterised by abs_font.c. */
int abs_font_init(void);
int abs_font_ready(void);

/* PNG files (the pointer's pictures): RGBA8, malloc()ed; NULL if not a PNG
 * this can read. */
uint8_t *abs_png_decode(const uint8_t *data, size_t len, int *w, int *h);

/* ----------------------------------------------------------- abs_dialog.c */
/* com.rovio.fusion.AlertDialogWrapper: shown by the port over the game. */
void abs_dialog_show(JObj *obj);
void abs_dialog_dismiss(JObj *obj);
int abs_dialog_active(void);
/* Controller/touch for an open dialog (render thread input, abs_input.c). */
void abs_dialog_input(uint64_t down, int touch, float tx, float ty);
void abs_dialog_draw(void);

/* ----------------------------------------------------------- abs_extras.c */
/* The port's pages about the tiny planets' links (the game would open a web
 * page: NASA, the Mars rovers, Facebook...), and their videos, if the player
 * put them on the SD card. */
void abs_extras_init(void);
/* The topics with a video on the SD card ("nasa curiosity ..."), for the
 * game's popups (abs_ctl.lua). */
const char *abs_extras_videos(void);
/* The port's own page (when the game's popup could not be made, or a URL
 * reached Launcher.openURL): url.lua's name ("NASA_URL") or the URL's t=. */
void abs_extras_open(const char *link);
int abs_extras_active(void);
void abs_extras_input(uint64_t down, uint64_t held, float lsx, float lsy, int touch, float tx, float ty);
void abs_extras_draw(void);

/* ------------------------------------------------------------ abs_video.c */
/* The links' videos: streamed from YouTube, or from the SD card, played in
 * the rectangle the game's popup leaves for them (full screen without one).
 * The game keeps running underneath, silent. */
enum {
  ABS_VS_IDLE = 0,
  ABS_VS_CONNECTING,       /* resolving the video, starting the download */
  ABS_VS_BUFFERING,        /* reading its start */
  ABS_VS_PLAYING,
  ABS_VS_PAUSED,
  ABS_VS_ENDED,            /* until the popup is done with it (abs_video_stop) */
  ABS_VS_ERR_OFFLINE = 61, /* the console is not on the internet */
  ABS_VS_ERR_UNAVAILABLE,  /* YouTube would not give it (or no such file) */
  ABS_VS_ERR_NETWORK,      /* the download broke off */
  ABS_VS_ERR_DECODE,       /* not a video this can play */
};
int abs_video_available(void);            /* built with the decoder */
int abs_video_play(const char *path);     /* an SD card file: starts it */
int abs_video_play_youtube(const char *id); /* a YouTube video (its id): starts it */
int abs_video_state(void);                /* ABS_VS_* */
int abs_video_active(void);               /* connecting .. paused: it takes the controller */
void abs_video_stop(void);                /* any thread */
void abs_video_set_rect(float x, float y, float w, float h); /* the popup's (w <= 0: full screen) */
/* A pause, B stop, left stick / D-pad left and right: 10 s back / on */
void abs_video_input(uint64_t down, uint64_t held, float lsx);
void abs_video_draw(void);                /* render thread, before the present */
/* Audio: the video's samples instead of the game's (abs_audio.c asks). */
int abs_video_mix(int16_t *out, int frames, int rate);

#endif
