/* abs_main.c -- Angry Birds Space's part of the boot (the runtime's main.c
 * runs the rest: runtime/source/main.c).
 *
 * port_load() goes from the APK to the game's first code: the library and
 * classes.txt unpacked when needed, the APK's assets indexed, the library
 * loaded and bound (abs_loader.c, which also installs the Lua bridge).
 * port_run() runs the engine's constructors, then JNI_OnLoad and the activity
 * (abs_boot.c). MIT.
 */
#include <stdio.h>

#include "abs.h"
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"

const char *port_apk_help(void) {
  return "Copy the APK of your own Angry Birds Space HD (com.rovio.angrybirdsspaceHD,\n"
         "2.2.14, armeabi-v7a) into that folder, under any name ending in .apk:\n"
         "the game reads its data from it, and its library is unpacked from it\n"
         "on the first launch.";
}

/* The runtime's default would run the shared audout test; abs_audio.c has
 * its own output (it pulls the engine's mixer and resamples), tested the
 * same way. */
void port_audio_selftest(void) { abs_audio_selftest(); }

int port_load(const char *apk) {
  /* libAngryBirdsSpace.so and classes.txt, from the APK when they are
   * missing or it has changed (abs_setup_plan.c) */
  dcr_setup_from_apk(apk);
  if (abs_assets_init(apk) != 0)
    fatal_error("Could not read the game's files from %s.\n\n"
                "Is it the APK of Angry Birds Space HD (it must hold assets/data/...)?",
                apk);
  if (abs_load_module() != 0)
    fatal_error("Could not load the game library from %s.\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n"
                ABS_LIB_GAME " and .setup there to unpack it again.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary: the engine's constructors, then (abs_boot.c)
 * JNI_OnLoad and the activity. */
void port_run(void) {
  abs_run_constructors();
  abs_boot_run();
}
