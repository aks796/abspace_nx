/* dcr_config.c -- Angry Birds Space's settings: config.ini's options, on the
 * runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * The options, their order, defaults and help text are the ones the port
 * wrote before the runtime, so players' config.ini files read the same.
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file gets the options a newer build adds, at the
 * end, so edits and comments survive updates. Read once at start-up: changes
 * apply the next time the game starts. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

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

static const CfgOpt k_opts[] = {
    {"game", "language", "auto",
     "auto (the console's language) or one of: en fr de it es pt ru nl sv da fi\n"
     "# nb pl tr ja ko zh (zh = simplified, zh_tw = traditional).",
     CFG_TEXT, NULL, NULL, 0, 0, 0, 0},
    {"game", "free_purchases", "true",
     "The shop is long closed: true makes everything in it free (Space Eagles,\n"
     "# power-ups, the paid episodes), granted the way a purchase was.",
     CFG_BOOL, NULL, &g_cfg.free_purchases, 0, 0, 0, 0},
    {"game", "planet_info", "true",
     "The tiny planets on the episode screen (NASA, the Mars rover, Facebook...)\n"
     "# and the Solar System's planets opened web pages. true: they open a popup\n"
     "# about their topic in the game instead, with its video: streamed from\n"
     "# YouTube when the console is online, or from the videos/ folder.",
     CFG_BOOL, NULL, &g_cfg.planet_info, 0, 0, 0, 0},
    {"game", "froot_loops", "true",
     "Froot Loops Bloopers, the Kellogg's episode (5 levels and 2 comics, a sign\n"
     "# by the Brass Hogs planet). The game showed it only where Rovio's server\n"
     "# offered it (the US, during the promotion). true: always shown.",
     CFG_BOOL, NULL, &g_cfg.froot_loops, 0, 0, 0, 0},
    CFG_ROW_SWAP_AB("Swap A and B (true: B launches and selects, A goes back).", &g_cfg.swap_ab),
    {"controls", "aim", "pull",
     "pull: the left stick pulls the bird back the way it points, as in Angry\n"
     "# Birds Trilogy (push the stick left to fire right). push: the stick points\n"
     "# the way the bird will fly.",
     CFG_CHOICE, "pull,push", &g_cfg.aim, 0, 0, 0, 0},
    {"controls", "fine_aim_speed", "1.0",
     "How far one D-pad press (or ZL held with the stick) moves the aim while a\n"
     "# bird is pulled back: 0.25 to 4.",
     CFG_FLOAT, NULL, &g_cfg.aim_speed, 0.25f, 4.0f, 0, 0},
    {"controls", "pointer_speed", "14",
     "Speed of the hand cursor (cursor controls): pixels per frame at full\n"
     "# stick, 4 to 40. D-pad up/down changes it while playing (saved in\n"
     "# pointer.cfg, which then wins over this).",
     CFG_FLOAT, NULL, &g_cfg.pointer_speed, 4.0f, 40.0f, 0, 0},
    {"controls", "menu_focus", "true",
     "Menus: the D-pad and the left stick jump between the buttons on screen and\n"
     "# A presses the highlighted one. false: the hand cursor in menus instead.",
     CFG_BOOL, NULL, &g_cfg.menu_focus, 0, 0, 0, 0},
    {"controls", "scheme", "console",
     "The controls at start. console: Angry Birds Trilogy's (aim with the stick,\n"
     "# highlighted buttons). cursor: Angry Birds Reloaded's hand cursor (tap and\n"
     "# drag with A, ZL or ZR). R switches between them while playing.",
     CFG_CHOICE, "console,cursor", &g_cfg.scheme, 0, 0, 0, 0},
    {"controls", "cursor_size", "1.0",
     "Size of the hand cursor (Orbital Escapade's hand), 0.5 to 3.",
     CFG_FLOAT, NULL, &g_cfg.cursor_size, 0.5f, 3.0f, 0, 0},
    CFG_ROW_RESOLUTION("720",
                       "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
                       "# starts). The Switch scales the picture to the screen either way; the\n"
                       "# game's art is made for 1024x768."),
    CFG_ROW_BOOST("CPU at 1785 MHz while the game starts (until its first picture) and\n"
                  "# inside loading frames (those over 50 ms), normal otherwise.",
                  &g_cfg.boost),
    {"performance", "texture_memory_mb", "200",
     "Memory the game may keep its pictures in, in MB (16 to 1024; 0 = the\n"
     "# 60 MB it allows itself on Android, which makes it reload them often).",
     CFG_TEXT, NULL, NULL, 0, 0, 0, 0},
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch. Off: the log appears only\n"
                     "# while something is being set up (first launch, a new APK or NRO).",
                     &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (slow; for bug reports).",
                    &g_cfg.log_jni),
    {"debug", "lua_bridge", "true",
     "The controller script that runs inside the game (aiming, camera, menu\n"
     "# buttons). false: touch and the free pointer only.",
     CFG_BOOL, NULL, &g_cfg.lua_bridge, 0, 0, 0, 0},
    {"debug", "log_lua", "false", "Log the controller script's view of the game once a second.",
     CFG_BOOL, NULL, &g_cfg.log_lua, 0, 0, 0, 0},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config();
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;

  const char *lang = rt_config_get("game", "language");
  g_cfg.locale[0] = 0;
  if (lang && strcasecmp(lang, "auto"))
    snprintf(g_cfg.locale, sizeof g_cfg.locale, "%s", lang);

  /* 0 (the game's own limit) or 16..1024 */
  const char *tex = rt_config_get("performance", "texture_memory_mb");
  int mb = tex ? atoi(tex) : 200;
  if (mb != 0 && (mb < 16 || mb > 1024)) {
    debugPrintf("[config] texture_memory_mb = %s: not 0 or 16..1024, using 200\n", tex ? tex : "");
    mb = 200;
  }
  g_cfg.tex_mb = mb;

  const char *r = rt_config_get("display", "resolution");
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] language %s; %dx%d (%s, %s); A/B %s; aim %s, fine aim %.2f, pointer %.0f; "
              "menu focus %s; controls %s; free shop %s; planet pages %s; Froot Loops %s; textures %d MB; CPU boost %s; "
              "Lua bridge %s\n",
              g_cfg.locale[0] ? g_cfg.locale : "auto", g_cfg.res_w, g_cfg.res_h, r ? r : "720",
              docked ? "docked" : "handheld", g_cfg.swap_ab ? "swapped" : "normal",
              g_cfg.aim == ABS_AIM_PULL ? "pull" : "push", (double)g_cfg.aim_speed,
              (double)g_cfg.pointer_speed, g_cfg.menu_focus ? "on" : "off",
              g_cfg.scheme == ABS_SCHEME_CURSOR ? "cursor" : "console", g_cfg.free_purchases ? "on" : "off",
              g_cfg.planet_info ? "on" : "off", g_cfg.froot_loops ? "on" : "off", g_cfg.tex_mb, g_cfg.boost ? "on" : "off",
              g_cfg.lua_bridge ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
