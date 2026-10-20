#!/usr/bin/env python3
"""グリッド線の「灰色」ベース色(0.4)を設定する `mov v0/v1/v2.16b, vN.16b` を FMOV S<d>,#imm に置換するパッチ仕様を出力する。
使い方: python3 -I make_gray_patch.py <元Mach-O> <値(例 0.75)>   → apply_container.sh に渡す引数を標準出力へ"""
import struct, sys
BASE = 0x100000000
SITES = [0x1001a2e0c, 0x1001b16d4, 0x1001b7c38, 0x1000c6548]   # strip / lane / 他モード2箇所 (各3命令)

def fmov_imm8(val):
    for imm in range(256):
        s = imm >> 7; b = (imm >> 6) & 1; cd = (imm >> 4) & 3; efgh = imm & 15
        exp = ((b ^ 1) << 7) | ((b * 0x1f) << 2) | cd
        bits = (s << 31) | (exp << 23) | (efgh << 19)
        if struct.unpack('<f', struct.pack('<I', bits))[0] == val:
            return imm
    raise SystemExit("not encodable as FMOV imm: %r" % val)

def main():
    path, val = sys.argv[1], float(sys.argv[2])
    d = open(path, "rb").read()
    imm = fmov_imm8(val)
    specs = []
    for site in SITES:
        for r in range(3):
            va = site + 4 * r
            old = struct.unpack_from("<I", d, va - BASE)[0]
            # ORR Vd.16B, Vn.16B, Vn.16B : 0x4EA01C00 | (Vm<<16) | (Vn<<5) | Vd, Vm==Vn
            assert (old & 0xFFE0FC00) == 0x4EA01C00, f"unexpected instr {old:#x} at {va:#x}"
            vd, vn, vm = old & 31, (old >> 5) & 31, (old >> 16) & 31
            assert vd == r and vn == vm, f"unexpected regs at {va:#x}"
            new = 0x1E201000 | (imm << 13) | r        # FMOV S<r>, #val
            specs.append("0x%X=%s:%s" % (va, struct.pack("<I", old).hex(), struct.pack("<I", new).hex()))
    print(" ".join(specs))

if __name__ == "__main__":
    main()
