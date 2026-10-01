/* port_config.h -- Angry Birds Space's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Angry Birds Space"
#define PORT_NAME     "abspace_nx"
#define PORT_PACKAGE  "com.rovio.angrybirdsspaceHD"
#define PORT_BANNER   "abspace_nx: Angry Birds Space (Rovio Fusion engine, armeabi-v7a)"
/* up to build 202609280910 the folder was /switch/abspace */
#define PORT_OLD_ROOT_PATHS "/switch/abspace"

/* The APK, by what is in it (any name): it holds the game's library; of
 * several, the one that is this game at the version the port is made for. */
#define PORT_APK_DESC "Angry Birds Space HD 2.2.14 (com.rovio.angrybirdsspaceHD, armeabi-v7a)"
#define PORT_APK_ROLES                                                                        \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi-v7a/libAngryBirdsSpace.so", NULL}, \
   .package = "com.rovio.angrybirdsspaceHD", .version_code = 221400, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's library from the APK)"

/* ------------------------------------------------------------------ libc */
#define RT_PROC_COMM "com.rovio.angry" /* /proc/self/stat's comm, as before */

/* ------------------------------------------------------------------ files */
/* the new launcher NRO in the old folder (how players update) comes along */
#define RT_MIGRATE_MOVE_NEWER_NRO 1

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 1

#endif
