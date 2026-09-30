#!/bin/sh
# Build the video decoder (tools/ffmpeg/build_ffmpeg_abs.sh) in the toolchain
# container. FFMPEG_SRC: an unpacked ffmpeg-7.1.1 (default: the one in an
# ffmpeg32 checkout next to this folder, ../ffmpeg32/ffmpeg-7.1.1, else
# ../../ffmpeg32/ffmpeg-7.1.1; github.com/aks796/ffmpeg32's ./build.sh
# downloads and unpacks it).
set -e
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
if [ -n "${FFMPEG_SRC:-}" ]; then
  SRC="$FFMPEG_SRC"
elif [ -f "$HERE/../ffmpeg32/ffmpeg-7.1.1/configure" ]; then
  SRC="$HERE/../ffmpeg32/ffmpeg-7.1.1"
else
  SRC="$HERE/../../ffmpeg32/ffmpeg-7.1.1"
fi
if [ ! -f "$SRC/configure" ]; then
  echo "tools/ffmpeg/build.sh: no FFmpeg 7.1.1 source at ${FFMPEG_SRC:-$HERE/../ffmpeg32/ffmpeg-7.1.1}" >&2
  echo "  clone github.com/aks796/ffmpeg32 next to this folder and run its ./build.sh" >&2
  echo "  (it downloads the source), or set FFMPEG_SRC to an unpacked ffmpeg-7.1.1" >&2
  exit 1
fi
SRC="$(cd "$SRC" && pwd)"
exec docker run --rm --platform linux/amd64 \
  -v "$HERE:/work" -v "$SRC:/ffmpeg-src:ro" -w /work "$IMAGE" \
  bash -lc "bash tools/ffmpeg/build_ffmpeg_abs.sh $*"
