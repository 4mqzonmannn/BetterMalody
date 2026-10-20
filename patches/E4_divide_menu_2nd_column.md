# E4: 分割メニューに2列目(標準外の分割値)を追加

無印の既存9項目(1/2〜1/32)は位置・見た目とも不変。左隣に2列目(9項目)を追加する。

## 追加する値
`5, 7, 9, 10, 20, 40, 48, 64, 96`  (tag 409〜417)。`tools/make_divide_patch.py` の VALUES と `tools/build_skin_e4.py` の VALUES を一致させること。

## レイアウト (skin-ios.bin / script_edit_pc.mui)
- `_right-div-9` の後ろに `_rightpanel-12`(背景パネル: size={rightW},430, pos=-2,2)と `_right-divx-[1-9]`(type=1 テキスト, tag=409$1, pos=-26,400$-45)を追加。
- 生成: `tools/build_skin_e4.py <元skin-ios.bin> work/e4/skin-ios.bin`(パッケージの entry サイズ欄を更新。整合性検査は無いので差し替えのみ)。
- ★要検証: type=1 のテキストが `text=` / `etag=104,tag=` でタップ可能か。ダメなら type=2 のヒット矩形 + 子ラベルに変更。

## コード
| 場所 | 変更 |
|---|---|
| `0x100005E00` (ヘッダ余白) | 新値テーブル 18 dword = [2,3,4,6,8,12,16,24,32,5,7,9,10,20,40,48,64,96] |
| `0x100005E80` (ヘッダ余白) | 強調 cave: 値でテーブル検索→ボタンベクタ[idx](ベクタ長ガード付き)→`b 0x1001FDF08` |
| `0x1001FDE70` (`sub_1001FDE70` 先頭) | `b 0x100005E80` (旧: `sub w8,w1,#2`) |
| `0x1001FE014` | `cmp w8,#8` → `cmp w8,#17` (tag-400 の範囲) |
| `0x1001FE01C/E020` | `adrp/add` を新テーブルへ(旧: `0x10080AFA0`) |
| `0x10080AFA0` | E1 を戻す(5 → 2) |

## 動作
- タップ(tag 400〜499): `0x100076614` が `sub_1001FDF90`(tag→値)を呼び、値を `sub_100021814`(エディタ)・`sub_1001A2C7C`(タイムライン)・譜面meta+0x1F1 に設定。tag 409〜417 は範囲(≤99)内で既存経路にそのまま乗る。
- E3(丸め無効化)と組み合わせ、保存・再読込でも値が保たれる。

## 適用 / ロールバック
- 適用: `tools/apply_e4.sh`(アプリ終了を待つ。skin は `backup/skin-ios.bin.original`、バイナリは `backup/MalodyMobile.bin.*` を自動作成)。
- ロールバック: `tools/apply_container.sh --restore`(バイナリ) + `backup/skin-ios.bin.original` をコンテナへコピーして再署名。

## 修正履歴
- 2026-10-14 17:01: 強調 cave の `ldp x8, x12, [x0, #8]` のエンコード誤り(imm7=0 → [x0+0]=vtable を読み、起動直後 `0x1fdf34` で EXC_BAD_ACCESS)を修正。`0x100005EA8`: `08 30 40 a9` → `08 b0 40 a9`。lldb の逆アセンブルで cave 全 19 命令を確認済み。
- 教訓: 手書きエンコードは必ず `lldb -b -o "disassemble -s <VA> -c N" <binary>` で独立検証する(`tools/make_divide_patch.py` 生成後)。
