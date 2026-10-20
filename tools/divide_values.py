"""分割メニューの値(上から順)。レイアウト・テーブル・ラベル画像の単一の情報源。"""
VALUES = [2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 48, 64, 96, 128]
ROW_TOP, ROW_STEP = 640, 40        # レイアウト単位(画面高720)。16行 x 40
PANEL_H = 670
FONT = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf"

# UI 上の並び: 先頭に ▲/▼ (分割数 +1/-1)の2行、続いて VALUES。コード側の値表もこの並び(矢印行はセンチネル値)
ARROW_UP, ARROW_DN = 250, 251
UI_TABLE = [ARROW_UP, ARROW_DN] + VALUES

# 再生速度メニュー: 先頭に ▲/▼(+0.05/-0.05, 画像は分割数と共用)、続いてプリセット(% , 表示)
SPEED_ROWS = [(100, "1.00x"), (75, "0.75x"), (50, "0.50x"), (25, "0.25x")]
