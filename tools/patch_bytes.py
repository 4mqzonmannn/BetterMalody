#!/usr/bin/env python3
"""Mach-O にバイト列パッチを当てる(元との照合つき)。
使い方: python3 -I patch_bytes.py <in> <out> <VA(hex)>=<旧hex>:<新hex> [...]
VA→file offset は __TEXT (vmaddr 0x100000000, fileoff 0) 内のみ対応(offset = VA - 0x100000000)。
旧バイトが一致しなければ中止。"""
import sys
BASE = 0x100000000
def main():
    src, dst, *specs = sys.argv[1:]
    d = bytearray(open(src, "rb").read())
    for s in specs:
        va, rest = s.split("=")
        old, new = (bytes.fromhex(x) for x in rest.split(":"))
        assert len(old) == len(new), "length mismatch"
        off = int(va, 16) - BASE
        cur = bytes(d[off:off+len(old)])
        if cur != old:
            raise SystemExit(f"MISMATCH at {va} (off {off:#x}): have {cur.hex()} want {old.hex()}")
        d[off:off+len(new)] = new
        print(f"patched {va} off={off:#x}: {old.hex()} -> {new.hex()}")
    open(dst, "wb").write(d)
if __name__ == "__main__":
    main()
