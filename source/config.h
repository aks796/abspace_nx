/* config.h -- Angry Birds Space's own constants. The runtime's settings
 * (folder, package, screen size, the library's region) are in port_config.h
 * (runtime/source/rt_settings.h).
 *
 * Angry Birds Space HD (com.rovio.angrybirdsspaceHD 2.2.14, versionCode
 * 221400), armeabi-v7a: Rovio's own "Fusion" C++ engine (Lua 5.1 + Box2D,
 * GLES2), one native library. MIT.
 */
#ifndef ABS_CONFIG_H
#define ABS_CONFIG_H

#include "rt_settings.h"

/* The game's library: libAngryBirdsSpace.so 2.2.14's highest
 * p_vaddr+p_memsz is 0x740cb0 (~7.3 MB), in PORT_SO_REGION_BYTES (32 MB). */
#define ABS_LIB_GAME     "libAngryBirdsSpace.so"
#define ABS_VERSION      "2.2.14"
#define ABS_VERSION_CODE 221400

#endif /* ABS_CONFIG_H */
