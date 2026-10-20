#!/usr/bin/env python3
"""スロー再生の下限を 0.5 倍 → 0.125 倍に広げるパッチ仕様を出力する。
 sub_1001F5A84(再生速度の設定)は 速度を [0.5, 1.5] に丸めている(FMOV S8,#0.5 が下限)。この定数を 0.125 にする。
 再生機は FMOD のチャンネル速度 + SoundTouch pitch shift DSP(1/速度)で作るため、下限を変えるだけで 0.125 倍まで鳴る。
 使い方: python3 -I make_slow_patch.py <Mach-O>  → patch_bytes.py / apply_container.sh に渡す引数(適用済みなら何も出さない)"""
import struct, sys
BASE = 0x100000000
VA = 0x1001F5AB4
def fmov_s(rd, imm8): return 0x1E201000 | (imm8 << 13) | rd
def value(imm8):
    # VFPExpandImm(単精度): sign = imm8[7]、exp = NOT(b):Replicate(b,5):cd、frac = efgh
    s = imm8 >> 7; b = (imm8 >> 6) & 1; cd = (imm8 >> 4) & 3; efgh = imm8 & 15
    bits = (s << 31) | ((((b ^ 1) << 7) | ((b * 0x1F) << 2) | cd) << 23) | (efgh << 19)
    return struct.unpack("<f", struct.pack("<I", bits))[0]
def find(val):
    for imm in range(256):
        if abs(value(imm) - val) < 1e-9: return imm
    raise SystemExit("no imm8 for %g" % val)
def main():
    d = open(sys.argv[1], "rb").read()
    off = VA - BASE
    cur = struct.unpack("<I", d[off:off + 4])[0]
    old = fmov_s(8, find(0.5)); new = fmov_s(8, find(0.125))
    if cur == new: return
    if cur != old: raise SystemExit("unexpected bytes at %#x: %08x" % (VA, cur))
    print("%#x=%s:%s" % (VA, struct.pack("<I", old).hex(), struct.pack("<I", new).hex()))
main()
