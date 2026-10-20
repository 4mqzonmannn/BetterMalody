#!/usr/bin/env python3
"""分割メニューのラベル画像(白・透過, 高さ80px)を汎用フォントで生成する。使い方: make_labels.py <出力ディレクトリ>"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont
from divide_values import VALUES, FONT, SPEED_ROWS

H, TARGET = 80, 52          # 画像高さ / 数字の高さ(px)
def font_for_height():
    lo, hi = 20, 120
    while lo < hi:
        mid = (lo + hi + 1) // 2
        f = ImageFont.truetype(FONT, mid)
        b = f.getbbox("1")
        if b[3] - b[1] <= TARGET: lo = mid
        else: hi = mid - 1
    return ImageFont.truetype(FONT, lo)

def main():
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    f = font_for_height()
    for v in VALUES:
        text = f"1/{v}"
        b = f.getbbox(text)
        w = (b[2] - b[0]) + 6
        im = Image.new("RGBA", (w, H), (255, 255, 255, 0))
        d = ImageDraw.Draw(im)
        y = (H - (b[3] - b[1])) // 2 - b[1]
        d.text((3 - b[0], y), text, font=f, fill=(255, 255, 255, 255))
        im.save(os.path.join(out, f"bm-div-{v}.png"))
    for name, sign, up in (("up", "+1", True), ("dn", "-1", False)):   # 分割数 +1/-1 の行(三角 + 文字)
        text = sign
        b = f.getbbox(text)
        tw = b[2] - b[0]
        w = 40 + tw + 12
        im = Image.new("RGBA", (w, H), (255, 255, 255, 0))
        d = ImageDraw.Draw(im)
        cy = H // 2
        tri = [(4, cy + 14), (36, cy + 14), (20, cy - 16)] if up else [(4, cy - 14), (36, cy - 14), (20, cy + 16)]
        d.polygon(tri, fill=(255, 255, 255, 255))
        d.text((44 - b[0], (H - (b[3] - b[1])) // 2 - b[1]), text, font=f, fill=(255, 255, 255, 255))
        im.save(os.path.join(out, f"bm-div-{name}.png"))
    for pct, text in SPEED_ROWS:                                       # 再生速度のプリセット
        b = f.getbbox(text)
        w = (b[2] - b[0]) + 6
        im = Image.new("RGBA", (w, H), (255, 255, 255, 0))
        d = ImageDraw.Draw(im)
        d.text((3 - b[0], (H - (b[3] - b[1])) // 2 - b[1]), text, font=f, fill=(255, 255, 255, 255))
        im.save(os.path.join(out, f"bm-spd-{pct}.png"))
    for name in ("x", "minus", "plus"):                                  # 情報画面の小ボタン(枠 + 記号)。ゲームのフォント依存を避けて画像化
        W, Hh = 120, 72
        im = Image.new("RGBA", (W, Hh), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((3, 3, W - 4, Hh - 4), radius=14, fill=(40, 40, 40, 230), outline=(255, 255, 255, 255), width=4)
        cx, cy = W // 2, Hh // 2
        if name == "x":
            d.line((cx - 14, cy - 14, cx + 14, cy + 14), fill=(255, 255, 255, 255), width=7)
            d.line((cx - 14, cy + 14, cx + 14, cy - 14), fill=(255, 255, 255, 255), width=7)
        else:
            d.line((cx - 16, cy, cx + 16, cy), fill=(255, 255, 255, 255), width=7)
            if name == "plus": d.line((cx, cy - 16, cx, cy + 16), fill=(255, 255, 255, 255), width=7)
        im.save(os.path.join(out, f"bm-info-{name}.png"))
    import math
    for name, flip in (("undo", False), ("redo", True)):               # 右バーの取り消し / やり直しアイコン(98x80, 既存アイコンと同じ白・太線)
        S = 4; W, Hh = 98 * S, 80 * S
        im = Image.new("RGBA", (W, Hh), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        cx, cy, r, wd = 49 * S, 44 * S, 25 * S, 9 * S
        box = (cx - r, cy - r, cx + r, cy + r)
        d.arc(box, 200, 360 + 70, fill=(255, 255, 255, 255), width=wd)      # 左下から上を回って右下へ(時計回り)
        ang = math.radians(200)                                            # 始点側に矢じり(反時計回りの戻る矢印)
        px, py = cx + r * math.cos(ang), cy + r * math.sin(ang)
        tang = ang - math.pi / 2                                          # 始点での進行方向と逆(外向き)
        tip = (px + 22 * S * math.cos(tang), py + 22 * S * math.sin(tang))
        a = math.pi / 2
        b1 = (px + 18 * S * math.cos(ang) , py + 18 * S * math.sin(ang))
        b2 = (px - 18 * S * math.cos(ang) , py - 18 * S * math.sin(ang))
        d.polygon([tip, b1, b2], fill=(255, 255, 255, 255))
        if flip: im = im.transpose(Image.FLIP_LEFT_RIGHT)
        im = im.resize((98, 80), Image.LANCZOS)
        im.save(os.path.join(out, f"bm-{name}.png"))
    # プラグイン(コンセント): 差し込み 2 本 + 本体 + コード(98x80, 白・太線)
    S = 4
    im = Image.new("RGBA", (98 * S, 80 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    d.line((36 * S, 10 * S, 36 * S, 30 * S), fill=w, width=9 * S)
    d.line((62 * S, 10 * S, 62 * S, 30 * S), fill=w, width=9 * S)
    d.rounded_rectangle((22 * S, 28 * S, 76 * S, 54 * S), radius=10 * S, outline=w, width=8 * S)
    d.line((49 * S, 54 * S, 49 * S, 72 * S), fill=w, width=9 * S)
    im.resize((98, 80), Image.LANCZOS).save(os.path.join(out, "bm-plugin.png"))
    print("labels:", len(VALUES), "+2 arrows +", len(SPEED_ROWS), "speed, font size:", f.size)

if __name__ == "__main__":
    main()
