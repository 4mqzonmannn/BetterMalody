#!/usr/bin/env python3
"""分割メニューを「1列・全項目 汎用フォント画像」にした skin-ios.bin を作る。
使い方: build_skin_divide.py <元skin-ios.bin> <ラベル画像ディレクトリ> <出力skin-ios.bin>"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg
from divide_values import VALUES, ROW_TOP, ROW_STEP, PANEL_H, SPEED_ROWS

KEY = "3cb1cc2da3dbc808db136e18ea9679aad447e442"   # SHA-1("script_edit_pc.mui")
OLD_BLOCK_START = "_right-div-[1-9]\n"
OLD_BLOCK_END = "_right-div-9\n    tex=ui-edit-div32.png\n"
OLD_PANEL = "_rightpanel-10\n    parent=_right-10\n"
PHONE_OLD = "_right-div-[1-9]\n    size=0,40\n    pos=30,685$-80\n    react=20,20\n"

def new_block():
    n = len(VALUES)
    L = ["_right-dl", "    type=101", "    size={rightW},100%-40", "    size2=0,14", "    anchor=5",
         "    pos={rightW}-2,50%", "    parent=_right-10", "    zindex=2", "    etag=104"]
    # リスト型は「子の並び順」を名前だけの行で指示する(情報画面 _infolist と同じ書式)
    L += ["# parsed at the order", "_right-divup", "_right-divdn"] + [f"_right-div-{i}" for i in range(1, n + 1)] + [""]
    # 分割数 +1/-1 の矢印行(タグ 727/728。dylib の hook_touch が処理する)
    L += ["_right-divup", "    tex=bm-div-up.png", "    size=0,30", "    parent=_right-dl", "    etag=104", "    tag=727",
          "_right-divdn", "    tex=bm-div-dn.png", "    size=0,30", "    parent=_right-dl", "    etag=104", "    tag=728", ""]
    L += [f"_right-div-[1-{n}]", f"    tex=bm-div-{VALUES[0]}.png", "    size=0,30",
          "    parent=_right-dl", "    etag=104", "    tag=402$1"]   # 値表(UI_TABLE)は先頭に ▲▼ のセンチネル 2 つ。stock の tag->値は表[tag-400] なので最初の値行は 402
    for i, v in enumerate(VALUES[1:], 2):
        L += [f"_right-div-{i}", f"    tex=bm-div-{v}.png"]
    return "\n".join(L) + "\n"

SPD_OLD = ("_rightpanel-11\n    size={rightW},155\n    parent=_right-11\n_right-spd-[1-3]\n    tex=ui-edit-speed1.png\n    size=0,26\n"
           "    anchor=4\n    pos=24,125$-45\n    parent=_right-11\n    etag=104\n    tag=500$1\n_right-spd-2\n    tex=ui-edit-speed75.png\n"
           "_right-spd-3\n    tex=ui-edit-speed5.png\n")
SPD_PHONE_OLD = "_rightpanel-11\n    size={rightW}+10,230\n_right-spd-[1-3]\n    size=0,40\n    pos=30,200$-{rightP}\n    react=17,20\n"

def speed_block():
    """再生速度メニュー: 先頭に ▲+0.05 / ▼-0.05(タグ 730/731)、続いてプリセット(タグ 732..)。全タグを dylib が処理する"""
    n = 2 + len(SPEED_ROWS)
    top = 35 + 45 * (n - 1)
    L = ["_rightpanel-11", f"    size={{rightW}},{top + 30}", "    parent=_right-11",
         f"_right-spd-[1-{n}]", "    tex=bm-div-up.png", "    size=0,26", "    anchor=4", f"    pos=24,{top}$-45",
         "    parent=_right-11", "    etag=104", "    tag=730$1", "_right-spd-2", "    tex=bm-div-dn.png"]
    for i, (pct, _) in enumerate(SPEED_ROWS, 3):
        L += [f"_right-spd-{i}", f"    tex=bm-spd-{pct}.png"]
    return "\n".join(L) + "\n"

def speed_phone_block():
    n = 2 + len(SPEED_ROWS)
    top = 200 + 75 * (n - 3)        # rightP(phone)=75: 元の3行の最上段 200 から (n-3) 行ぶん上へ
    return (f"_rightpanel-11\n    size={{rightW}}+10,{top + 30}\n_right-spd-[1-{n}]\n    size=0,40\n"
            f"    pos=30,{top}$-{{rightP}}\n    react=17,20\n")

INFO_ORDER_OLD = "_infot-7\n_empty-3\n_infob-1\n"
INFO_ORDER_NEW = ("_infot-7\n_empty-3\n_infol-12\n_infoe-12\n_infot-12\n_infol-13\n_infoe-13\n_infot-13\n"
                  "_infol-14\n_infoe-14\n_infot-14\n_infol-15\n_infoe-15\n_infot-15\n"
                  "_infol-16\n_infoe-16\n_infot-16\n_empty-4\n_infob-1\n")
INFO_DEF_OLD = "_empty-[1-3]\n    size=1,20\n    parent=_infolist\n"
INFO_DEF_NEW = INFO_DEF_OLD + """_empty-4
    size=1,20
    parent=_infolist
_infol-[12-16]
    type=1
    fsize={panelF}
    parent=_infolist
_infoe-[12-16]
    type=4
    tex=g-input.png
    part=10,50
    parent=_infolist
    size={listW},40
    etag=104
_infot-[12-16]
    type=1
    fsize=20
    size2=320,0
    anchor=3
    pos=10,26
    etag=104
_infoe-12
    tag=745
_infoe-13
    tag=746
_infoe-14
    tag=747
_infot-12
    parent=_infoe-12
    tag=745
_infot-13
    parent=_infoe-13
    tag=746
_infot-14
    parent=_infoe-14
    tag=747
_infoe-15
    tag=749
_infoe-16
    tag=751
_infot-15
    parent=_infoe-15
    tag=749
_infot-16
    parent=_infoe-16
    tag=751
_infob-[2-8]
    size=0,36
    anchor=5
    etag=104
_infob-2
    tex=bm-info-x.png
    parent=_infoe-12
    pos=100%-10,50%
    tag=752
_infob-3
    tex=bm-info-x.png
    parent=_infoe-13
    pos=100%-10,50%
    tag=753
_infob-4
    tex=bm-info-x.png
    parent=_infoe-14
    pos=100%-10,50%
    tag=754
_infob-5
    tex=bm-info-minus.png
    parent=_infoe-15
    pos=100%-80,50%
    tag=748
_infob-6
    tex=bm-info-plus.png
    parent=_infoe-15
    pos=100%-10,50%
    tag=749
_infob-7
    tex=bm-info-minus.png
    parent=_infoe-16
    pos=100%-80,50%
    tag=750
_infob-8
    tex=bm-info-plus.png
    parent=_infoe-16
    pos=100%-10,50%
    tag=751
_infol-15
    text=Bar begin
_infol-16
    text=Speed (Catch)
_infol-12
    text=Video
_infol-13
    text=BGA
_infol-14
    text=Skin
"""

def main():
    src, labels, dst = sys.argv[1:4]
    d = open(src, "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    t = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    a = t.index(OLD_BLOCK_START); b = t.index(OLD_BLOCK_END) + len(OLD_BLOCK_END)
    assert t.count(OLD_BLOCK_END) == 1 and t.count(OLD_PANEL) == 1
    t2 = t[:a] + new_block() + t[b:]
    # phone 用の上書き(@if phone 内): 9項目 x 80 → 16項目 x 43 に合わせる
    n = len(VALUES)
    assert t2.count(PHONE_OLD) == 1, "phone block not found"
    t2 = t2.replace(PHONE_OLD, f"_right-dl\n    size={{rightW}}+10,100%-40\n    size2=0,20\n_right-div-[1-{n}]\n    size=0,40\n    react=20,20\n_right-divup\n    size=0,40\n    react=20,20\n_right-divdn\n    size=0,40\n    react=20,20\n", 1)
    t2 = t2.replace(OLD_PANEL, OLD_PANEL + "    size={rightW},100%-4\n", 1)
    assert t2.count(INFO_ORDER_OLD) == 1 and t2.count(INFO_DEF_OLD) == 1, "info block not found"
    t2 = t2.replace(INFO_ORDER_OLD, INFO_ORDER_NEW, 1).replace(INFO_DEF_OLD, INFO_DEF_NEW, 1)
    assert t2.count(SPD_OLD) == 1 and t2.count(SPD_PHONE_OLD) == 1, "speed block not found"
    t2 = t2.replace(SPD_OLD, speed_block(), 1).replace(SPD_PHONE_OLD, speed_phone_block(), 1)
    out = skinpkg.replace(d, KEY, t2.encode("utf-8"))
    items = [(f"bm-div-{v}.png", open(os.path.join(labels, f"bm-div-{v}.png"), "rb").read()) for v in VALUES]
    items += [(f"bm-div-{k}.png", open(os.path.join(labels, f"bm-div-{k}.png"), "rb").read()) for k in ("up", "dn")]
    items += [(f"bm-spd-{p}.png", open(os.path.join(labels, f"bm-spd-{p}.png"), "rb").read()) for p, _ in SPEED_ROWS]
    items += [(f"bm-info-{k}.png", open(os.path.join(labels, f"bm-info-{k}.png"), "rb").read()) for k in ("x", "minus", "plus")]
    items += [(f"bm-{k}.png", open(os.path.join(labels, f"bm-{k}.png"), "rb").read()) for k in ("undo", "redo", "plugin")]
    out = skinpkg.add_entries(out, items)
    open(dst, "wb").write(out)
    print("layout", len(t), "->", len(t2), "| package", len(d), "->", len(out), "| entries +", len(items))

if __name__ == "__main__":
    main()
