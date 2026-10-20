# skin-ios.bin パッケージ形式 (4.3.7 iOS) — 確定

IDA `sub_1000C02E0`(読込) / `sub_1000C08B8`(extract) / `sub_1001E40B4`(キー生成) で確認。

```
header : "oiprocS\0"(8) | u32 version=1 | u32 count(=294)
entry  : marker 90 96 8f 8d 90 9c ac ff (= 0xFFAC9C908D8F9690 LE) | u32 keylen(=40) | key(ascii)
         | u32 size | u32 a(=1) | u32 b(=4) | data[size]
```
- **整合性検査は無い**(マジックとマーカーの照合のみ)。内容の改変・サイズ変更は自由。
- **key = SHA-1(ファイル名)の16進40桁**(小文字・名前そのまま。例: `script_edit_pc.mui`)。
- skin-ex.bin は別形式(マジック不一致)。未解析。
- 道具: `tools/skinpkg.py` (list / grep / dump)。

## 主なエントリ(名前→key先頭)
script_edit_pc.mui 3cb1cc2d (12,835B, **エディタUIレイアウト**) / script_edit_key.mui 4994d4d3 / script_edit_config.mui 15afca7d /
script_def.mui c5711adb / script_result.mui 8a30849e / script_review.mui 26e4c112 / script_sub_head.mui 520dedaf / script_loginbox.mui 63e7f444 /
editor-0.png 533cdf2b (111,382B, エディタ用アトラス) / ui1080-0.png f1687d9c

## レイアウト言語 (.mui, 平文 CRLF)
- `name` 行 + 字下げ付きの `key=value`。`[1-9]`/`[1,8,9]` で複数ノード一括定義、`$` は連番加算(`tag=400$1` = 400,401,…)。
- 主なキー: tex / type(2=矩形 4,5=9patch 109=テキスト) / text / fsize / anchor / pos / size / parent / tag / etag / zindex / show / react。
- 値は `{rightW}` などの定数参照、`100%-{rightW}-10` のような式。`@include script_edit_config.mui` で定数読込。

## 分割メニュー (script_edit_pc.mui)
```
_right-10 : size={rightW},100% anchor=8 pos=100%-{rightW}-10,0 show=0 zindex=40
_rightpanel-10 : type=5 tex=ui-edit-bg2.png size={rightW},430 anchor=8 pos={rightW}-2,2 parent=_right-10
_right-div-[1-9] : tex=ui-edit-div2.png size=0,26 anchor=4 pos=24,400$-45 parent=_right-10 etag=104 tag=400$1
  -2..9: ui-edit-div3/4/6/8/12/16/24/32.png   (ラベルは画像。アトラス内のフレーム名)
_rightpanel-11 : size={rightW},155 ; _right-spd-[1-3] pos=24,125$-45 tag=500$1 (speed1/75/5)
```
