#---------------------------------------------------------------------------------
# Angry Birds Space -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the game's own APK (the user's copy,
# any name) is read at run time; its library is unpacked from it on the first
# launch; its assets are read from the APK itself.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: abspace_nx.nsp, which the launcher NRO carries (launcher/).
#---------------------------------------------------------------------------------
TARGET               := abspace_nx
PORT_NPDM_PROGRAM_ID := 0x010000000000100F

# The tiny planets' videos: FFmpeg's MP4 demuxer and H.264 / MPEG-4 / AAC /
# MP3 decoders, built by tools/ffmpeg/build.sh (LGPL) into portlibs32/.
# Without them abs_video.c builds without pictures (the pages have no video
# button). Streaming the links' videos: mbedTLS (tools/mbedtls/build.sh,
# Apache-2.0) for HTTPS, its configuration in portlibs32/include.
ABS_PORTLIBS := $(CURDIR)/portlibs32
ABS_VIDEO    := $(if $(wildcard $(ABS_PORTLIBS)/lib/libavcodec.a),1,0)
ifeq ($(ABS_VIDEO),1)
PORT_LIBS    := -L$(ABS_PORTLIBS)/lib -lavformat -lavcodec -lavutil
endif
PORT_LIBS    += -L$(ABS_PORTLIBS)/lib -lmbedtls -lmbedx509 -lmbedcrypto
PORT_CFLAGS  := -DMBEDTLS_USER_CONFIG_FILE="<abs_mbedtls_user_config.h>"
PORT_STAMP   := -v$(ABS_VIDEO)
include runtime/runtime.mk

# FFmpeg is built with int-sized enums (-fno-short-enums), and so is its one
# user here, whose interface to the rest has no enum types (the runtime links
# with --no-enum-size-warning).
$(BUILD)/abs_video.o: $(SOURCES)/abs_video.c $(RENDERER_STAMP) | $(BUILD) $(BUILD)/dcr_build.h
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -fno-short-enums -DABS_VIDEO=$(ABS_VIDEO) -c $< -o $@

# The controller script, the Orbital Escapade script and the hand cursor's
# pictures, assembled in with .incbin (not seen by -MMD).
$(BUILD)/abs_res.o: $(SOURCES)/abs_ctl.lua $(SOURCES)/abs_orbital.lua $(wildcard $(SOURCES)/cursor/*.png)

.PHONY: check
check:
	@echo "run on the host: python3 tools/verify_lua_api.py <your.apk>"
	@echo "                 python3 runtime/tools/gen_imports.py --check"
