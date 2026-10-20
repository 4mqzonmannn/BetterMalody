#!/usr/bin/env python3
"""E4: 分割メニュー拡張(2列目)のコードパッチ仕様を出力する。
 - 新値テーブル(18 dword)と強調コードをヘッダ余白(0x100005E00〜)へ置く
 - sub_1001FDF90(tag->値): 範囲 8→17, テーブルを新テーブルへ
 - sub_1001FDE70(値->強調ボタン): 先頭を cave へ分岐(値テーブル検索 + ベクタ長ガード)
 - E1(値表[0]=5)を元(2)へ戻す
使い方: python3 -I make_divide_patch.py <元Mach-O>  → apply_container.sh に渡す引数を出力"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
BASE = 0x100000000
from divide_values import UI_TABLE as VALUES
N = len(VALUES)
TABLE = 0x100005D00   # 値表(UI_TABLE 20要素=80B)。フックのケイブ(0x5E48〜)と重ならない空き領域
CAVE  = 0x100005E80
CAVE2 = 0x100005F00
CALLSITE = 0x100073BF0   # bl sub_1001FDCAC(a1+0x4f8, _right-10)

def u32(x): return struct.pack("<I", x & 0xFFFFFFFF)

def adrp(rd, target, pc):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12
    imm &= (1 << 21) - 1
    return 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd

def add_imm(rd, rn, imm12, x=True):
    return (0x91000000 if x else 0x11000000) | (imm12 << 10) | (rn << 5) | rd

def b(pc, target):
    return 0x14000000 | (((target - pc) >> 2) & 0x3FFFFFF)

def bcond(pc, target, cond):
    return 0x54000000 | ((((target - pc) >> 2) & 0x7FFFF) << 5) | cond

def cave_code():
    pc = CAVE
    ins = []
    def emit(x): ins.append(x)
    L1 = L2 = None
    emit(adrp(9, TABLE, pc))                      # adrp x9, TABLE
    emit(add_imm(9, 9, TABLE & 0xFFF))            # add  x9, x9, #off
    emit(0x5280000A)                              # mov  w10, #0
    L1 = CAVE + 4 * len(ins)
    emit(0xB8607800 | (10 << 16) | (9 << 5) | 11)  # ldr  w11, [x9, x10, lsl #2]
    emit(0x6B01017F)                              # cmp  w11, w1
    i_beq = len(ins); emit(0)                     # b.eq L2 (後で)
    emit(0x1100054A)                              # add  w10, w10, #1
    emit(0x7100001F | (N << 10) | (10 << 5))      # cmp  w10, #N
    emit(bcond(CAVE + 4 * len(ins), L1, 3))       # b.lo L1
    emit(0x5280004A)                              # mov  w10, #2
    L2 = CAVE + 4 * len(ins)
    ins[i_beq] = bcond(CAVE + 4 * i_beq, L2, 0)   # b.eq L2
    emit(0xA9400000 | (1 << 15) | (12 << 10) | (0 << 5) | 8)   # ldp  x8, x12, [x0, #8]  (imm7=1 -> +8)
    emit(0xCB08018C)                              # sub  x12, x12, x8
    emit(0xD343FD8C)                              # lsr  x12, x12, #3
    i_cbz = len(ins); emit(0)                     # cbz  x12, Lret (後で)
    emit(0x5280004D)                              # mov  w13, #2
    emit(0xEB0C015F)                              # cmp  x10, x12
    emit(0x9A800000 | (13 << 16) | (3 << 12) | (10 << 5) | 10)   # csel x10, x10, x13, lo
    emit(0x8B000000 | (10 << 16) | (3 << 10) | (8 << 5) | 8)     # add  x8, x8, x10, lsl #3
    emit(b(CAVE + 4 * len(ins), 0x1001FDF08))     # b 0x1001fdf08 (共通テール)
    ins[i_cbz] = 0xB4000000 | ((((CAVE + 4 * len(ins)) - (CAVE + 4 * i_cbz)) >> 2) & 0x7FFFF) << 5 | 12   # cbz x12, Lret
    emit(0xD65F03C0)                              # Lret: ret (ボタン0個なら何もしない)
    return b"".join(u32(x) for x in ins)

def _stp(rt, rt2, rn, off, pre=False):  return (0xA9800000 if pre else 0xA9000000) | (((off // 8) & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt
def _ldp(rt, rt2, rn, off, post=False): return (0xA8C00000 if post else 0xA9400000) | (((off // 8) & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt
def _str(rt, rn, off): return 0xF9000000 | ((off // 8) << 10) | (rn << 5) | rt
def _ldr(rt, rn, off): return 0xF9400000 | ((off // 8) << 10) | (rn << 5) | rt

def cave2_code():
    """収集呼び出し(sub_1001FDCAC(this=x0, parent=x1))の差し替え。
    親(_right-10)の children[1] = リスト(ListSprite)を取り、情報画面と同じ再レイアウト
    sub_100048FF8(list, 0) を呼んでから、**リスト内部コンテナ(list+0x618 = 1560)**を親として収集する。"""
    ins = []
    emit = ins.append
    emit(_stp(29, 30, 31, -48, pre=True))   # stp x29, x30, [sp, #-48]!
    emit(_stp(0, 1, 31, 16))                # stp x0, x1, [sp, #16]
    emit(_str(19, 31, 32))                  # str x19, [sp, #32]
    emit(0xAA0103E0)                        # mov x0, x1
    emit(_ldr(8, 0, 0))                     # ldr x8, [x0]
    emit(_ldr(8, 8, 0x228))                 # ldr x8, [x8, #0x228]   ; getChildren()
    emit(0xD63F0100)                        # blr x8
    emit(_ldp(8, 9, 0, 0))                  # ldp x8, x9, [x0]       ; begin, end
    emit(0xCB080129)                        # sub x9, x9, x8
    emit(0xD343FD29)                        # lsr x9, x9, #3
    emit(0xF100093F)                        # cmp x9, #2
    i_blo = len(ins); emit(0)               # b.lo skip
    emit(_ldr(19, 8, 8))                    # ldr x19, [x8, #8]      ; list
    emit(0xAA1303E0)                        # mov x0, x19
    emit(0x52800001)                        # mov w1, #0
    emit(0x94000000 | (((0x100048FF8 - (CAVE2 + 4 * len(ins))) >> 2) & 0x3FFFFFF))   # bl sub_100048FF8
    emit(_ldr(1, 19, 0x618))                # ldr x1, [x19, #0x618]  ; list container
    emit(_ldr(0, 31, 16))                   # ldr x0, [sp, #16]
    i_b = len(ins); emit(0)                 # b call
    skip = CAVE2 + 4 * len(ins)
    emit(_ldp(0, 1, 31, 16))                # skip: ldp x0, x1, [sp, #16]
    call = CAVE2 + 4 * len(ins)
    emit(_ldr(19, 31, 32))                  # call: ldr x19, [sp, #32]
    emit(_ldp(29, 30, 31, 48, post=True))   # ldp x29, x30, [sp], #48
    emit(b(CAVE2 + 4 * len(ins), 0x1001FDCAC))   # b sub_1001FDCAC
    ins[i_blo] = bcond(CAVE2 + 4 * i_blo, skip, 3)
    ins[i_b] = b(CAVE2 + 4 * i_b, call)
    return b"".join(u32(x) for x in ins)

def main():
    """使い方: make_divide_patch.py <現在のバイナリ>  (現在値との差分だけを spec として出力する)"""
    d = open(sys.argv[1], "rb").read()
    specs = []
    def want(va, new):
        cur = d[va - BASE: va - BASE + len(new)]
        if cur != new:
            specs.append("0x%X=%s:%s" % (va, cur.hex(), new.hex()))
    table = b"".join(u32(v) for v in VALUES)
    table += b"\x00" * ((-len(table)) % 4 + (72 - len(table) if len(table) < 72 else 0))   # 旧18要素分(72B)まで 0 埋め
    want(TABLE, table)
    # tag->値 (sub_1001FDF90): 範囲 N-1, テーブル参照
    want(0x1001FE014, u32(0x7100001F | ((N - 1) << 10) | (8 << 5)))   # cmp w8, #(N-1)
    want(0x1001FE01C, u32(adrp(9, TABLE, 0x1001FE01C)))               # adrp x9, TABLE
    want(0x1001FE020, u32(add_imm(9, 9, TABLE & 0xFFF)))              # add  x9, x9, #off
    # 値->強調 (sub_1001FDE70): 先頭を cave へ
    want(CAVE, cave_code())
    want(0x1001FDE70, u32(b(0x1001FDE70, CAVE)))
    # toolbar収集: 親を list(children[1]) に
    want(CAVE2, cave2_code())
    want(CALLSITE, u32(0x94000000 | (((CAVE2 - CALLSITE) >> 2) & 0x3FFFFFF)))   # bl CAVE2
    # E1 の残骸(値表[0]=5)は元(2)へ
    want(0x10080AFA0, u32(2))
    print(" ".join(specs))

if __name__ == "__main__":
    main()
