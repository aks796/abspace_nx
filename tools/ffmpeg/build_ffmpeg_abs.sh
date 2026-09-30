#!/bin/bash
# build_ffmpeg_abs.sh -- the FFmpeg for Angry Birds Space's videos (the tiny
# planets. pages, abs_video.c): the MP4 (mov), MPEG-TS and AAC demuxers (files and YouTube HLS), the H.264 and MPEG-4
# Part 2 video decoders and the AAC and MP3 audio decoders, as static
# libavformat / libavcodec / libavutil for the AArch32 Switch wrapper
# (libnx32). NEON on, no threads, no programs. LGPL (no --enable-gpl).
#
# The same recipe as build_ffmpeg32.sh in github.com/aks796/ffmpeg32, with the
# H.264 decoder added (most MP4 videos are H.264). The FFmpeg source is its
# unpacked ffmpeg-7.1.1, read only; everything built lands in this project:
#   build-ffmpeg/          the build tree
#   portlibs32/lib, include  where the Makefile finds it (ABS_VIDEO=1)
# Run from abspace_nx/ (tools/ffmpeg/build.sh does the docker run).
# Built with -fno-short-enums (FFmpeg needs int-sized enums; devkitARM defaults
# to short ones): abs_video.c, its one user, is compiled so too.
set -euo pipefail
TOP=/work
SRC=${FFMPEG_SRC:-/ffmpeg-src}
PREFIX=$TOP/build-ffmpeg/prefix
NX=$DEVKITPRO/libnx32
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec"
BUILD=$TOP/build-ffmpeg/tree
mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f config.h ] || [ "${1:-}" = reconfigure ]; then
  "$SRC/configure" --prefix="$PREFIX" \
    --enable-cross-compile --cross-prefix="$DEVKITPRO/devkitARM/bin/arm-none-eabi-" \
    --arch=arm --cpu=armv8-a --target-os=none \
    --enable-static --disable-shared --enable-pic \
    --disable-runtime-cpudetect --enable-neon --enable-vfp \
    --disable-pthreads --disable-w32threads --disable-os2threads \
    --disable-programs --disable-doc --disable-debug --disable-autodetect \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc \
    --disable-network --disable-iconv --disable-zlib --disable-bzlib --disable-lzma \
    --disable-everything \
    --enable-demuxer=mov --enable-demuxer=mpegts --enable-demuxer=aac \
    --enable-decoder=h264 --enable-decoder=mpeg4 --enable-decoder=aac --enable-decoder=mp3 \
    --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=aac --enable-parser=mpegaudio \
    --extra-cflags="$ARCH -O2 -D__SWITCH__ -fno-short-enums -ffunction-sections -fdata-sections -isystem $NX/include" \
    --extra-ldflags="$ARCH -Wl,-z,notext -specs=$NX/switch32.specs -L$NX/lib" \
    --extra-libs="-lnx -lm"
fi
make -j"$(nproc)"
make install
mkdir -p $TOP/portlibs32/lib $TOP/portlibs32/include
cp $PREFIX/lib/libavformat.a $PREFIX/lib/libavcodec.a $PREFIX/lib/libavutil.a $TOP/portlibs32/lib/
rm -rf $TOP/portlibs32/include/libavformat $TOP/portlibs32/include/libavcodec $TOP/portlibs32/include/libavutil
cp -r $PREFIX/include/libavformat $PREFIX/include/libavcodec $PREFIX/include/libavutil $TOP/portlibs32/include/
ls -l $TOP/portlibs32/lib
