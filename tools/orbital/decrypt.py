#!/usr/bin/env python3
"""decrypt.py <unpacked APK assets dir> <out dir> -- the game's Lua files
(data/scripts, data/levels, data/scenes, data/images/*/loadlist.lua) out of
Rovio's container: AES-256-CBC (zero IV), then "\\x89LZMA\\r\\n\\x1a\\n" and an
LZMA-alone stream. Needs openssl."""
import lzma, os, struct, subprocess, sys

KEY = b"RmgdZ0JenLFgWwkYvCL2lSahFbEhFec4"


def dec(path):
    raw = subprocess.run(["openssl", "enc", "-d", "-aes-256-cbc", "-K", KEY.hex(), "-iv", "0" * 32, "-nopad",
                          "-in", path], capture_output=True, check=True).stdout
    if raw[:9] != b"\x89LZMA\r\n\x1a\n":
        return None
    body = raw[9:]
    usize = struct.unpack("<Q", body[5:13])[0]
    return lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(body, max_length=usize)[:usize]


def main():
    src, out = sys.argv[1], sys.argv[2]
    ok = bad = 0
    root = os.path.join(src, "data")
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith(".lua"):
                continue
            p = os.path.join(d, f)
            try:
                data = dec(p)
            except Exception:
                data = None
            if data is None:
                bad += 1
                continue
            o = os.path.join(out, os.path.relpath(p, root))
            os.makedirs(os.path.dirname(o), exist_ok=True)
            open(o, "wb").write(data)
            ok += 1
    print(f"{ok} decrypted, {bad} not in the container")


if __name__ == "__main__":
    main()
