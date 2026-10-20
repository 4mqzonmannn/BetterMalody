#!/usr/bin/env python3
"""Taiko エディタ(script_edit_taiko.mui)に小節線ツールのボタンを足す:
 - 右ツールメニューの 7 番目 `_right-icon3-7`(tag 88, 既定は減光)と、そのラベル `_right-tip3-7`
 - 専用アイコン bm-bar.png(白い小節線)をパッケージに追加
使い方: python3 -I build_skin_taiko.py <入力skin-ios.bin> <出力skin-ios.bin>"""
import sys, os, io, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg
from PIL import Image, ImageDraw
KEY = hashlib.sha1(b"script_edit_taiko.mui").hexdigest()

def bar_png():
    S = 8
    im = Image.new("RGBA", (80 * S, 80 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for x, w in ((14, 7), (36, 4), (58, 4), (68, 7)):       # 小節線(太)・拍の線(細)の並び
        d.rounded_rectangle([x * S, 10 * S, (x + w) * S, 70 * S], radius=w * S // 2, fill=(255, 255, 255, 255))
    im = im.resize((80, 80), Image.LANCZOS)
    b = io.BytesIO(); im.save(b, "PNG"); return b.getvalue()

BLOCK = """
# BetterMalody: 小節線ツール(右ツールメニューの 7 番目)
_right-icon3-7
	tex=bm-bar.png
	tag=88
	opacity=130
	show=1
_right-tip3-7
	text=Bar line
	show=1
"""

def main():
    d = open(sys.argv[1], "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    text = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    assert "_right-icon3-7" not in text
    # 末尾に @if phone ブロックがあるため、その前(共通部)へ入れる
    marker = "\n@if phone"
    assert marker in text
    text = text.replace(marker, BLOCK + marker, 1)
    out = skinpkg.replace(d, KEY, text.encode("utf-8"))
    out = skinpkg.add_entries(out, [("bm-bar.png", bar_png())])
    open(sys.argv[2], "wb").write(out)
    print("taiko layout %d -> %d, bm-bar.png added" % (e["size"], len(text.encode("utf-8"))))

if __name__ == "__main__":
    main()
