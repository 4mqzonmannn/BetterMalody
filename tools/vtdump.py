#!/usr/bin/env python3
"""Mach-O arm64 (非chained-fixup) の vtable を読む。使い方: python3 -I vtdump.py <macho> <vaddr(hex)> <nslots>
スロットの値(関数VA)を表示する。--rel でスライド前VAのみ表示。読み取り専用。"""
import struct, subprocess, sys, re

def segments(path):
    out = subprocess.run(["otool", "-arch", "arm64", "-l", path], capture_output=True, text=True).stdout
    segs, cur = [], None
    for line in out.splitlines():
        m = re.match(r"\s+segname (\S+)", line)
        if m: cur = {"name": m.group(1)}; segs.append(cur); continue
        if cur is not None:
            for k in ("vmaddr", "vmsize", "fileoff", "filesize"):
                m = re.match(rf"\s+{k} (0x[0-9a-f]+|\d+)", line)
                if m and k not in cur: cur[k] = int(m.group(1), 0)
    return [s for s in segs if "vmaddr" in s and "fileoff" in s]

def main():
    path, va, n = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3])
    data = open(path, "rb").read()
    segs = segments(path)
    def off(v):
        for s in segs:
            if s["vmaddr"] <= v < s["vmaddr"] + s["vmsize"] and s.get("filesize", 0):
                return v - s["vmaddr"] + s["fileoff"]
        raise SystemExit(f"VA {v:#x} not in file-backed segment")
    base = off(va)
    for i in range(n):
        (p,) = struct.unpack_from("<Q", data, base + 8 * i)
        print(f"slot{i:3d} @{va + 8*i:#x} -> {p:#x}")

if __name__ == "__main__":
    main()
