# E6a: 分割メニューをスクロール可能なリスト(type=101)に / 1/128 まで拡張 (試作)

要望(ユーザー): スマホで要素が多いと細かくタップできない → 上下スライドで使えるように。1/128 まで拡張。

## 値
`2,3,4,5,6,7,8,9,10,12,16,20,24,32,48,64,96,128` (18項目, `tools/divide_values.py`)。
分割値は譜面 `extra.divide` に **unsigned byte** で保存され、符号付き読みは無い(`ldrsb` 0件を確認)ため 128 も安全。

## レイアウト (script_edit_pc.mui)
- `_right-dl`(type=101 リスト, size={rightW},680, size2=0,12, parent=_right-10, etag=104)を追加し、`_right-div-[1-18]` を **リストの子**にする(既存の情報画面 `_infolist` と同じ構造)。項目の座標はリストが決める。
- phone: `_right-dl size={rightW}+10,100%-24`, 項目 `size=0,40 react=20,20`(タップ領域を大きく)。

## コード
- ボタン収集(`0x100073BF0` の `bl sub_1001FDCAC`): `bl CAVE2`(`0x100005F00`)に差し替え。親 `_right-10` の **children[1](= リスト)** を渡して tail call。
- 強調 cave: ボタン0個なら何もせず `ret`(`cbz x12`)。値テーブル18要素、`cmp w8,#17`。

## 検証ポイント(未確認)
1. リストの行が表示され、上下ドラッグでスクロールするか
2. 行のタップで分割値が切り替わるか(etag/tag が list 内でも効くか)
3. 強調(青)が効くか = list の getChildren() に行が直接入っているか。入っていなければ強調だけ効かない(クラッシュはしない)。

ロールバック: `backup/skin-ios.bin.original` + `tools/apply_container.sh --restore` 系、または E5 を再生成(git 的な履歴は patches/ にある)。

## 追記 (E6c): ListSprite の仕組みと修正
- `ListSprite` は項目を**内部コンテナ `list+0x618`(=1560)**の子として持ち、配置は `sub_100048FF8(list, 0)`(→`sub_100048A2C` が子の高さを合算して上から並べる)で行う。情報画面 `_infolist` は初期化時にこれを呼ぶ。E6a/b は呼んでおらず、行が未配置(透明)だった。
- CAVE2 を更新: `children[1]`(= list) に対し `sub_100048FF8(list,0)` を呼んでから、**`list+0x618`(内部コンテナ)**を親として `sub_1001FDCAC`(ボタン収集)へ tail call。
- 手書きエンコードは lldb で確認済み。

## 結果 (2026-10-14): E6d で成功
- 行表示・選択(タップ)・スクロール・青い強調すべて動作(ユーザー確認)。
- 最終構成: skin = `_right-dl`(type=101, size2=0,14 / phone 0,20) + 順序指示行 + `_right-div-[1-18]`(tex=bm-div-N.png, 高さ30 / phone 40)。コード = E6c(CAVE2 で list を再レイアウトし内部コンテナを収集元に) + 値表18要素 + cave。
- 構成要素の対応: tools/divide_values.py → make_labels.py → build_skin_divide.py + make_divide_patch.py → apply_divide.sh。
