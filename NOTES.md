# abspace_nx notes

abspace_nx runs the Android build of Angry Birds Space HD 2.2.14 (`com.rovio.angrybirdsspaceHD`,
armeabi-v7a) on a Switch with Atmosphère. The whole process is 32-bit (AArch32). It loads the
game's own `libAngryBirdsSpace.so` and supplies the bionic, JNI, Android, EGL/GLES and audio
services that library expects. It ships no game code or data.

Below: what the 32-bit libraries need fixed, what was specific to this game, and advice for other
32-bit ports. Paths are relative to this repository unless they name a library file. "Hardware"
means runs on a Switch with Atmosphère (September 2026, from `debug.log`).

## Library updates needed

### Status (2026-09-30)

The port is built against the libnx32 fork at `41b61f92` (libnx 4.12.0, switchbrew master
`9b4f3b29` merged; [github.com/aks796/libnx32](https://github.com/aks796/libnx32)) and mesa32,
devkitPro's Mesa with the 32-bit fixes on top
([github.com/aks796/mesa32](https://github.com/aks796/mesa32)). Commit `c6c53d20` fixed most of
the libnx32 entries below. The port keeps its own hardware-proven code for each of them; its
overrides still take precedence at link time (checked in the link map).

| Entry | Library status | This port |
| --- | --- | --- |
| Short enums in IPC data | Fixed, `cb01ef9f` | uses the fork |
| `svcSetThreadCoreMask` u32 mask | Fixed, `c6c53d20` (u64) | keeps `dcr_thread_set_cores` |
| `svcGetThreadCoreMask` stack | Fixed, `c6c53d20` | keeps `dcr_thread_get_cores` |
| `svcWaitForAddress` / `svcSignalToAddress` | Added, `c6c53d20` (int32 value in r2) | keeps its own, with the self-test |
| `svcGetThreadContext3` | Added, `c6c53d20` | keeps its asm in `watchdog.c` |
| AArch32 exception entry | Added, `c6c53d20` (weak; optional `__libnx_exception_handler32`) | keeps `exc32.S` |
| `armICacheInvalidate` | Implemented, `c6c53d20` (R/RX flip) | keeps `code_flush.c` |
| audout/audin descriptor, u64 tags | Fixed, `c6c53d20` | keeps its own IPC in `abs_audio.c` |
| virtmem bounds and search region | Fixed, `c6c53d20` | keeps `nx32_virtmem.c` |
| `__libnx_initheap` | Fixed, `c6c53d20` (clamped, retries) | keeps `nx_init.c` |
| Default window's display | `nwindowGetDefaultDisplay()`, `c6c53d20` | keeps its `nwindowGetDefault` override |
| Own process handle | `envAcquireOwnProcessHandle()`, `c6c53d20` | keeps `selfproc.c` |
| fsdev open-for-writing error | Mapped to EBUSY, `c6c53d20` | keeps routing `stat`/`truncate` through the open handle |
| `timespec_get` | Weak definition, `c6c53d20` | its own in `host_compat.c` wins |
| `.rel.dyn` in `switch32.ld` | Fixed, `c6c53d20` | keeps `dcr32.ld` |
| `-z text` / non-PIC target libraries, `__nx_dynamic` | Open | `crt0_reloc.c` |
| Soft-float newlib libm, `setjmp` without d8-d15 | Open | `bionic_math.c`, `bionic_setjmp.S` |
| `__appInit` aborting on a failed service | Open | `nx_init.c` |
| miniz greedy `inflate` | Open | `bionic_zlib.c` |
| Mesa `eglQuerySurface` size | Fixed in mesa32, `4e41d89f` | `b_eglQuerySurface` kept (now optional) |
| Mesa pbuffers; console and EGL buffer slots | Open | window surfaces only; console retired before EGL |
| FFmpeg h264 without hevc | Open (library packaging) | stub in `abs_video.c` |

One change in behaviour: with the core mask fixed, libnx's `pthread_create` (newlib's, through
`libsysbase`) now starts threads on 32-bit, where it used to fail. This port's game threads use its
own bionic `pthread_create` (`bionic_pthread.c`), so they are unaffected. In the linked Mesa only
`u_queue` creates threads (plus the ddebug and rbug drivers, which run only when enabled), so
Mesa's work queues can now start their threads.

### The entries

Each entry gives the problem, what this port does, and what should change upstream. The libraries:
devkitARM and libnx32 from the vita2hos image (`ghcr.io/vita2hos/devcontainer/vita2hos`; its
libnx32 is vita2hos/libnx at 721c977, "AArch32 support"); the patched libnx32 fork (branch
`master`, at cb01ef9f when these entries were written; now `41b61f92`, see the status above;
installed into `prefix/` by its `build.sh`);
Mesa 20.1.0-rc3 and libdrm_nouveau 1.0.1 (mesa32's `build.sh` installs into its `prefix/`,
copied into `portlibs32/`); FFmpeg 7.1.1 (`tools/ffmpeg/`) and mbedTLS 3.6.2 (`tools/mbedtls/`),
which install into `portlibs32/`.

### Toolchain (devkitARM in the vita2hos image)

- **Enums are short.** arm-none-eabi GCC uses `-fshort-enums` by default (the ARM EABI rule).
  Horizon's services use the 64-bit layout, where an enum is 4 bytes. Any struct field or raw IPC
  argument of an enum type that reaches the system is wrong. This causes the libnx32, Mesa and
  FFmpeg entries below. Port: the patched libnx32; a Mesa fix (mesa32 `099a02a3`); FFmpeg and
  `source/abs_video.c` built with `-fno-short-enums` (both enum sizes are linked, hence
  `-Wl,--no-enum-size-warning`).
  Upstream: pick one rule and document it: short enums with fixed-width types wherever data
  crosses IPC, shared memory or applet storage (the fork's approach), or `-fno-short-enums` for
  libnx32 and every portlib.
- **`switch32.specs` links with `-z text`.** The prebuilt devkitARM target libraries (newlib
  libc/libm, libsysbase, libstdc++) are not built `-fPIC`. Their literal pools hold absolute
  addresses, so a PIE link has `R_ARM_RELATIVE` relocations in `.text` and `.rodata`, and
  `-z text` fails. Port: `dcr32.specs` uses `-z notext` and `source/crt0_reloc.c` applies the text
  relocations (see `__nx_dynamic` below); the Mesa and FFmpeg builds pass `-Wl,-z,notext` too.
  Upstream: build the target libraries with `-fPIC`.
- **`switch32.ld` does not place `.rel.dyn`.** ARM uses SHT_REL; the script lists only `.rela.*`,
  so `.rel.dyn` lands wherever orphan placement puts it. Port: `dcr32.ld` places it in rodata.
  Upstream: add `.rel.dyn` and `.rel.plt`.

### libnx32

- **IPC data sized by an enum.** Fixed in the fork, commit cb01ef9f ("AArch32: IPC data that
  depended on the size of an enum"):
  - `hidSetSupportedNpadIdType` (called by `padConfigureInput`) and
    `hidGetNpadOfHighestBatteryLevel` sent `HidNpadIdType` lists one byte per id, where hid reads
    u32 ids. Handheld Joy-Cons worked; wireless controllers could not connect.
  - Raw enum request data (1 byte where a u32 is read): `hidIsUsbFullKeyControllerConnected`,
    `mmuRequestFinalize`/`Get`, `ncmContentMetaDatabaseListApplication`, `setsysGetAudioVolume`,
    `setsysGetAudioOutputMode`, and the request structs of `nfcStartDetection` and
    `pcvGetPossibleClockRates`.
  - Structs the system reads or writes, enum fields now u32: swkbd arguments (`SwkbdArgCommon`,
    `SwkbdAppearArg`: every field after the type was 2 bytes early), web, pctl-auth and error-EULA
    applet arguments, `LibAppletInfo`/`AppletIdentityInfo`, audren mempool and circular-buffer sink
    info, hid Lark/Lucia npad states, btdrv event statuses, nifm network profile info, ns shell
    event info, psm battery charge info.
  - `serviceDispatchIn/InOut` and `tipcDispatchIn/InOut` now `static_assert` on raw data of an
    enum type smaller than a u32.
  - `tools32/check_short_enums.sh` compiles every header struct with both enum sizes and lists
    those whose layout differs. The ones left are client-side only: `SwkbdInline`, `Framebuffer`,
    `SfOutHandleAttrs`, `NvMap`, `TipcDispatchParams`, `SfDispatchParams`, `RingConUserCal`,
    `RingCon`, `AppletHolder`.
  - Port: `build.sh` mounts the fork over the image's libnx32; `source/abs_input.c` calls
    `padConfigureInput` unchanged. Upstream: merge the commit; rerun the script when headers change.
- **`svcSetThreadCoreMask` declares the mask `u32`** (`svc.h`; `svc32.s` `DEFINE_OUT00_SVC 0x0F`).
  The kernel reads a u64 mask from r2:r3. r3 held whatever the caller left there (the thread
  handle, after `mutexUnlock`), the kernel returned InvalidCoreId, and every thread stayed on
  core 0. Port: `dcr_thread_set_cores` (`source/dcr_sched.c`) issues `svc 0x0F` with the high word
  zeroed and reads the mask back. Upstream: declare the mask `u64`; AAPCS then passes it in r2:r3
  and the stub works unchanged.
- **The `svcGetThreadCoreMask` stub unbalances the stack** (`svc32.s`). It pushes `{r0, r1, r4}`
  (12 bytes), then runs `add sp, sp, #4; pop {r4}`: r4 comes back holding the saved r1 and sp is
  4 bytes low. Port: `dcr_thread_get_cores` (`source/dcr_sched.c`; handle in r2, out r1 = ideal
  core, r2:r3 = mask). Upstream: `add sp, sp, #8` before `pop {r4}`.
- **No `svcWaitForAddress` / `svcSignalToAddress` stubs.** `svc.h` declares them; `svc32.s` has
  only a comment.
  - Port: `arb_wait_if_equal` and `arb_signal` in `source/bionic_pthread.c` (condvars, futex).
    Two WaitForAddress layouts are possible: r0 address, r1 type, r2 value, r3:r4 timeout (int32
    value), or r2:r3 value, r4:r5 timeout (int64 value). `dcr_pthread_selftest` times a 30 ms wait
    and switches layout if it is wrong. On hardware and on Ryujinx 1.1.1098 only the int32 layout
    waited 30 ms. SignalToAddress: r0 address, r1 type, r2 value, r3 count (<= 0: all waiters).
  - Upstream: add both stubs, value in r2 and timeout in r3:r4.
- **No `svcGetThreadContext3` stub** (`svc32.s`: TODO). Port: `get_ctx` in `source/watchdog.c`
  issues `svc 0x33`. The kernel's `ThreadContext` is 0x320 bytes; for an AArch32 thread `r[0..14]`
  are r0-r14. The call returns with r1-r3 zeroed, so inline asm must list them as clobbers (the
  first watchdog report crashed on a pointer the compiler had kept in r3). Upstream: add the stub.
- **The exception entry is a TODO stub** (`source/runtime/exception32.s`): faults cannot be
  handled or reported inside the process.
  - Port: `source/exc32.S` replaces `__libnx_exception_entry`. Mesosphere saves r0-r7, sp, lr,
    pc, pstate, esr and far, re-enters the program at its entry point with r0 = type, r1 = info
    and a stack of under 448 bytes, and leaves r8-r12 and VFP/NEON untouched. The entry moves to a
    64 KiB static stack, saves r8-r12, d0-d31 and FPSCR, calls `dcr_exception_dispatch`
    (`source/exc_handler.c`) and returns with `svcReturnFromException`. The handler writes a report
    to `svcOutputDebugString` and `crash.log` (through the FS service, no newlib locks) and returns
    a failure, so the kernel ends the process and Atmosphère writes its own report.
  - Upstream: implement the entry this way; add an AArch32 `ExceptionInfo` type.
- **`armICacheInvalidate` is `(void)0`** (`arm/cache.h`, AArch32 branch). AArch32 EL0 has no cache
  maintenance instructions, so code written at run time can execute stale bytes.
  - Port: `source/code_flush.c`. The kernel invalidates every core's I-cache when a Code page gains
    or loses execute permission. A page mapped with `svcMapProcessCodeMemory` and never executed
    is flipped R/RX once per invalidation, after `svcFlushProcessDataCache` on the written range.
    `so_flush_caches` (`source/so_util.c`) uses it. Proven on hardware in the Crossy Road port.
    Upstream: implement it this way (it needs a real process handle), or document the no-op.
- **`AudioOutBuffer` has 32-bit pointers** (`audout.h`). audout's descriptor has u64 fields for
  every client (0x28 bytes). `audoutGetReleasedAudioOutBuffer` also passes a 4-byte out buffer
  (`sizeof(AudioOutBuffer*)`) for tags the service writes as u64. Port: `source/abs_audio.c`
  defines the descriptor with u64 fields and issues append (cmd 7; 3 before 3.0.0) and
  get-released (cmd 8; 5) itself; `abs_audio_selftest` checks that buffers come back. Upstream:
  u64 fields in `AudioOutBuffer` and a u64 tag array; check audin and audren too.
- **`virtmem.c` is wrong for 32-bit processes.** (1) Region ends are `start + size` in
  `uintptr_t`; the 32-bit ASLR region ends at 0x1_0000_0000, which wraps to 0. (2)
  `virtmemFindAslr` and `virtmemFindCodeMemory` search the whole ASLR region, but in a 32-bit
  address space the kernel accepts Shared, Code, AliasCode, SharedCode, GeneratedCode, Transfered
  and ThreadLocal mappings only in the code region [0x200000, 0x40000000), which svcGetInfo
  reports as the stack region. Seen as `MapSharedMemory` = InvalidCurrentMemory in
  `hidInitialize`. Port: `source/nx32_virtmem.c` defines every symbol of `virtmem.o` (so the
  library object is not linked), with u64 bounds and the stack (= code) region as the search
  region. Upstream: the same two changes.
- **`__libnx_initheap`** (`source/runtime/init.c`) stores TotalMemorySize - UsedMemorySize in a
  32-bit `size_t` and does not clamp it to the heap region, which is 1 GiB in a 32-bit address
  space. A failed `svcSetHeapSize` aborts before `main`. Port: `source/nx_init.c` clamps to
  `InfoType_HeapRegionSize`, keeps 16 MB (`GFX_RESERVE_MB`) out of the heap, rounds to 2 MiB and
  retries smaller (hardware: 972 MB of the 1024 MB region). Upstream: clamp to the region size.
- **`__nx_dynamic` cannot apply text relocations** (`source/runtime/dynamic.c` writes them in
  place). `.text` and `.rodata` are Code pages (RX, R). A Code page made writable with
  `svcSetProcessMemoryPermission` becomes CodeData and can never be executable again (a hardware
  boot died with svcBreak 0xDC03).
  - Port: `source/crt0_reloc.c` replaces `__nx_dynamic`. It gets a real handle to its own process
    (see Kernel facts), maps each kernel memory block of [image base, `__relro_start`) to a free
    range as an RW alias with `svcMapProcessMemory`, applies the relocations there and unmaps. One
    block per call is required: `.text` and `.rodata` together fail with InvalidCurrentMemory
    (0xD401). It runs before BSS, TLS and libnx exist, so it uses only raw SVCs, pc-relative code
    and hidden linker symbols; `dcr32.ld` keeps it and libnx32's crt0 alone on page 0, and the
    Makefile builds it with `-fno-builtin -fno-tree-loop-distribute-patterns`. Its raw
    `svcSetProcessMemoryPermission` (0x73) uses libnx32's layout: r0 handle, r2:r3 address, r1
    size low, r4 size high, r5 permission.
  - Upstream: unnecessary once the target libraries are PIC; otherwise adopt the alias method.
- **The default window's display handle is private** (`default_window.c`). vi allows one
  `OpenDisplay("Default")` per process (a second returns 2114-0009), and that handle gives the
  vsync event. Port: `source/nx_init.c` defines `nwindowGetDefault`, `__nx_win_init` and
  `__nx_win_exit` (same calls, same order) and keeps the handle. This port paces frames with
  `eglSwapInterval(1)`; the shared runtime's other ports use the handle. Upstream: a getter.

### newlib and the devkitARM target libraries

- **libm is soft-float.** Every double operation is an `__aeabi_d*` call: correct for softfp
  callers, only slow. Port: `source/bionic_math.c` does sqrt, fabsf, the VRINT family
  (floor/ceil/trunc/round/rint), fminf/fmaxf, lroundf and modf in VFP; transcendental functions
  pass through. Upstream: a softfp (`-mfloat-abi=softfp -mfpu=neon-fp-armv8`) multilib of newlib
  and libgcc for this target.
- **`setjmp`/`longjmp` do not save d8-d15**, which AAPCS makes callee-saved. Port:
  `source/bionic_setjmp.S` saves r4-r11, sp, lr, d8-d15 and FPSCR. Upstream: the softfp build.
- **`timespec_get` is declared but not implemented** (Mesa's `c11/threads.h` uses it). Port:
  `source/host_compat.c`; the Mesa build defines `HAVE_TIMESPEC_GET`. Upstream: implement it.
- **`access()` is unreliable over fsdev** (the code does not record how). Port: `b_access`
  (`source/bionic_io.c`) uses `stat`.
- **fsdev `stat()` opens the file.** Horizon lets one handle write a file at a time. On a file
  already open for writing, `stat` and `truncate` by name fail with fs result 2-0xE02, which libnx
  reports as EIO (hardware self-test: "sized by name alone fails (fs result 0xe02)"). Port:
  `source/bionic_io.c` tracks the paths of open files and routes `b_stat`/`b_truncate` through the
  open handle. Upstream: map 2-0xE02 to its own errno (for example EBUSY), not EIO.
- **`exit()` shuts libnx services down while other threads still run.** A thread then polled the
  touch screen after hid was gone, and libnx aborted (0x1159, hardware 2026-09-24). Port:
  `b_exit`/`b__exit` (`source/bionic_core.c`) flush the log and call `svcExitProcess`; static
  destructors never run. Upstream: nothing; multi-threaded ports should avoid newlib's `exit()`.
- **`tmpfile()` has no temporary directory.** `b_tmpfile` (`source/bionic_stdio.c`) uses the
  app's cache folder.
- **ABI differences from bionic** (not bugs; every shim converts; `source/bionic.h` pins them with
  static assertions): time_t and off_t 8 vs 4 bytes, timespec/timeval 16 vs 8, struct tm 36 vs 44,
  stat 96 vs 104, dirent 264 vs 280, pthread_mutex_t 16 vs 4, sem_t 12 vs 4, jmp_buf 160 vs 256,
  open flags (O_CREAT 0x200 vs 0x40), CLOCK_MONOTONIC 4 vs 1, errno values above 34. Also
  mbstate_t 8 vs 4 (`source/bionic_wchar.c`), LC_ALL 0 vs 6 (`source/bionic_core.c`) and bionic's
  84-byte FILE (`source/bionic_stdio.c`).

### Mesa and libdrm_nouveau (mesa32)

devkitPro ships these for AArch64 only. The 32-bit build is
[github.com/aks796/mesa32](https://github.com/aks796/mesa32), a fork of devkitPro's Mesa (branch
`switch-20.1.0-rc3`). Its `build.sh` builds libdrm_nouveau 1.0.1 and Mesa 20.1.0-rc3: devkitPro's
branch with fixes on top, among them `4dbba376` (glapi: build with Python 3.9+, the `getchildren()`
fix in `gl_XML.py` and `glX_XML.py`) and `099a02a3` (AArch32: don't depend on int-sized enums). The
port links `-lEGL -lGLESv2 -lglapi -ldrm_nouveau -lstdc++`.

- **`mesa_format` is 16 bits with short enums.** `_mesa_format_from_format_and_type` can return a
  `MESA_ARRAY_FORMAT` value (bit 31), which was truncated. The first unsized
  GL_RGBA/GL_UNSIGNED_BYTE upload crashed in `st_choose_matching_format` (hardware, in the
  Unity-based sister port). An enum-typed `opcode:10` bitfield in `tgsi_opcode_info` had the same
  issue.
  - Port: `099a02a3` keeps the value in `uint32_t` in `st_choose_matching_format` (st_format.c),
    `_mesa_tex_format_from_format_and_type` (glformats.c) and
    `_mesa_format_matches_format_and_type` (formats.c), makes the bitfield `unsigned`
    (tgsi_info.h) and fixes one prototype (tgsi_ureg.c). `texture_test` in `source/gl_mesa.c`
    checks both paths when `[debug] gl_selftest` is on.
  - Upstream: `uint32_t` for these values, or build with `-fno-short-enums`.
- **The Switch EGL platform has window surfaces only, no MSAA configs, and rejects unknown
  attributes.** Port: `b_eglCreatePbufferSurface` (`source/gl_mesa.c`) returns `EGL_NO_SURFACE`;
  `filter_attrs` forces `EGL_SURFACE_TYPE` to `EGL_WINDOW_BIT`, drops Android-only attributes and
  retries `eglChooseConfig` without samples. The game's shared contexts are made current without
  a surface (`EGL_KHR_surfaceless_context`, `source/abs_egl.c`), enough for texture uploads.
  Upstream: pbuffers, or document surfaceless contexts.
- **`eglQuerySurface(EGL_WIDTH/EGL_HEIGHT)` returns 0 for window surfaces.** The platform sizes
  the buffers from the NWindow but does not store the size. Port: `b_eglQuerySurface` answers with
  `nwindowGetDimensions`. Upstream: store the size in the surface.
- **Mesa registers 3 buffer slots on the NWindow; the libnx console uses 2.** Giving the default
  window back to the console after Mesa used it fails on the third console frame
  (`BadGfxDequeueBuffer`, 0x2B59; hardware 2026-09-23). Port: `log_console_close`
  (`source/util.c`) retires the console for good before EGL takes the window. Upstream: release
  the queue's buffer slots when an EGL window surface or a console framebuffer is destroyed.
- GPU memory comes from the process heap (libdrm_nouveau memaligns buffers and hands them to
  nvmap), so leave free heap for it (`source/config.h`).

### FFmpeg 7.1.1 (tools/ffmpeg)

Static libavformat/libavcodec/libavutil: mov, mpegts and aac demuxers, h264/mpeg4/aac/mp3
decoders, NEON, `--target-os=none`, no threads, LGPL.

- **FFmpeg needs int-sized enums.** It is built with `-fno-short-enums`, and so is
  `source/abs_video.c` (its own Makefile rule). That file's interface to the rest of the program
  has no enum types.
- **h264 without hevc does not link.** `h2645_sei.c` calls `ff_aom_uninit_film_grain_params`, but
  `aom_film_grain.o` is built only with HEVC. Port: an empty definition in `source/abs_video.c`
  (without HEVC nothing fills those sets). Upstream: build `aom_film_grain.o` with `h2645_sei.o`,
  or guard the call.
- **Threads are off** (`--disable-pthreads`). H.264 decodes on one thread with
  `skip_loop_filter = AVDISCARD_NONREF`; 720p is the target. A threaded build was not tried.

### mbedTLS 3.6.2 (tools/mbedtls)

No bugs found. `tools/mbedtls/abs_mbedtls_user_config.h`: TLS 1.2 client only; `NET_C`,
`TIMING_C`, `FS_IO`, `HAVE_TIME`, TLS 1.3 and `AESCE_C` off; `MBEDTLS_NO_PLATFORM_ENTROPY` with
`MBEDTLS_ENTROPY_HARDWARE_ALT`, served by `randomGet` in `mbedtls_hardware_poll`
(`source/abs_net.c`). A failing `psa_crypto_init` is tolerated. Certificates are not verified
(`MBEDTLS_SSL_VERIFY_NONE`); verifying would need a CA bundle and a clock.

### miniz (in the image's libnx32 folder)

- **`inflate` takes input greedily.** zlib stops taking input while output is owed; miniz can take
  all of it, trailer included, while still holding output. libpng 1.5.9 then fails with "Not
  enough image data". miniz also accepts only windowBits ±15. Port: `source/bionic_zlib.c` hands
  the last input byte back until the window is drained and parses gzip headers itself. This game
  links its own zlib; the shim matters for games that import libz.

## Getting Angry Birds Space running

### The engine

- One library, `libAngryBirdsSpace.so` (7.3 MB): Rovio's Fusion engine, NDK GCC 4.9, gnustl, ARM
  and Thumb-2, NEON, softfp. Statically linked: Lua 5.1 (float `lua_Number`), Box2D, libpng, zlib,
  libjpeg, libwebp, mpg123, libcurl, BoringSSL, LZMA. `libjs.so` and `libadcolony.so` (ads) are
  not loaded.
- Imports: bionic libc/libm/pthread, 68 GLES2 functions, five AAssetManager calls, liblog, dl*. No
  EGL and no OpenSL. Relocations: `R_ARM_RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`, `ABS32`. No raw
  syscalls.
- Data is under `assets/data/` in the APK. Lua scripts are AES-256-CBC (fixed key, zero IV) around
  an LZMA stream (`\x89LZMA\r\n\x1a\n` header) of Lua 5.1 bytecode; the engine decrypts them.

### Loading

- `source/so_util.c`: the so-loader lineage ported to Elf32 REL (the addend is the word already at
  the target). The image is staged in heap memory, relocated and resolved, then mapped with
  `svcMapProcessCodeMemory` into a 32 MB `virtmemFindCodeMemory` reservation (`SO_REGION_BYTES`),
  RX pages first, then RW. `source/abs_loader.c` drives it and calls `abs_lua_install` last.
- `source/imports.c` is generated by `tools/gen_imports.py`: a `b_<name>` shim, else a data shim,
  else newlib directly where the bionic and newlib ABIs agree; weak imports without one are bound
  to NULL; anything else is an error. gl* names resolve through `dcr_gl_lookup` (Mesa's
  `eglGetProcAddress`).
- C++ exceptions inside the engine need `__gnu_Unwind_Find_exidx` (`source/bionic_dl.c`).
  `source/abs_bionic.c` adds what the shared runtime lacked: rwlocks, condattr,
  `pthread_cond_timedwait_monotonic`, `readdir_r`, `memrchr`, `dup`, and `getauxval` (a Cortex-A57
  in AArch32 state, for BoringSSL's CPU probe).

### Boot contract and JNI

- `source/abs_boot.c` plays `com.rovio.fusion.App` and its GLSurfaceView: constructors and
  `JNI_OnLoad`, `nativeConfig`, `nativeGetPossibleOrientations`, `nativeRenderThread()` (single-
  or multi-threaded), `EGLWrapper.init`, `nativeInit`, `nativeResume`, `nativeResize`, then
  `nativeUpdate` once per frame. Hardware runs single-threaded; the multi-threaded path is
  implemented but not used there.
- HOME: `nativePause`/`nativeResume` with monotonic clocks frozen (`source/bionic_time.c`). Quit:
  `nativeFrameClear`, `nativeDeinit`. Close from HOME: pause (the game saves), exit, 5 s backstop.
- `source/jni_core.c` fills all 233 JNINativeInterface slots by name. Every Call*/NewObject goes
  through one `invoke()` that decodes arguments by the signature.
- Fusion builds method signatures at run time, so handlers in `source/abs_java.c` match by class
  and name (NULL signature: any). Classes are found through
  `Globals.getActivity().getClassLoader().findClass()`, answered from `classes.txt`, the class
  names in the APK's dex files (written by `source/dcr_setup.c`). Unhandled calls are logged once.
  Rovio's cloud SDK (`com.rovio.rcs.*`) and Flurry get neutral answers.

### Android services, graphics, audio, input

- `source/android_ndk.c`: `ANativeWindow` is libnx's default NWindow.
- `source/abs_assets.c`: `AAssetManager` over the APK. The central directory is indexed once;
  `AAsset_getBuffer` returns whole files, inflated with miniz. Reads go through
  `source/dcr_apkcache.c` (128 KiB blocks in RAM, at most 128 MB, only while 256 MB of heap stay
  free). Files in `mods/` replace APK files at the same path.
- `source/bionic_net.c`: the engine's libcurl sees a device without connectivity (sockets never
  connect, lookups fail), so its requests to Rovio's servers fail cleanly.
- `source/abs_egl.c` creates what the GLSurfaceView created (ES 2, RGBA8888, depth 24, stencil 8,
  swap interval 1) and the EGLWrapper context table: [0] blank, [1] the GL thread's, [2..] shared
  contexts, surfaceless. `source/abs_overlay.c` draws the port's UI before each present and
  restores the GL state it touches.
- Audio: the engine creates `AudioOutput(mixer, 16000 Hz, 1 ch, 16 bit, 8192 bytes)` and expects
  its mixer to be pulled with `nativeMixData`. `source/abs_audio.c` pulls 512 frames at a time on
  a thread (priority 0x28, core 2), resamples to 48 kHz stereo (4-point Hermite) and queues
  1024-frame buffers, blocking at three queued (about 64 ms).
- Input: touches go to `MyInputHandler.nativeInput(action, x, y, id)`, keys to `nativeKeyInput`
  (BACK = 4). `source/abs_input.c` turns the controller into synthesized touches placed with what
  the Lua script reports; `source/abs_cursor.c` is a hand cursor (stick, USB mouse, gyro).

### The Lua bridge

- The game has no gamepad code; aiming, camera and menus are touch-driven Lua. `source/abs_lua.c`
  runs `source/abs_ctl.lua` (built in by `source/abs_res.S`) in the game's Lua state and thread.
- Lua's C API is inside the stripped engine. 2.2.14 offsets, found through the base library's
  `luaL_Reg` table at 0x6ea7b8: `luaL_loadbuffer` 0x3f2c28, `lua_pcall` 0x3ffe94, `lua_gettop`
  0x3fc8a8, `lua_settop` 0x3fc8bc, `lua_tolstring` 0x3fdf94. The first two instruction words of
  each are checked at run time; on a mismatch the bridge stays off and touch and the cursor still
  work.
- Only data changes. Before any Lua state exists, nine entries of the math library's `luaL_Reg`
  table (0x6ea650, RW segment) point at wrappers: floor, min, max, abs, sqrt, sin, cos, atan2,
  frexp. The first call after each update request runs `__abs.frame()`, then the real function.
  `math.frexp` with two string arguments is the script's channel to the port.
- The script returns one line of numbers per update (the snapshot `abs_input.c` reads) and carries
  out camera, pause, paging and carousel requests. It also makes the shop free, shows Froot Loops
  Bloopers, opens the links' popups and raises the texture budget.
- `tools/verify_lua_api.py <apk>` runs the same checks on a PC; `luajit tools/test_ctl_mock.lua
  source/abs_ctl.lua` runs the script against a mock game.

### Threads, memory, setup

- The main thread (the GL thread) and every `pthread_create` thread run at priority 59 on cores
  0-2, new threads started round-robin (`source/dcr_sched.c`, `source/bionic_pthread.c`). Hardware
  self-test: the main thread got a core back within 10 ms with one core free and 20 ms with none.
- Port threads: audio 0x28; video loader 0x3B and downloads 0x2C; Orbital Escapade index and prune
  0x3B (`idx_start` in `source/abs_orbital.c`); watchdog and exit backstop 0x2B. The NPDM
  (`abspace_nx.json`) allows priorities 28-59 and cores 0-2; 0x3C is refused.
- Memory: heap 972 MB (hardware), GPU buffers included; 32 MB reserved for the library; APK cache
  up to 128 MB. The game sets a 60 MB texture budget on Android; the script calls
  `native.ResourceManager.setMemoryLimit` with `[performance] texture_memory_mb` (default 200)
  every 10 s.
- First launch: `source/dcr_setup.c` unpacks `lib/armeabi-v7a/libAngryBirdsSpace.so` and writes
  `classes.txt`, with a progress bar. `.setup` records the CRC-32 of each source entry. Assets stay
  in the APK, which may have any name ending in `.apk` (`source/dcr_apkfind.h`).
- Updates: the launcher NRO carries `abspace_nx.nsp` and `abspace_nx.build` (a build number in UTC
  minutes) in its romfs. If the NRO in the game folder has a higher build than `DCR_BUILD`, the
  program rewrites its ExeFS override (`source/dcr_exefs.h`) and restarts. Never a downgrade.
- The game folder was `/switch/abspace` up to build 202609280910. `source/dcr_migrate.c` (no libnx,
  so the 64-bit launcher builds it too) moves an old folder's contents into `/switch/abspace_nx` by
  rename at the first start: folders present in both are merged, existing files are kept, and an
  NRO moves only if it carries this build or a newer one.

### Orbital Escapade (optional)

- The two new worlds of ShadowBird81's PC mod
  ([Game Jolt](https://gamejolt.com/games/OrbitalEscapade/1016788)) become two more planets.
  Nothing of the mod ships; the player copies its `data` folder into the game folder, where it is
  found by its contents (`find_mod_data` in `source/abs_orbital.c`).
- `source/abs_orbital.c` serves the mod's files through `abs_assets.c`. Its Lua (plain, bytecode,
  or under the PC key) is put into the game's container: AES-256-CBC over a literal-only LZMA
  stream. Levels are served under prefixed names, with their `filename` string set to the name
  asked for (the engine aborts otherwise). Sheets are cut to the sprites the game lacks. A missing
  picture or sheet is served empty, because any missing file aborts the engine. A thread builds
  the sprite index; another trims the mod folder to what the worlds use (137 MB to about 22 MB).
- `source/abs_orbital.lua` registers episodes 19 and 20, clones Danger Zone's carousel entity into
  the two places after the last, swaps in the mod's bird and block definitions only inside its worlds, and runs
  the prototype birds' powers through a `fireAction` wrapper.

### Video streaming (the links' popups)

- `source/abs_net.c`: bsd:u sockets (`socketInitialize` with larger TCP buffers), nifm for the
  online check, mbedTLS TLS 1.2, HTTP/1.1 (one connection per request, chunked bodies, redirects).
- `source/abs_yt.c`: YouTube's player API asked as the Android app; 720p H.264 (itag 136) and AAC
  (itag 140) in 1 MiB ranges (whole requests are throttled), 360p itag 18 as fallback, new links
  after a 403.
- `source/abs_video.c`: FFmpeg on a loader thread; pictures stay YUV 4:2:0 in three textures
  converted by a shader; the sound replaces the game's in the mix. Offline, `videos/<topic>.mp4`
  on the SD card plays instead.

## Porting other 32-bit games

### Toolchain setup

- Build in the vita2hos image with `--platform linux/amd64`. It has devkitARM and libnx32 at
  `/opt/devkitpro/libnx32`.
- Mount the patched libnx32 ([github.com/aks796/libnx32](https://github.com/aks796/libnx32)) over
  the image's file by file, read-only, as `build.sh` does:
  `include/switch`, `include/switch.h`, `lib/libnx.a`, `lib/libnxd.a`. Do not mount the whole
  folder: the image keeps miniz, deko3d and other libraries there.
- Use the same flags for the program and every library linked into it: `-march=armv8-a+crc+crypto
  -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE
  -ftls-model=local-exec`. libnx32 reads the thread pointer through `__aeabi_read_tp`, hence
  `-mtp=soft`.
- softfp is the ABI, not a tuning choice (Makefile comment): armeabi-v7a passes float and double
  in core registers, and so do libnx32 and newlib, which are soft-float. softfp still emits
  VFP/NEON code. A hard-float host would need `pcs("aapcs")` on every function that crosses to the
  game or to newlib.
- Link with `dcr32.specs` and `dcr32.ld`. Package with `elf2nso` (main), `npdmtool` (main.npdm)
  and `build_pfs0` (the ExeFS NSP). Check new IPC structs with `tools32/check_short_enums.sh`.

### Address space and packaging

- The NPDM sets `is_64_bit: false` and `address_space_type: 0` (32-bit). Layout measured under
  Ryujinx: code region 0x200000 + ~1 GiB, alias 0x40000000 + 1 GiB, heap 0x80000000 + 1 GiB.
- hbloader is 64-bit and cannot run a 32-bit NRO. This port is an ExeFS NSP installed as
  `/atmosphere/contents/<title id>/exefs.nsp` for a sphaira forwarder. The loader requires
  main.npdm's program id (ACI0 and the ACID range) to match the title; `source/dcr_exefs.h` and
  `tools/make_exefs_override.py` rewrite it. The NPDM's own address space type then applies.
- The 64-bit launcher (`launcher/`, devkitA64) carries the NSP in its romfs, writes the override
  only for the forwarder it runs as (title id 05xx...), and calls `appletRestartProgram`.
- For testing, the NSP can also run through Atmosphère's hbl override with
  `override_any_app_address_space=32_bit`; without it hbl overrides get a 39-bit address space.
  Emulators load the NSP directly.

### Kernel facts (Mesosphere)

- Started as an NSO, the program has no hbloader environment (no process handle, argv or heap
  override). `svcMapProcessCodeMemory` and `svcMapProcessMemory` reject `CUR_PROCESS_HANDLE`;
  `svcSetProcessMemoryPermission` accepts it. A real handle comes from sending
  `CUR_PROCESS_HANDLE` as a copy handle over a session to yourself, as hbloader does
  (`source/selfproc.c`, `source/crt0_reloc.c`).
- Code mapped with `svcMapProcessCodeMemory` comes from page-aligned heap, which is donated and
  must never be freed. Set RX first; W to X is refused. One `svcMapProcessMemory` call must cover
  blocks that share state, permission and attributes.
- The main thread and threads from `threadCreate(..., -2)` run only on their ideal core until the
  mask is widened.
- Horizon does not preempt at game priorities: a runnable thread keeps its core. Mesosphere
  rotates the priority-59 queue of cores 0-2 (63 on core 3) every 10 ms. Engines that spin-wait on
  another thread need their threads at 59 on several cores (`source/dcr_sched.c`).
- libnx's `CondVar` forgets a signal sent while nobody waits. bionic's condvar is a sequence
  counter, and engines signal without the mutex. `source/bionic_pthread.c` builds bionic's
  semantics on WaitForAddress, with 250 ms waits as a backstop.
- The system tick runs during sleep and the HOME menu; `source/bionic_time.c` subtracts suspended
  time from `CLOCK_MONOTONIC`. Horizon has no signals; `source/bionic_signal.c` records handlers
  and delivers nothing.

### Debugging

- `debug.log` in the game folder. During play lines go to a RAM ring that the watchdog writes out
  every 5 s. `[debug] boot_log_on_screen` shows the boot log until EGL takes the window.
  `[jni] unhandled ...` lines list the Java calls the engine makes that have no handler.
- Boot self-tests log `[sched]` (core masks, time slicing), `[pthread]` (WaitForAddress layout),
  `[audio]` (descriptor layout), `[io]` (files open for writing) and, with `[debug] gl_selftest`,
  `[gl-test]`.
- `crash.log`: registers, pc/lr as module+offset, a stack scan (`source/exc_handler.c`).
  Atmosphère's own report is in `sd:/atmosphere/crash_reports/`.
- `source/watchdog.c`: after 10 s without a frame (in focus), every thread is paused briefly and
  reported with priority, core mask, pc, lr and return addresses; again at 40 s and 100 s.
- `CRT0_EXTRA=-DDCR_TEST_ALIAS_RELOC` (after deleting `build/crt0_reloc.o`) builds the hardware
  relocation path for testing under an emulator.
- Host harnesses in `tools/`: `test_net.c` (`abs_net.c`, `abs_json.c`, `abs_yt.c` against
  YouTube); `test_video.c` (the real `abs_video.c` with `tools/host_shim/` and a host FFmpeg in
  `build-ffmpeg-host/`); `test_orbital.c` (`abs_orbital.c` against the mod and the APK's assets);
  `tools/orbital/` (`setup.sh` builds Lua 5.1.5 with float numbers and 32-bit `size_t` bytecode
  plus `orbhost` around the real `abs_orbital.c`; `orbtest.lua` checks registration, the carousel
  and the powers; `loadtest.lua` runs the game's own level loader over every level of the two
  worlds and each level's first seconds).

### Emulator caveats (Ryujinx 1.1.1098)

- It refuses `CUR_PROCESS_HANDLE` in `svcSetProcessMemoryPermission` and does not enforce guest
  page permissions. `source/crt0_reloc.c` detects the emulator that way (`dcr_is_emulator()`);
  relocation, loading and permission changes then take direct paths.
- It maps `svcMapProcessCodeMemory` destinations with permission None and refuses the permission
  change, so the library runs from its heap staging buffer there (`source/so_util.c`).
- Its T32 decoder has no DMB/DSB/ISB; `emu_strip_thumb_barriers` (`source/so_util.c`) replaces
  them with NOP.W under the emulator only.
- Its T32 decoder has no LDREX/STREX either. This game's `__cxa_guard_acquire` (the engine's
  statically linked C++ runtime, Thumb) uses them, and the library's static constructors call it,
  so the boot stops there with an undefined-instruction exception (opcode `e855 3f00`,
  `LDREX r3, [r5]`). The engine has about 170 Thumb LDREX and 210 STREX sites. Under Ryujinx the
  port can be checked up to that point only: setup, the self-tests and the GL test.
- Its A32 decoder lacks VSWP, VADDHN, VSRI, VSLI, VACGT/VACGE, VPADAL, VSHLL by the element size
  (for example VSHLL.I8 #8) and fixed-point VCVT. Mesa's `nv50_sampler_state_create` has a
  fixed-point VCVT, so the GL self-test skips its textured draw there (`source/gl_mesa.c`). This
  port does not rewrite these instructions.
- It invalidates JIT translations only when guest memory is unmapped; no AArch32 cache maintenance
  reaches its translator. `dcr_icache_invalidate` (`source/code_flush.c`) does nothing under the
  emulator, which is harmless here because no code changes after it first runs. A port that
  patches code at run time has to unmap and remap those pages under Ryujinx.
- The time service's shared memory fails to map for 32-bit processes, and libnx's `__appInit`
  aborts on that; `source/nx_init.c` records service failures instead and the clocks fall back to
  the system tick. It declares WaitForAddress with an int32 value (`source/bionic_pthread.c`).
- Priority 59 is time-sliced only intermittently, and after the scheduler self-test its GPU
  emulation stopped presenting frames, so the self-test is skipped there (`source/dcr_sched.c`).
- On Apple silicon (16 KiB host pages) it can alias pages only at the same offset within a host
  page; `find_free_range` (`source/crt0_reloc.c`) keeps aliases congruent modulo 64 KiB.
