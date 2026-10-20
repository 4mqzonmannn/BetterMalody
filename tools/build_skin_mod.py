#!/usr/bin/env python3
"""MOD 画面(script_mod.mui)に 12 個目の「TURBO」モッドを足す:
 - mod11.png(白い稲妻のアイコン)をスキンに追加
 - 右パネルに Turbo のメニュー(_turbo: 4 つのトグル行 + 音楽速度の ±)を追加(TURBO を選んでいる間だけ表示。tag 40〜55)
使い方: python3 -I build_skin_mod.py <入力skin-ios.bin> <出力skin-ios.bin>"""
import sys, os, io, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg
from PIL import Image, ImageDraw
KEY = hashlib.sha1(b"script_mod.mui").hexdigest()

def bolt_png():
    S = 8
    im = Image.new("RGBA", (80 * S, 80 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = [(47, 2), (13, 45), (35, 45), (28, 78), (67, 31), (44, 31)]
    d.polygon([(x * S, y * S) for x, y in pts], fill=(255, 255, 255, 255))
    im = im.resize((80, 80), Image.LANCZOS)
    b = io.BytesIO(); im.save(b, "PNG"); return b.getvalue()

def small_icon(im):
    """小アイコン: 既存の mod0s.png(歯車)から絵柄だけ消して六角形の地を残し、稲妻を重ねる"""
    import numpy as np
    t = np.array(im.crop((225, 1422, 225 + 106, 1422 + 120)))
    glyph = t[:, :, 3] > 30
    t[glyph] = (171, 179, 250, 26)
    tile = Image.fromarray(t, "RGBA")
    bolt = Image.open(io.BytesIO(bolt_png())).convert("RGBA").resize((76, 76), Image.LANCZOS)
    tile.alpha_composite(bolt, (15, 22))
    return tile

ATLAS_PNG = hashlib.sha1(b"ui1080-0.png").hexdigest()
ATLAS_PLIST = "5136a852acc61afff75476584f41be0a266b9e23"     # ui1080-0.png の spriteFrame 表(mod0〜10.png もここ)
SLOT = (1058, 1246)
SLOT_S = (936, 1288)   # mod11s.png(小アイコン 106x120: 読み込み画面・リザルトで使う)の置き場所
def add_atlas_icon(d):
    """setSpriteFrame は spriteFrame 名で探すので、mod11.png はアトラス(ui1080-0.png)へ描き足し、plist にフレームを追加する"""
    import plistlib
    _, _, ents = skinpkg.parse(d)
    ep = [x for x in ents if x["key"] == ATLAS_PNG][0]
    im = Image.open(io.BytesIO(d[ep["data"]:ep["data"] + ep["size"]])).convert("RGBA")
    icon = Image.open(io.BytesIO(bolt_png())).convert("RGBA")
    im.paste(icon, SLOT)
    im.paste(small_icon(im), SLOT_S)
    b = io.BytesIO(); im.save(b, "PNG")
    d = skinpkg.replace(d, ATLAS_PNG, b.getvalue())
    _, _, ents = skinpkg.parse(d)
    el = [x for x in ents if x["key"] == ATLAS_PLIST][0]
    pl = plistlib.loads(d[el["data"]:el["data"] + el["size"]])
    pl["frames"]["mod11.png"] = {"frame": "{{%d,%d},{80,80}}" % SLOT, "offset": "{0,0}", "rotated": False,
                                 "sourceColorRect": "{{0,0},{80,80}}", "sourceSize": "{80,80}"}
    pl["frames"]["mod11s.png"] = {"frame": "{{%d,%d},{106,120}}" % SLOT_S, "offset": "{0,0}", "rotated": False,
                                  "sourceColorRect": "{{0,0},{106,120}}", "sourceSize": "{106,120}"}
    return skinpkg.replace(d, ATLAS_PLIST, plistlib.dumps(pl))

# 位置は右パネルの中央にある _mask(anchor=4, pos=50%,458)を基準にした相対指定。パーセント指定は、子の生成時点の親の大きさ(画面サイズ)で計算されるため使わない
ROWS = [(42, 12, "(M) Ignore early miss: OFF"), (43, -73, "(P) Disable pause: OFF")]   # (E) ホールドを終端で判定 は見送り
def block():
    L = ["_turbo", "    size=_right 0,0", "    anchor=6", "    pos=0,0", "    parent=_right", "    zindex=8", "    etag=123", "    tag=40", "    show=0",
         "_turbosolid", "    type=2", "    color=#14161b", "    opacity=255", "    size=_right 0,0", "    anchor=6", "    pos=0,0", "    parent=_turbo", "    etag=123", "    tag=40",
         "_turbobg", "    type=5", "    tex=g-rightbg.png", "    rect=20px,144px,40px,80px", "    size=_right 0,0", "    size2=0,1080px",
         "    anchor=6", "    pos=0,0", "    parent=_turbo", "    etag=123", "    tag=40",
         "_turbot", "    type=1", "    fsize={f1}", "    anchor=4", "    pos=_mask 0,142", "    parent=_turbo", "    zindex=2", "    text=TURBO",
         "_turbod", "    type=1", "    fsize={f3}", "    anchor=4", "    pos=_mask 0,87", "    parent=_turbo", "    zindex=2",
         "    text=Customize the judge rules"]
    for tag, dy, txt in ROWS:
        L += [f"_turbob-{tag}", "    type=4", "    tex=g-dialogbtn.png", "    tex2=g-dialogbtn2.png", "    part=7,46", "    size=720,72",
              "    anchor=4", f"    pos=_mask 0,{dy}", "    parent=_turbo", "    zindex=2", "    etag=123", f"    tag={tag}",
              f"_turbol-{tag}", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=360,36", f"    parent=_turbob-{tag}", f"    tag={tag + 10}",
              f"    text={txt}"]
    # 音楽速度の行: (S) Audio speed  [-]  x1.00  [+]
    L += ["_turbosl", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=_mask -400,-158", "    parent=_turbo", "    zindex=2", "    text=(S) Audio speed",
          "_turbosm", "    type=4", "    tex=g-dialogbtn.png", "    tex2=g-dialogbtn2.png", "    part=7,46", "    size=90,72", "    anchor=4",
          "    pos=_mask 120,-158", "    parent=_turbo", "    zindex=2", "    etag=123", "    tag=45",
          "_turbosmt", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=45,36", "    parent=_turbosm", "    text=-",
          "_turbosv", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=_mask 235,-158", "    parent=_turbo", "    zindex=2", "    tag=55", "    text=x1.00",
          "_turbosp", "    type=4", "    tex=g-dialogbtn.png", "    tex2=g-dialogbtn2.png", "    part=7,46", "    size=90,72", "    anchor=4",
          "    pos=_mask 340,-158", "    parent=_turbo", "    zindex=2", "    etag=123", "    tag=46",
          "_turbospt", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=45,36", "    parent=_turbosp", "    text=+"]
    return "\n".join(L) + "\n"

def main():
    d = open(sys.argv[1], "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    text = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    assert "\n+item\n" in text
    # +item はひな型の開始。以降のノードは項目ごとに複製されるので、本体(右パネルなど)の側 = +item の前へ入れる
    text = text.replace("\n+item\n", "\n" + block() + "+item\n", 1)
    out = skinpkg.replace(d, KEY, text.encode("utf-8"))
    out = add_atlas_icon(out)
    open(sys.argv[2], "wb").write(out)
    print("mod layout %d -> %d, mod11.png added to atlas" % (e["size"], len(text.encode("utf-8"))))

if __name__ == "__main__":
    main()
