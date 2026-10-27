#!/usr/bin/env python3
"""設定画面(script_config.mui)の改善:
 - Audio の先頭に「Master Volume」スライダー(_area1-7, tag 90/96/97)
 - 各スライダーの値に単位(% / ms / °)と、値をタップして数値を手入力する当たり判定(tag 80〜85)
 - 手入力の小パネル(_cfgp: 入力欄 tag 94 / OK 91 / Cancel 92 / 背景 99)
 - 項目の並べ替え(Gameplay / Graphics)
使い方: python3 -I build_skin_cfg.py <入力skin-ios.bin> <出力skin-ios.bin>"""
import sys, os, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skinpkg
KEY = hashlib.sha1(b"script_config.mui").hexdigest()

# --- 並べ替え / 行の追加 ---
AREA1_OLD = "_title-1\n_line-1\n_area1-[1-6]\n    parent=_list\n    size=100%,90\n"
AREA1_NEW = ("_title-1\n_line-1\n_area1-7\n_area1-[1-6]\n    parent=_list\n    size=100%,90\n"
             "_area1-7\n    parent=_list\n    size=100%,90\n")
AREA2_OLD = "_area2-[1-8]\n"
AREA2_NEW = "_area2-[1,10-16,2,17-23,6,7,3,5,4,8]\n"   # Offset(見出し 1 + モードごとのゲージ 10-16)/ Hitposition(見出し 2 + 17-23)/ 3D column / angle / DJ disk / fullwidth / storyboard / portrait
AREA3_OLD = "_area3-[1,5,3,7,6,4,2]\n"
AREA3_NEW = "_area3-[4,2,3,7,1,5,6]\n"              # Language / Skin / Original title / Grid mode / FPS(, 非表示の 2 つ)
AREA6_OLD = "_area6-[1-3]\n"
AREA6_NEW = "_area6-[1-9]\n"                        # 最下段に「osu! Store」「BetterMalody」の行と、Note speed の見出し(6)・モードごとの行(7-9)を足す
BTN_OLD = "_button-[1-13]\n"
BTN_NEW = "_button-[1-18]\n"
BTNSTR_OLD = "_buttonstr-[1-13]\n"
BTNSTR_NEW = "_buttonstr-[1-18]\n"

LABEL_OLD = (("_label-5\n    lang=52\n    parent=_area2-1\n", "_label-5\n    text=Offset\n    parent=_area2-1\n"),
             ("_label-6\n    lang=53\n    parent=_area2-2\n", "_label-6\n    text=Hitposition\n    parent=_area2-2\n"))

# スライダー番号 -> (所属 area, 単位)。1..5 は元のスライダー、6 が Master。3 / 4(元の Hit offset / Graphic offset)は、モードごとのゲージに置き換えたので出さない
SLIDERS = {1: ("_area1-1", "%"), 2: ("_area1-2", "%"),
           5: ("_area2-7", "°"), 6: ("_area1-7", "%")}
OF_MODES = ["Key", "Catch", "Pad", "Taiko", "Ring", "Slide", "Live"]   # dylib/modeoffset.inc の OF_ROW と同じ順

def block():
    L = []
    # Master Volume の行
    L += ["_label-32", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=87,45", "    text=Master Volume", "    parent=_area1-7",
          "_slidebg-6", "    type=4", "    tex=ui-setting-bg0.png", "    part=24,86", "    size={slide_w},47", "    anchor=6",
          "    pos={right_x},22", "    zindex=3", "    etag=107", "    parent=_area1-7", "    tag=90",
          "_slide-6", "    type=106", "    tex=ui-setting-bg1.png", "    size={slide_w},47", "    anchor=6",
          "    pos=_slidebg-6 0,0", "    zindex=2", "    parent=_area1-7", "    tag=96",
          "_slidestr-6", "    type=1", "    fsize={f2}", "    anchor=4", "    pos={slide_x},45", "    zindex=4",
          "    text=100", "    parent=_area1-7", "    tag=97"]
    # osu! Store の行(最下段): 見出し + 既存と同じ見た目のボタン(tag 100)
    L += ["_label-33", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=87,45", "    text=Store server", "    parent=_area6-4",
          "_button-14", "    tag=100", "    parent=_area6-4",
          "_buttonstr-14", "    text=Malody / osu! (sayobot)...", "    parent=_button-14",
          "_label-34", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=87,45", "    text=BetterMalody", "    parent=_area6-5",
          "_button-15", "    tag=101", "    parent=_area6-5",
          "_buttonstr-15", "    text=Editor options...", "    parent=_button-15"]
    # Note speed(ツリー): 見出しの行(開閉ボタン tag 102 / 文字 tag 103)+ モードごとの行(tag 270+i = 行 / 280+i = 値 / 200+10i+j = ボタン)
    L += ["_label-35", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=87,45", "    text=Note speed", "    parent=_area6-6",
          "_button-16", "    tag=102", "    parent=_area6-6",
          "_buttonstr-16", "    text=Show...", "    tag=103", "    parent=_button-16"]
    NS_ROWS = ["Key", "Taiko", "Slide"]
    for i, name in enumerate(NS_ROWS):
        area = f"_area6-{7 + i}"
        L += [area, f"    tag={270 + i}",
              f"_label-{40 + i}", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=125,45", f"    text={name}", f"    parent={area}",
              f"_nsval-{i}", "    type=1", "    fsize={f2}", "    anchor=4", "    pos={right_x}+245,45", "    zindex=4", "    text=0.00",
              f"    tag={280 + i}", f"    parent={area}",
              f"_nshit-{i}", "    type=2", "    color=#ffffff", "    opacity=1", "    size=170,64", "    anchor=4", "    pos={right_x}+245,45",
              "    zindex=6", "    etag=107", f"    parent={area}", f"    tag={202 + 10 * i}"]
        for j, (dx, txt) in enumerate(((0, "-0.1"), (80, "-0.01"), (None, None), (330, "+0.01"), (410, "+0.1"))):
            if dx is None: continue
            L += [f"_nsb-{i}-{j}", "    type=4", "    tex=ui-cfg-btn.png", "    part=10,62", "    size=80,49", "    anchor=3",
                  f"    pos={{right_x}}+{dx},45", "    etag=107", f"    parent={area}", f"    tag={200 + 10 * i + j}",
                  f"_nsbs-{i}-{j}", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=40,24.5", f"    text={txt}", f"    parent=_nsb-{i}-{j}"]
    # Offset / Hitposition(ツリー): 元の 2 行(_area2-1 / _area2-2)を見出しにして、モードごとのゲージ行(_area2-10..16 / 17..23)を足す
    #   見出しのボタン tag 104 / 108(文字 105 / 109)/ 行 tag 300+10g+i / 当たり判定 320+ / バー 340+ / 値の文字 360+(g = 0 Offset / 1 Hitposition)
    for g, (hb, hs) in enumerate((("_button-17", "_buttonstr-17"), ("_button-18", "_buttonstr-18"))):
        L += [hb, f"    tag={104 + 4 * g}", f"    parent=_area2-{1 + g}",
              hs, "    text=Show...", f"    tag={105 + 4 * g}", f"    parent={hb}"]
    for g in range(2):
        for i, name in enumerate(OF_MODES):
            k = 10 * g + i; area = f"_area2-{10 + 7 * g + i}"
            L += [area, f"    tag={300 + k}",
                  f"_label-{50 + k}", "    type=1", "    fsize={f2}", "    anchor=3", "    pos=125,45", f"    text={name}", f"    parent={area}",
                  f"_ofbg-{k}", "    type=4", "    tex=ui-setting-bg0.png", "    part=24,86", "    size={slide_w},47", "    anchor=6",
                  "    pos={right_x},22", "    zindex=3", "    etag=107", f"    parent={area}", f"    tag={320 + k}",
                  f"_ofbar-{k}", "    type=106", "    tex=ui-setting-bg1.png", "    size={slide_w},47", "    anchor=6",
                  f"    pos=_ofbg-{k} 0,0", "    zindex=2", f"    parent={area}", f"    tag={340 + k}",
                  f"_ofstr-{k}", "    type=1", "    fsize={f2}", "    anchor=4", "    pos={slide_x},45", "    zindex=4",
                  "    text=0", f"    parent={area}", f"    tag={360 + k}",
                  f"_ofunit-{k}", "    type=1", "    fsize={f2}", "    anchor=3", "    pos={slide_x}+52,45", "    zindex=4",
                  "    text=ms", f"    parent={area}"]
    # 元の Hit offset / Graphic offset のスライダー(3 / 4)は隠す(タッチも dylib 側で捨てる)
    for n in (3, 4):
        L += [f"_slidebg-{n}", "    show=0", f"_slide-{n}", "    show=0", f"_slidestr-{n}", "    show=0"]
    # 値の単位 と 手入力の当たり判定
    for n, (area, unit) in SLIDERS.items():
        L += [f"_slideunit-{n}", "    type=1", "    fsize={f2}", "    anchor=3", "    pos={slide_x}+52,45", "    zindex=4",
              f"    text={unit}", f"    parent={area}",
              f"_slidehit-{n}", "    type=2", "    color=#ffffff", "    opacity=1", "    size=200,64", "    anchor=4",
              "    pos={slide_x}+30,45", "    zindex=6", "    etag=107", f"    parent={area}", f"    tag={79 + n}"]
    # 数値入力パネル(中央のモーダル)
    L += ["_cfgp", "    size=100%,100%", "    anchor=6", "    pos=0,0", "    zindex=200", "    etag=107", "    tag=99", "    show=0",
          "_cfgpdim", "    type=2", "    color=#000000", "    opacity=140", "    size=100%,100%", "    anchor=6", "    pos=0,0",
          "    parent=_cfgp", "    etag=107", "    tag=99",
          "_cfgpbg", "    type=5", "    tex=g-rightbg.png", "    rect=20px,144px,40px,80px", "    size=900,380",
          "    size2=0,1080px", "    anchor=4", "    pos=50%,50%", "    parent=_cfgp", "    etag=-2",
          "_cfgpt", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=50%,50%+110", "    zindex=2", "    parent=_cfgp", "    tag=93",
          "    text=Value",
          "_cfgpin", "    type=4", "    tex=g-input.png", "    part=10,50", "    parent=_cfgp", "    size=420,64", "    anchor=4",
          "    pos=50%,50%+10", "    zindex=2", "    etag=107", "    tag=94",
          "_cfgpit", "    type=105", "    fsize=32", "    size=410,64", "    tex=g-inputbar.png", "    size2=0,35", "    anchor=3",
          "    pos=5,32", "    zindex=2", "    etag=107", "    parent=_cfgpin", "    tag=94",
          "_cfgpok", "    type=4", "    tex=g-dialogbtn.png", "    tex2=g-dialogbtn2.png", "    part=7,46", "    parent=_cfgp",
          "    size=220,62", "    anchor=4", "    pos=50%-130,50%-100", "    zindex=2", "    etag=107", "    tag=91",
          "_cfgpcancel", "    type=4", "    tex=g-dialogbtn.png", "    tex2=g-dialogbtn2.png", "    part=7,46", "    parent=_cfgp",
          "    size=220,62", "    anchor=4", "    pos=50%+130,50%-100", "    zindex=2", "    etag=107", "    tag=92",
          "_cfgpokhit", "    type=2", "    color=#ffffff", "    opacity=1", "    size=320,100", "    anchor=4", "    parent=_cfgp",
          "    pos=50%-130,50%-100", "    zindex=3", "    etag=107", "    tag=91",
          "_cfgpcanhit", "    type=2", "    color=#ffffff", "    opacity=1", "    size=320,100", "    anchor=4", "    parent=_cfgp",
          "    pos=50%+130,50%-100", "    zindex=3", "    etag=107", "    tag=92",
          "_cfgpoki", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=110,31", "    parent=_cfgpok", "    text=OK",
          "_cfgpcani", "    type=1", "    fsize={f2}", "    anchor=4", "    pos=110,31", "    parent=_cfgpcancel", "    text=Cancel",
          "@if <1.44", "_slide-6", "    tex=ui-setting-bg2.png"] + [x for k in [10 * g + i for g in range(2) for i in range(7)] for x in (f"_ofbar-{k}", "    tex=ui-setting-bg2.png")] + ["@endif"]
    return "\n".join(L) + "\n"

def main():
    d = open(sys.argv[1], "rb").read()
    _, _, ents = skinpkg.parse(d)
    e = [x for x in ents if x["key"] == KEY][0]
    text = d[e["data"]:e["data"] + e["size"]].decode("utf-8")
    for old, new in ((AREA1_OLD, AREA1_NEW), (AREA2_OLD, AREA2_NEW), (AREA3_OLD, AREA3_NEW), (AREA6_OLD, AREA6_NEW), (BTN_OLD, BTN_NEW), (BTNSTR_OLD, BTNSTR_NEW)):
        assert old in text, old
        text = text.replace(old, new, 1)
    for old, new in LABEL_OLD:
        assert old in text, old
        text = text.replace(old, new, 1)
    text = text.rstrip("\n") + "\n" + block()
    out = skinpkg.replace(d, KEY, text.encode("utf-8"))
    open(sys.argv[2], "wb").write(out)
    print("config layout %d -> %d" % (e["size"], len(text.encode("utf-8"))))

if __name__ == "__main__":
    main()
