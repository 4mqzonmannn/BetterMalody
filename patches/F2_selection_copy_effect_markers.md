# F2: 選択操作 / コピペ強化 / effect 範囲操作 / effect マーカー — dylib 方式 (2026-10-15)

すべて `dylib/bm.m`。追加項目は「その他」(_right-more, editor+1400) のポップアップに入る。Key / Slide 共通(lay_of で判別)。

## 項目
| tag | 項目 | 内容 |
|---|---|---|
| 710 | Select all | 表示中の全ノートを選択(選択ベクタ: Key +1592 / Slide +1560、ハイライト sub_1001B2A9C / sub_1000C7BB4) |
| 711 | Invert selection | 選択反転。標準はタッチ開始で選択解除するため keytouch フックで直前の選択を控える(30秒) |
| 712 | Deselect | 全解除(sub_1001B1BDC / sub_1000C5954) |
| 713 | Copy selection | 選択ノートを clone(vtable+16) して **標準クリップボード(editor+840)** へ。標準 Paste がそのまま使える |
| 715 | Paste mirrored | 標準クリップボードを左右反転して現在位置へ貼る。Key=レーン反転, Slide=x→255-x, パスの seg offset 反転 |
| 716 | Copy effects in range | 範囲(選択ノートの最初の開始〜最後の終了。無ければ標準範囲コピーの範囲)内の effect を保持 |
| 717 | Paste effects here | 現在位置へ相対位置を保って貼る(同拍は置換) |
| 718 | Delete effects in range | 範囲内の effect を削除 |
| 719 | Effect markers | FX プレビューOFFでもマーカーを常時表示するトグル |

## ノート挿入手順 (標準の追加処理を再現)
- Key: chart(+1624) `sub_1001C6834` / `sub_1001B395C`(sprite) / `sub_10006C1EC`,`288`(音) / hold: `sub_1001A32D8` / `sub_1001A3174`(layer+1112) / vtable+1648(undo)
- Slide: chart(+1592) `sub_1001C6834` + path? `sub_1000C88C8` : `sub_1000C8A68` + `6C1EC/288` + vtable+1648
- Beat 書込: `sub_1001F2EC4(dst, m|num<<32, den)`。ノート +64 開始 / +88 終了、effect +8。

## Effect マーカー (UIKit オーバーレイ)
- レーン外側(余白の広い側, 左優先)に `SC 2 HS 1.5 ▶`。上部に現在位置の有効値。FX プレビュー ON または Effect markers ON で表示。
- 座標: editor 空間 y = contY + K*(G(b)-gG0) - off、画面 = (H0 - y) * (windowH / H0)。毎フレーム update フック末尾で更新。

## 注意
- 標準 Cut 後の Paste mirrored は元ノートを消さない(標準 Cut の移動挙動は未確認)。
- 実機ビルド・Android 未対応。

## 追記 (2026-10-15 後半)
- OS 非依存化: UIKit は一切リンクしない(Foundation のみ)。メッセージ=ゲーム標準トースト `sub_1001D15D0(mgr, std::string*, level, ms)`、Effect マーカー=editor ノードへ追加したゲームのラベル(`sub_1001F3608(fontH)` + `sub_100569DF8`)、値入力=ゲーム標準 Textbox(`sub_10006DA24`)。画面比は実行時 KVC(keyWindow.bounds / screen.scale)。★GL ビューポート(glGetIntegerv)は FX 押下で落ちる(コンテキスト無効)。
- 選択系の追加: Apply width slider(720, Slide: note+129=幅, +128=中心, sub_100167D8C/sub_1000AA59C+sub_1000C7CE8), Move selection later/earlier/left/right(723-726, 標準ドラッグ移動と同じ後処理), Lasso mode(729, keytouch/s_touch フックでなぞりを消費 → 多角形内を選択)。
- 分割数 +1/-1: 既存の分割メニュー(リスト先頭)に ▲/▼ 行(skin: `_right-divup/_right-divdn` tag=727/728)。値表 UI_TABLE=[250,251]+VALUES。★値表(20要素=80B)は 0x100005D00 に置く(0x5E00 だとフックのケイブ 0x5E48〜と重なって末尾が壊れる)。
- 長押し選択解除: 標準がタップで解除するため不要。

## 追記: 再生速度メニュー拡張 (2026-10-15)
- 標準: タグ500=100% / 501=75% / 502=50% を `sub_1001FDDC0` が返し、chart+498(byte)=% を保存、`sub_1001F5A84(editor+720, %/100)`、editor+704 の +168 に float 保存。
- 拡張: スキンで `_right-spd-[1-6]`(▲/▼ + 1.00/0.75/0.50/0.25, タグ 730..735)。全タグを dylib が処理(▲▼=±5%, 下限10%/上限100%)。強調は自前: toolbar(editor+1304: +8 begin/+16 end)の各ボタンを vtable+1080 で着色(青 = sub_1005545B8(2,206,255) / 通常 word_1016CC9F8)。
- 値の画像: `tools/make_labels.py`(bm-spd-*.png)。パネル高さは `build_skin_divide.py` の speed_block / speed_phone_block。

## 追記: 「その他」メニューの分類 (2026-10-15)
- 追加項目が多すぎるため、標準項目の後ろにはカテゴリ項目 5 つだけを追加: `Select >`(760) / `Copy / Paste >`(761) / `Move >`(762) / `Effect >`(763) / `Spectrogram >`(764)。
- カテゴリ押下 → ポップアップを `menu_show_items(self, MItem[], n)` で一時的に差し替え(標準項目は retain して退避、`menu_restore()` で復元)。各サブメニュー末尾に `< Back`(765)。
- サブメニュー項目(710-720, 723-729, 736-739)を押すと処理後に非同期で `menu_restore()`。分割(727/728)・速度(730-735)はそれぞれの標準メニュー内。
- オフセット自動適用は不要のため削除(Check offset は提案値の表示のみ)。
