#!/usr/bin/env python3
"""verify_lua_api.py -- does this APK's engine match the controller bridge?

The bridge (source/abs_lua.c) calls Lua's C API inside libAngryBirdsSpace.so
at addresses located in Angry Birds Space HD 2.2.14, and hooks entries of its
math library table. On the console it checks the same things before using
them and turns itself off on a mismatch; this runs the check on a PC.

    python3 tools/verify_lua_api.py your.apk
    python3 tools/verify_lua_api.py path/to/libAngryBirdsSpace.so
"""
import re, struct, sys, zipfile

# keep in step with k_fn[] and MATHLIB_REG in source/abs_lua.c
FUNCS = {
    "luaL_loadbuffer": (0x3F2C28, 0xE52DE004, 0xE24DD00C),
    "lua_pcall": (0x3FFE94, 0xE3530000, 0xE92D4030),
    "lua_gettop": (0x3FC8A8, 0xE5902008, 0xE590300C),
    "lua_settop": (0x3FC8BC, 0xE3510000, 0xBA00000B),
    "lua_tolstring": (0x3FDF94, 0xE92D4070, 0xE2515000),
}
MATHLIB_REG, MATHLIB_N = 0x6EA650, 28
HOOKED = ["floor", "min", "max", "abs", "sqrt", "sin", "cos", "atan2", "frexp"]


def load(path):
    if path.endswith(".so"):
        return open(path, "rb").read()
    with zipfile.ZipFile(path) as z:
        return z.read("lib/armeabi-v7a/libAngryBirdsSpace.so")


def v2o(data, vaddr):
    phoff, = struct.unpack_from("<I", data, 0x1C)
    phnum, = struct.unpack_from("<H", data, 0x2C)
    for i in range(phnum):
        p_type, p_off, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", data, phoff + 32 * i)
        if p_type == 1 and p_vaddr <= vaddr < p_vaddr + p_filesz:
            return vaddr - p_vaddr + p_off
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = load(sys.argv[1])
    ok = True
    for name, (off, w0, w1) in FUNCS.items():
        o = v2o(data, off)
        got = struct.unpack_from("<II", data, o) if o is not None else (None, None)
        good = got == (w0, w1)
        ok &= good
        print(f"{name:16s} 0x{off:06x}: {'ok' if good else 'MISMATCH'}")
    o = v2o(data, MATHLIB_REG)
    names = []
    for i in range(MATHLIB_N):
        np_, fp = struct.unpack_from("<II", data, o + 8 * i)
        no = v2o(data, np_)
        names.append(data[no:data.index(b"\0", no)].decode() if no is not None else "?")
    missing = [h for h in HOOKED if h not in names]
    ok &= not missing
    print(f"math library table 0x{MATHLIB_REG:06x}: {'ok' if not missing else 'MISSING ' + ' '.join(missing)}")
    print("the controller bridge will be ON" if ok else "the bridge will stay OFF (touch and the pointer still work)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
