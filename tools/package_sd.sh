#!/bin/sh
# package_sd.sh -- assemble SD_CARD/ (and SD_CARD.zip) for a test on a Switch.
#
#   tools/package_sd.sh [path/to/your/AngryBirdsSpaceHD.apk]
#
# Builds the wrapper and the launcher, then lays out what goes on the card:
#   SD_CARD/switch/abspace_nx/abspace_nx.nro
#   SD_CARD/switch/abspace_nx/<your APK> (only if an APK path is given: your own
#                                      copy, under its own name)
#   SD_CARD/switch/abspace_nx/videos/README.txt  (where the tiny planets' videos go)
#   SD_CARD/switch/abspace_nx/orbital/README.txt (where the Orbital Escapade mod's
#                                      data folder goes; the mod is not included)
#   SD_CARD/README_FIRST.txt
# The ExeFS override is not included: the launcher writes it for whichever
# sphaira forwarder it is started from.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
./build.sh
launcher/build.sh
if [ -n "$1" ]; then
  python3 tools/verify_lua_api.py "$1" || echo "(the game will run with touch and the pointer only)"
fi
rm -rf SD_CARD SD_CARD.zip
mkdir -p SD_CARD/switch/abspace_nx
cp launcher/abspace_nx.nro SD_CARD/switch/abspace_nx/
cp tools/README_FIRST.txt SD_CARD/
mkdir -p SD_CARD/switch/abspace_nx/videos
cp sd/VIDEOS.txt SD_CARD/switch/abspace_nx/videos/README.txt
mkdir -p SD_CARD/switch/abspace_nx/orbital
cp sd/ORBITAL.txt SD_CARD/switch/abspace_nx/orbital/README.txt
if [ -n "$1" ]; then
  cp -p "$1" "SD_CARD/switch/abspace_nx/$(basename "$1")"
fi
(cd SD_CARD && zip -qr ../SD_CARD.zip .)
echo "build $(cat abspace_nx.build): SD_CARD/ and SD_CARD.zip ready"
