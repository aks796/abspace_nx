#!/bin/sh
# setup.sh <unpacked APK assets dir> <folder holding orbital/data> [work dir]
#
# Prepares tools/orbital's test of source/abs_orbital.lua on a PC:
#   - Lua 5.1.5 built with float numbers, as Rovio builds it (lua.org);
#   - the game's own Lua, decrypted (decrypt.py), as lua_dec/;
#   - the names the game has: its scripts' globals (luals.py) and its
#     engine's strings, as android_globals.txt and so_strings.txt;
#   - orbhost: that Lua with the real source/abs_orbital.c and the math.frexp
#     service of source/abs_lua.c.
# Then, from the work dir:
#   QUIET=1 ./orbhost <folder holding orbital/data> <assets> orbtest.lua <repo>/source/abs_orbital.lua
# or loadtest.lua in orbtest.lua's place: that, then the game's own level
# loader (game.lua, gamelogic.lua, the engine stubbed) over every level of
# the two worlds, calibrated on levels of the game's.
set -e
ASSETS="$(cd "$1" && pwd)"
MODROOT="$(cd "$2" && pwd)"
WORK="${3:-/tmp/abs_orbital_test}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
mkdir -p "$WORK"
cd "$WORK"
if [ ! -f lua51f/liblua.a ]; then
  curl -sLO https://www.lua.org/ftp/lua-5.1.5.tar.gz
  tar xzf lua-5.1.5.tar.gz
  rm -rf lua51f && mv lua-5.1.5/src lua51f
  # the game's Lua: float numbers, and bytecode with a 32-bit size_t
  python3 - <<'PY'
import re
p = 'lua51f/luaconf.h'
s = open(p).read()
s = s.replace('#define LUA_NUMBER_DOUBLE\n', '')
s = re.sub(r'#define LUA_NUMBER\tdouble', '#define LUA_NUMBER\tfloat', s)
s = s.replace('"%lf"', '"%f"').replace('"%.14g"', '"%.7g"').replace('strtod((s), (p))', 'strtof((s), (p))')
open(p, 'w').write(s)
p = 'lua51f/lundump.c'
s = open(p).read()
s = s.replace(' size_t size;\n LoadVar(S,size);', ' size_t size;\n unsigned int size32;\n LoadVar(S,size32);\n size=size32;')
s = s.replace('*h++=(char)sizeof(size_t);', '*h++=(char)4; /* 32-bit size_t, as on the devices */')
open(p, 'w').write(s)
PY
  (cd lua51f && make -s posix >/dev/null 2>&1)
fi
python3 "$HERE/decrypt.py" "$ASSETS" lua_dec
: > android_globals.txt
find lua_dec/scripts -name '*.lua' | while read f; do python3 "$HERE/luals.py" "$f" 2>/dev/null; done |
  grep -o "SETGLOBAL r[0-9]* [A-Za-z_][A-Za-z_0-9]*" | awk '{print $3}' | sort -u > android_globals.txt
strings -n 3 "$ASSETS/../lib/armeabi-v7a/libAngryBirdsSpace.so" | grep -E "^[a-zA-Z_][A-Za-z0-9_]{2,40}$" | sort -u > so_strings.txt
cp "$HERE/orbtest.lua" "$HERE/loadtest.lua" "$HERE/dumpt.lua" "$HERE/decrypt.py" .
M="$REPO/build-mbedtls/mbedtls-3.6.2"
mkdir -p hmbed
for f in aes aesni aesce platform_util; do cc -O1 -c -I "$M/include" "$M/library/$f.c" -o "hmbed/$f.o"; done
ar rcs hmbed/libhmbed.a hmbed/*.o
cc -O1 -g -I "$REPO/tools/host_shim" -I "$REPO/source" -I "$REPO/runtime/source" -I "$M/include" -I lua51f "$HERE/orbhost.c" \
  "$REPO/source/abs_orbital.c" "$REPO/source/abs_png.c" lua51f/liblua.a hmbed/libhmbed.a -lz -o orbhost
ln -sfn "$ASSETS" assets
echo "ready: cd $WORK && QUIET=1 ./orbhost $MODROOT $ASSETS orbtest.lua $REPO/source/abs_orbital.lua"
echo "  (loadtest.lua in its place: the game's own level loader over every level of the two worlds)"
