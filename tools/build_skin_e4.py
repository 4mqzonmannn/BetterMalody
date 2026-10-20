#!/usr/bin/env python3
"""E4: script_edit_pc.mui に分割メニューの2列目(9項目)を追加した skin-ios.bin を作る。
使い方: python3 -I build_skin_e4.py <元skin-ios.bin> <出力skin-ios.bin>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg

KEY = "3cb1cc2da3dbc808db136e18ea9679aad447e442"   # SHA-1("script_edit_pc.mui")
VALUES = [5, 7, 9, 10, 20, 40, 48, 64, 96]          # make_divide_patch.py の VALUES[9:] と一致させる

def block():
    L = []
    L += ["_rightpanel-12", "    type=5", "    tex=ui-edit-bg2.png", "    rect=3px,3px,4px,4px",
          "    size={rightW},430", "    anchor=8", "    pos=-2,2", "    parent=_right-10"]
    L += ["_right-divx-[1-9]", "    type=1", "    fsize=22", "    color=#FFFFFF", "    anchor=4",
          "    pos=-26,400$-45", "    parent=_right-10", "    etag=104", "    tag=409$1"]
    for i, v in enumerate(VALUES, 1):
        L += [f"_right-divx-{i}", f"    text=1/{v}"]
    return "\n".join(L) + "\n"

def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    text = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    anchor = "\n_rightpanel-11\n    size={rightW},155\n"
    assert text.count(anchor) == 1, "anchor not unique"
    assert "_right-divx-" not in text, "already patched"
    new = text.replace(anchor, "\n" + block() + anchor.lstrip("\n"), 1)
    out = skinpkg.replace(d, KEY, new.encode("utf-8"))
    open(dst, "wb").write(out)
    print("layout", len(text), "->", len(new), "| package", len(d), "->", len(out))

if __name__ == "__main__":
    main()
