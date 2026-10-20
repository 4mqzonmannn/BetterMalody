# F1: effect マネージャ + エディタ反映(プレビュー) — dylib 方式

## 構成
- `dylib/bm.m` → `BetterMalody.dylib`(PlayCover用 macabi。実機用は iphoneos SDK で別ビルド要)。`tools/apply_dylib.sh` が build / skin(FXボタン) / optool / フックパッチ / 再署名。
- フック: `tools/make_hook_patch.py` の HOOKS。ケイブ(24B)をヘッダ余白に置き、`__DATA`末尾スロット(0x1016CF800+8*i)経由で dylib 関数へ。slot==0 なら元処理(dylib無しでも安全)。
  touch(0x100075E60) / Key: update 0x1001B0AF0, zoom 0x1001B10C0, touch 0x1001B1804 / Slide: update 0x1000C5408, zoom 0x1000C5AB0, touch 0x1000C6744。

## 機能
- FXボタン(tag700, 左上): UIKit一覧。追加(現在位置beat同期)/編集/削除/負のbeat。編集中データは editor+0x2d8 の deque(effect: +680)。
- Preview: 表示のみ。Key/Slide。G(beat)=∫scroll + jump*scroll_new/beatLen。補助線・ラベル・hold・slide経路を G で配置。編集操作は無効。Scale(0.01〜1)。
- slide 経路はプレビュー中スプライトを隠し、DrawNode に縁線で描く(座標を現在位置基準に保つため)。

## 検証済み
- mourning(_song_40104/03.mc, mode7, 93845点)で補助線・経路が流れることを確認(2026-10-15)。

## 既知の制約 / 未実装
- 経路は輪郭線のみ(塗りなし)。hs は未反映。Key 以外の他モード(Taiko/Catch/Pad)は未対応。
- 実機(iPhone)ビルド・Android 未対応。

## 追記(2026-10-15): UI の最終形
- 左のFXボタン(tag700) = プレビュー ON/OFF のみ(表示「FX」⇄「FX ON」)。
- 右ツールバー2番目(BPM, tag18) → 「その他」ポップアップ(_right-more)を一時的に [BPM(702)/Effect(701)] へ差し替え、選択後に元の項目へ復元(項目はretain/removeFromParentで退避)。
- Effectパネル `_fxpanel`(tag38) は `_bpmpanel` の複製(skin: tools/build_skin_fx.py FXPANEL/FXPANEL_PHONE)。UI初期化 sub_100073140 をフックし layout から名前でノード取得(`sub_100155BB0(layout,std::string)`)。
  - 行生成=sub_1000775DC と同じ手順(list->clear=sub_100048BF4, 行=sub_100544A64, 背景=sub_1001F38B0, ラベル=sub_1001F3608)。行タップ=BPMリストと同じ functor vtable(0x1008F1BD8)に自前関数を登録。
  - 入力は「種類ボタン(Scroll/HS/Jump/Sign)+値1つ」。effect は1要素1種類が前提。値入力タップ=UIAlert。
  - 閉じる=パネル自体のタップ(tag38)。閉じ処理は BPM と同じ(slide-out sub_1005921E8 / 右メニュー復帰 sub_1005470B4+sub_10053CF04 / a1+902=1)。
