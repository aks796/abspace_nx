/* config.h -- build-wide constants for the Angry Birds Space Switch wrapper.
 *
 * Angry Birds Space HD (com.rovio.angrybirdsspaceHD 2.2.14, versionCode
 * 221400), armeabi-v7a: Rovio's own "Fusion" C++ engine (Lua 5.1 + Box2D,
 * GLES2), one native library. AArch32 host process built against libnx32
 * (see the Makefile). MIT.
 */
#ifndef DCR_CONFIG_H
#define DCR_CONFIG_H

/* Where on the SD card the game files live (up to build 202609280910 it was
 * DCR_OLD_ROOT_PATH: dcr_migrate.c moves an older install over), and the
 * module name. The player's APK there may have any name (dcr_apkfind.h). */
#define DCR_ROOT_PATH     "/switch/abspace_nx"
#define DCR_OLD_ROOT_PATH "/switch/abspace"
#define ABS_LIB_GAME      "libAngryBirdsSpace.so"
#define DCR_PACKAGE     "com.rovio.angrybirdsspaceHD"
#define ABS_VERSION     "2.2.14"
#define ABS_VERSION_CODE 221400

/* The reserved region the game module is mapped into. libAngryBirdsSpace.so
 * 2.2.14: highest p_vaddr+p_memsz = 0x740cb0 (~7.3 MB). A module larger than
 * this is refused by so_load (-3), reported by name. */
#define SO_REGION_BYTES (32u * 1024 * 1024)

/* Left outside the heap for kernel-side allocations. GPU buffers come from
 * the heap (libdrm_nouveau memaligns them and hands them to nvmap). */
#define GFX_RESERVE_MB  16u

/* Default render size (config.ini [display] resolution changes it). The
 * engine lays its screen out for any size; its art is made for 1024x768. */
#define DCR_FORCE_SCREEN_W 1280
#define DCR_FORCE_SCREEN_H 720

#define DEBUG_LOG 1

/* The renderer: 1 = mesa/nouveau (gl_mesa.c, portlibs32/ from mesa32),
 * 0 = null GL (gl_null.c: runs the game, draws nothing). Set by the
 * Makefile. */
#ifndef DCR_GL_MESA
#define DCR_GL_MESA 0
#endif

#endif /* DCR_CONFIG_H */
