/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

/* [controls] aim: which way the left stick moves the bird in the slingshot. */
enum { ABS_AIM_PULL, /* the bird goes where the stick points (Angry Birds Trilogy) */
       ABS_AIM_PUSH  /* the stick points where the bird will fly */ };

/* [controls] scheme: how the controller plays at start (R switches). */
enum { ABS_SCHEME_CONSOLE, /* Angry Birds Trilogy: aim with the stick, buttons highlighted */
       ABS_SCHEME_CURSOR   /* Angry Birds Reloaded: a hand cursor you tap and drag with */ };

typedef struct {
  char locale[8];        /* [game] language: "en_US"... ("" = the console's language) */
  int swap_ab;           /* [controls] swap_a_b */
  int aim;               /* [controls] aim: ABS_AIM_* */
  float aim_speed;       /* [controls] fine_aim_speed: D-pad / ZL precision nudge */
  float pointer_speed;   /* [controls] pointer_speed: px per frame at full stick */
  int menu_focus;        /* [controls] menu_focus: D-pad / stick jump between buttons */
  int scheme;            /* [controls] scheme: ABS_SCHEME_* at start */
  float cursor_size;     /* [controls] cursor_size: the hand cursor's size */
  int free_purchases;    /* [game] free_purchases: the shop gives everything for free */
  int planet_info;       /* [game] planet_info: the tiny planets open the port's pages */
  int froot_loops;       /* [game] froot_loops: the Froot Loops Bloopers episode is shown */
  int tex_mb;            /* [performance] texture_memory_mb (0: the game's own limit) */
  int res_w, res_h;      /* [display] resolution */
  int boost;             /* [performance] boost_cpu_when_loading */
  int gl_selftest;       /* [debug] gl_selftest */
  int boot_log;          /* [debug] boot_log_on_screen */
  int log_jni;           /* [debug] log_java_calls */
  int lua_bridge;        /* [debug] lua_bridge: the controller script inside the game's Lua */
  int log_lua;           /* [debug] log_lua: its per-second state line */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
