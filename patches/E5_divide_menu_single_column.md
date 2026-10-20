# E5: 分割メニューを「1列・全項目 汎用フォント画像」に(E4 を置換)

経緯: E4 の 2 列目はテキストノード(type=1)で表示はされたが、**タップに反応しなかった**(ユーザー確認)。
ユーザー要望: 2列ではなく長い1列に / 角ばった独自フォントは使わず汎用的に。

## 仕様
- 値(上から): `2,3,4,5,6,7,8,9,10,12,16,20,24,32,48,64`(16項目、昇順)。単一情報源=`tools/divide_values.py`。
- 既存9項目も含めて**ラベルを汎用フォント(DIN Condensed Bold, macOS標準)の画像に統一**。新ラベルは `bm-div-<n>.png` として skin パッケージへ単体PNGエントリで追加(`tex=` から参照可。`bga-fixed.png`/`icon-ma.png` と同方式)。
- 既存と同じスプライトボタン方式(`tex=`+`etag=104`+`tag=400$1`)なので、**タップ判定と青の強調(色変更)は従来の仕組みのまま**。
- レイアウト: `_right-div-[1-16]`(size=0,26, pos=24,640$-40)、パネル高 670(`_rightpanel-10`)。phone 用上書きも 16 項目(pos=30,685$-43)に更新。
- コード: 値テーブル 16 要素(`0x100005E00`)、`sub_1001FDF90` の範囲 `cmp w8,#15`、強調 cave の探索数 `cmp w10,#16`(E4 と同構造)。

## 生成/適用
- `tools/make_labels.py` → `tools/build_skin_divide.py <元skin> <labels> <out>` → `tools/make_divide_patch.py <現在のバイナリ>`(差分のみ出力) → `tools/apply_divide.sh`(アプリ終了を待って適用)。
- 既存項目の画面上の位置は変わる(1/2〜1/32 の並びは保たれ、間に新項目が入る)。タップ位置の筋肉記憶に影響するため、ユーザー指示に基づく変更。
- ロールバック: `backup/skin-ios.bin.original` をコンテナへコピー + `tools/apply_container.sh --restore` または `new:old` 逆指定。
