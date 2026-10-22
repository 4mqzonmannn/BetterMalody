#!/usr/bin/env python3
"""ストア画面(script_store.mui)の見出しに「Malody / osu! ストア切替」ボタン(_head-btn-4, tag 9)を足す。
 - 右上の検索 / モードの並びへ 3 つ目として加える(押下は dylib 側 = sub_1000ACEA4 のフック)
使い方: python3 -I build_skin_store.py <入力skin-ios.bin> <出力skin-ios.bin>"""
import sys, os, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg
KEY = hashlib.sha1(b"script_store.mui").hexdigest()

def main():
    d = open(sys.argv[1], "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    text = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    assert "_head-btn-4" not in text
    a = "_head-btn-[2,3]\n"
    assert a in text
    text = text.replace(a, "_head-btn-[2,3,4]\n", 1)
    b = "_head-btn-3\n    tag=4\n    tex=icon-mode.png\n"
    assert b in text
    text = text.replace(b, b + "_head-btn-4\n    tag=9\n    tex=icon-system.png\n", 1)
    c = "_head-btn-3\n    tex=icon-mode0.png\n"
    assert c in text
    text = text.replace(c, c + "_head-btn-4\n    tex=icon-system0.png\n", 1)
    open(sys.argv[2], "wb").write(skinpkg.replace(d, KEY, text.encode("utf-8")))
    print("store layout %d -> %d" % (e["size"], len(text.encode("utf-8"))))

if __name__ == "__main__":
    main()
