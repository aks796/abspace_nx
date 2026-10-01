#!/bin/sh
# Build the video decoder (tools/ffmpeg/build_ffmpeg_abs.sh) in the toolchain
# container. FFMPEG_SRC: an unpacked ffmpeg-7.1.1 (default: ../ffmpeg32's).
set -e
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${FFMPEG_SRC:-$HERE/../../ffmpeg32/ffmpeg-7.1.1}"
SRC="$(cd "$SRC" && pwd)"
exec docker run --rm --platform linux/amd64 \
  -v "$HERE:/work" -v "$SRC:/ffmpeg-src:ro" -w /work "$IMAGE" \
  bash -lc "bash tools/ffmpeg/build_ffmpeg_abs.sh $*"
