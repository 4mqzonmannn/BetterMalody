# BetterMalody TODO (2026-10-15 時点)

## 進行中 / 確認待ち
- [x] 譜面置き場を Documents 直下へ移行(ファイルアプリで編集可能に) — 確認済み(2026-10-15)。ロールバック: `backup/beatmap-20261015-165804`
- [ ] Lasso / 選択系 / Effect 範囲操作 / 速度・分割メニュー — 動作確認済み(速度・分割・マーカー・コピペ)。Lasso も確認済み。

## 次の候補(優先度は相談)
- [x] 情報画面に Video / BGA / Skin を追加(タップ→ファイル選択シート→譜面フォルダへコピー、macOS で動作確認 2026-10-16)
- [ ] 情報画面の拡張: 解除(空にする)操作、bar_begin(小節番号の開始値) / speed(既定スクロール速度) / column(キー数) の編集、Cover file(3007 の文言は存在)
- [ ] 実機(iPhone)向け IPA ビルド(元 IPA へパッチ再適用 + dylib を platform 2 化 + 再パッケージ。署名は利用者側)
- [x] **スペクトログラム表示**(エディタのレーン上に半透明) — 完了(2026-10-15)。設計/実装メモは `docs/SPECTROGRAM.md`。
  - [ ] 拡張案: アプリ内オフセット検証(アタックと拍グリッドのズレを集計して +ms を提示/適用)、BPM 検出補助、周波数範囲・コントラスト設定、起動時 ON の記憶
  - [x] Python 試作(tools/spectrogram.py): STFT→対数96帯→カラーマップ→拍へ再サンプリング+offset check(2026-10-15)
  - [x] 実装済み(2026-10-15): ogg=stb_vorbis(dylib同梱)/FFT=vDSP/タイル→Texture2D(sub_100578504)→Sprite(new 0x3E0+ctor 0x10056ADCC+vtable+1344)→editor+1472 の背面。既定濃さ78%・保存。★LUT初期化忘れで全透明になった
  - [ ] OGG デコード方式の確定(ゲーム内の vorbis デコーダ呼出 or dylib 側で実装 + vDSP FFT)
  - [ ] cocos2d のテクスチャ/スプライト生成関数の特定(Texture2D::initWithData 等)
  - [ ] 背面レイヤーへの配置とスクロール同期(FX プレビュー時の G 変換対応)
  - [ ] ゲーム内メニュー(ON/OFF・濃さ・周波数スケール)
- [ ] 再生速度の連続可変(Rubber Band 相当の時間伸縮。現状は 0.10〜1.00 を 0.05 刻み)
- [ ] 譜面フォルダのアプリ内管理画面(一覧・リネーム・複製・削除・mcz 書き出し)
- [ ] Slide⇄Wipe 変換(選択ノートのみ)
- [ ] マーカーの色分け(負 scroll=赤)— ゲームのラベル色指定関数の特定が必要
- [ ] プレイ画面の改善(判定表示・見た目・速度・リザルト等。要望の整理から)
- [ ] 実機(iPhone)向けビルド(iphoneos SDK)と導入手順
- [~] **Malody V のプラグイン(Lua)対応**(v1 動作確認済み(2026-10-16): 管理パネル=BPM ボタンの選択メニュー、置き場=Documents/export_plugins: docs/LUA_PLUGINS.md) — 4.3.7 無印に Lua ランタイムを組み込み、V 用エディタプラグインを動かす。調査: 無印に Lua は無い(別途 Lua 5.x/LuaJIT を dylib 同梱)、V のプラグイン API(Note/Chart/GetSelectNotes 等。`reference_malodyv_editor_plugin_internals` メモ参照)の無印への写像、実行 UI(その他メニュー)、ファイル配置(Documents/plugin/)。リポジトリ(sekanndopuro-code/test)にも Lua プラグインランナーあり
- [ ] Android(libmalodycpp.so)への移植

## 完了(要約)
「その他」メニューのカテゴリ分類 / スペクトログラム(濃さ・保存・オフセット検証) / 譜面置き場の Documents 直下化 /
Effect マネージャ+プレビュー+マーカー+範囲操作 / 全選択・反転・解除・Lasso / コピー・ペースト・反転ペースト / 幅の一括変更 / 選択ノート移動 / 分割数 ▲▼ / 再生速度 ▲▼+プリセット / OS 非依存化(UIKit 不使用)

## 2026-10-16 追加(情報画面の拡張 / クオンタイズ) — 実装済み・動作確認済み(2026-10-16)
- 情報画面: Video/BGA/Skin 行の右端 [X] で解除(空にする)。Bar begin(meta+396, Key/Taiko)・Speed(meta+340, Catch)を [-][+] で増減。対象外の譜面種別は「-」表示。
- 「その他 > Move > Quantize to grid」: 選択ノートを現在の分割数へ丸める(hold の終端・Slide の seg も)。
- 見送り: column(キー数)の変更 = 既存ノートのレーンが範囲外になるため。Cover は譜面メタに無く曲単位のファイル(cover.jpg 等)。
- Lua 次段(Interact 型は 2026-10-16 完了): StartBatch/FinishBatch の Undo 一括、Key 用プラグインでの検証、AddNote の雛形不要化(ctor 呼び出し)、Sprite/Text モジュールの描画。

## 2026-10-16 取り消し / やり直し — 動作確認済み
- 右バー(下から 3・4 番目。メイン / ツール / メニューの全パネル)に Undo / Redo。標準エディタに Undo は無いため自作(dylib/undo.inc)。
- 方式: ノートごとの「影」(clone)と現在の譜面を、最後の操作の 0.35 秒後に比較 → 追加/削除/変更を 1 履歴にまとめる(上限 40)。復元は標準の削除/追加手順。
- ノート一覧の取得: chart+184 は Slide では空 → ノートコンテナ(editor+1472)の子の userData から列挙(重複除去 + vtable 範囲検査)。
- 未対応: BPM の編集の取り消し(effect とノートのみ)、履歴の永続化。

## 2026-10-16 Lua 強化(動作確認済み)
- 描画: AddText/AddSprite/FindModule/RemoveModule(modules.inc)。X%/Beat/Width%/Height/HeightBeat/Opacity/Visible/Text。
- 編集: AddEffect/DeleteEffect/AddTime/DeleteTime、SeekTo/Beat、CallShortcut(7/8/10/11/12/13/16/18/19)。
- ゲーム内インポート: Plugins パネルの Import(ファイル選択シート → Documents/export_plugins)。zip 展開は未対応。
- Undo 拡張: BPM / 情報画面(title 等)も取り消し可。★std::string 戻りは x8(sret)で渡す(IDA の引数表示を鵜呑みにしない)。
- 日本語文字列の描画クラッシュ(Catalyst の sizeWithFont 異常値 → bzero(NULL,負))は _bzero スタブの差し替えで無害化。
- 未対応: parent(親モジュール)、ReadBytes/DoRequest/OnKey、zip プラグイン、Key 用プラグインでの検証。

## 2026-10-16 GetUserInput / Key / ES3(実装済み・実機確認待ち)
- GetUserInput: プラグインパネルに入力欄(`_pltext-3` 題 / `_plit-1` Textbox / `_plb-3` OK, tag 333/334)。コールバック方式のため Run 終了後も Lua 状態を保持し、OK で `lh_input_deliver`。連鎖(コールバック内で再度)・複数待ちはキュー。パネルが閉じていれば自動で開く。
- Key モード: AddNote を雛形不要に(sub_1001EB7EC(this) + sub_1001EB82C の手順で HitObjectKey を直接生成, new(0x88))。
- module:SetColor(r,g,b)(ES3 の Draw/Edit/Lift/Help が必須)。vtable+1080(マーカーの色付けと同じ)。
- ゲーム無しの検証: `tools/lua_harness.sh <0|7> plugin.lua...`(モック譜面で Run/Interact を通し、未対応 API・実行時エラーを洗い出す)。ES3 11本・既存 Slide プラグイン全て 0 エラー。
- Key テスト用: `work/plugins_src/key/`(stairs / shift / to_hold / mma_editor)。ES3 は画像 `editor-es3-fill.png` を譜面フォルダに置く(テスト用に `_song_35794` へ配置済み)。
- 方針変更: スペクトログラムのキャッシュは TODO から除外。
- 実機確認済み(2026-10-16): GetUserInput(連鎖・同値の再入力)/ Key の AddNote・SetNoteX・Tap→Hold(同レーンの次ノーツ手前で終端を詰める)。
  ★Key のノート一覧は chart+184 ではなく容器の子スプライトから(chart+184 は件数が合わず CFDictionary で落ちた)。
  ★Textbox は編集中だと tb_set が表示に出ず打鍵が追記される → sub_10006D2E0(tb) で編集を終えてから設定。
  ★Key の Tap→Hold は見た目が別構造 → Tap を消して Hold を新規挿入(ID は引き継ぎ)。ノートの ms は time_of(n+56 / n+80) で拍から求める。
- ES3 実機確認済み(2026-10-16): Check/Config/Draw/Edit/Lift。ShowTip=編集領域上端の常設 1 行(tip_set/tip_update, modules.inc)。ShowMessage は同文 1 秒抑制。
  画像は 譜面フォルダ → export_plugins → 白 4x4 の順に探す。Interact イベントはノート収集を遅延(pl_need)。
- Lua 強化 その2(2026-10-16): ReadBytes / Read・WriteChartCustom / DoRequest+OnResponse / print / os(time,clock,date) / Alpha 0〜100 修正 / Scale / Review 結果の常設表示。トースト表示時間 2.5s→4.5s(BM_TOAST_MS)。実機確認待ち。
- 実機確認済み(2026-10-16): DoRequest→OnResponse(example.com の HTML が返る)/ ReadChartCustom・WriteChartCustom(再実行で値が残る)/ os.time・os.date / SetColor・Alpha・Scale / トースト 4.5s。
- zip インポート(2026-10-16 実機確認済み): Import で zip → export_plugins/<zip名>/ に構成ごと展開(dylib/zipx.c: store/deflate, パス検査, GB18030 名変換, 共通最上位フォルダ除去)。フォルダ選択も構成ごとコピー。スキャンは深さ 4 まで再帰。素材画像は 譜面フォルダ→プラグインの隣→export_plugins の順。
  残り候補: GetUserOption/GetUserSelect, 親モジュール, 譜面フォルダ管理, 実機 IPA 化。
- 2026-10-16 その3(実機確認待ち): GetUserSelect/GetUserOption(パネル内ボタン)、親モジュール、エディタ「その他>BPM メニュー」に Folder(曲フォルダを Finder/ファイル App で開く)。
  曲選択後の譜面選択メニューからも開けるようにする件は、対象メニューの特定待ち(スクリーンショット)。
- 曲フォルダを開く(2026-10-16): エディタ BPM メニューの Folder + 曲選択画面「何をしますか?」の「曲のフォルダを開く」。
  ★元アプリは lang 101「曲のフォルダを開く」/ id 208 の処理(sub_10010A114 case 208)を持つが、ボタン未追加・実処理 nullsub_48(0x100020120)が空 = デスクトップ版のみ。
  nullsub_48 をフック(slot10)して Finder/ファイルApp を開く実装を入れ、ボタン追加は sub_10017E3D8(slot12, id==202 の直後に id 208)、言語表は sub_1000B5980(slot11)で控える。
  開く順: NSWorkspace openURL(Mac) → UIApplication openURL(file / shareddocuments://) → フォルダ選択シート。
- ダイアログ UI(2026-10-16): GetUserSelect/GetUserOption と数値入力(GetUserInput の既定値が数値のとき)をゲーム標準の DialogOptions で表示(pl_dlg_open: sub_10001BAB0 + sub_10017E3D8 + 関数オブジェクト off_10091A088 + scene->addChild(504,z=100000))。
  GetUserOption は拡張で表(最大 32 択)も可。数値は OK / ±1 / ±10 / ±100 / Keyboard...(パネルの入力欄)。文字入力は DialogOptions に入力部品が無いためパネルの Textbox のまま。実機確認待ち。
- ダイアログ UI / 曲フォルダを開く / 親モジュール / GetUserSelect・Option: 実機確認済み(2026-10-16)。
- 汎用スライダーダイアログ(2026-10-16 実機確認済み): skin の _sdpanel(script_editpause.mui のフォークを 760x300 に縮小、ゲージ最大 2 本)。LineProgressSprite の割合は +1012(0..100)、更新は sub_100166D10(bar, pct)、タッチ位置→割合は sub_100166E3C(bar, float *pos)(★pos はポインタ)。
  ゲージ上のタッチは hook_touch で return 1 して移動イベントを受ける。sd_open(ed, rows[{label,min,max,val,step}], n, onChange, onDone, onCancel)。使用例: スペクトログラム濃さ。
  Effect 種類選択はダイアログ(DialogOptions)。今後: Lua の数値入力(範囲指定)、幅/速度などの設定への展開。
- Lua スライダー / エディタ設定のゲージ化(2026-10-16 実機確認済み): Editor:GetUserSlider(title,min,max,default[,step],cb) / GetUserSlider2(...)(BM 拡張)。Move > Width...(Slide)/ Quantize...(任意分割で量子化)。Grid divide は不要のため削除。

## 2026-10-16 プラグイン仕様の精査(V との比較)・実機確認待ち
- 確認用: `work/plugins_src/key/compat_test.lua`(Interact。ON でトースト 'Height=… X=… W=…'、赤帯ではなく ES3 の塗り画像 2 本=Rotate 30°、GetUserInput 既定 'abc'、キー入力でトースト、レーンをタップでファイル選択)。
- 確認したいこと: ①Height(確認済み: 158.69) が 0 でなく数値(HeightBeat 1/4 拍相当)で出る ②ct-b が傾く(確認済み 2026-10-16: sub_100543EBC の直接呼びで 30° 時計回りに傾いた) ③BezierSlide の補助線が斜めになる ④NoteBeatScaler の入力欄に既定値が入る ⑤Img2Wipe のファイル選択 ⑥ES3-Draw の数字キー。
- 未確認(静的に確定できない): Key モードの GetClickBeat は表示ずれ補正(editor+1684)を含まない可能性。Slide の x 範囲(0〜255 と 100*x/256)の端の誤差。

## 2026-10-16 プラグイン互換の精査 結果(実機確認済み)
- 確認済み: モジュールの値の読み出し(Height 等)/ Rotate(Node::setRotation=sub_100543EBC を直接呼ぶ)/ GetUserInput 2 引数形(数値=既定値・文章=見出し)/ "(0/1)" 型の見出しは選択ダイアログ /
  NoteBeatScaler / BezierSlide(Interact)/ BezierWipe。画像が無いモジュールは透明。
- ★クラッシュ修正: Slide の節を編集してスプライトを作り直す際、選択リスト(editor+1560/+1568)に古いスプライトが残り、次の選択解除(sub_1000C5954)で落ちた。作り直し前に選択から外し、後で再選択する。
- ★選択: 一覧のタップで標準の選択が解除されないよう、一覧領域のタッチは標準の処理へ渡さない(縮小しない幅でも)。プラグインの選択取得は sel_effective(控え 30 秒)。
- 確認済み(追記): OnKey(Mac キーボード)/ ReadBytesSelect。
- 解決: Key の GetClickBeat は標準のタップ配置(sub_1001B1D9C)も ofs(editor+1684)を含めない=現状どおりで正しい。標準は「分割幅の切り捨て」なので GetClickBeat も切り捨てに変更(Free は最寄りの 1/32)。エディタに譜面が出ない症状(プレビュー往復で復帰)は 1 回のみ。
- 修正(2026-10-16): 分割メニューの行タグが 400 始まりで、値表(先頭に ▲▼ のセンチネル 2 つ)とずれ、2/3 の行が 250/251 分割になっていた → build_skin_divide.py のタグを 402$1 に(stock の tag->値 = 表[tag-400])。実機確認済み。

## 2026-10-16 設定画面の改善(実装済み・実機確認待ち)
- 構成: script_config.mui(スキン)を tools/build_skin_cfg.py で拡張 + dylib/settings.inc(フック 13〜16: cfgtouch=sub_100019B8C / cfgbuild=sub_1000174F8 / keydlg=sub_10001BDA8 / setvol=sub_1001F50B4)。
- Master Volume: Audio 先頭に新スライダー(tag 90/96/97)。全チャンネル音量 sub_1001F50B4 に掛ける。保存=NSUserDefaults `bm.master_volume`。
- 数値の手入力: 値の数字(tag 80〜85)をタップ → 中央の小パネル(入力欄 tag 94 / OK 91 / Cancel 92)。範囲: 音量 0-100 / オフセット ±500ms / 角度 30-50。
- 単位表示: % / ms / °。並べ替え: Gameplay=[Offset x2, 3D column, angle, DJ disk, fullwidth, storyboard, portrait]、Graphics=[Language, Skin, Original title, Grid mode, FPS]。
- Custom key sound: Custom 選択時に skin/custom.wav(または dir13)が無ければ、ファイル選択シート(public.audio)で取り込む(kind 103)。
- 設定の構造(IDA): config 本体=sub_1001DFA40 / main_volume +152, effect_volume +156, user_offset +160, graphic_offset +164, key_sound_type +240(0 none,1-3,4 custom), column_deg +238(30..50), dim_alpha +237 ほか。

## 2026-10-16 遅ミスの素通し(純正組み込み・実機確認待ち)/ Turbo モッド設計メモ
- 遅ミス: Key の押下入口 sub_1001197F8(scene,note,type,lane)をフック(slot 17, cave 0x100005108, gameplay.inc)。判定窓(Bad = judge+56 と +32)を過ぎた未判定音符は、その場でミス確定 → 押下は同レーンの次の未判定音符へ。無ければ空押し。設定 `bm.skip_late_miss`(既定 ON)。
  構造(IDA): scene+992 = JudgeKey(+32 レベル補正 / +48..+60 窓 [Best,Good,Bad,EarlyMiss]), scene+1072 = 現在時刻(double ms), scene+2160 = 音符 deque, 音符 int[20]=時刻 / int[30]=状態 / u8@+128=レーン。
  標準の選択 sub_100025F18 は「レーン内の最初の未判定音符」を無条件で返す → 遅い押下は sub_10014C6A4 が窓外でも Miss(=押下を消費)にしていた(窓の上限チェックが無い)。
- Turbo(V の仕様, lang-en 1119-1124): 判定ルールを自由に変更(スコアはサーバーに受理されない)。(E)ホールドを終端で判定 / (F)複数の指=不要(2026-10-16 ユーザー指示) / (M)早すぎるミスを無視 / (P)一時停止無効 / (S)音楽速度を自由に。
  実装案: (M)= JudgeKey の +60(EarlyMiss 窓)を +56 に揃える(早押しは 0x40000000 = 無視)。MOD 画面は script_mod.mui + sub_1000AF494(12 項目目)/ sub_1000B0078(選択)。名前表 sub_1000A3924, 説明 lang(190+id)。

## 2026-10-16 Key 判定の改善(実機ログで確認)
- ★実プレイの Key の押下入口は ModeLayerKey の sub_10005D060(scene, lane, type)(UIS 側 sub_1001197F8 は通らない)。scene+984=判定器(JudgeKey: +32 補正 / +48..+60 窓 / vtable[3]=押下, [4]=離し, [5]=ホールド tick)、+1064=現在時刻(double ms)、+1392.. = 音符 deque(要素→*(+16)→*(+40)=HitObjectKey)、結果適用=scene vtable+1480。
- 遅ミスの素通し(hook_kpress, slot 20): レーン先頭の未判定音符が Bad 窓を過ぎていれば、判定器でミスを確定 → 押下は次の音符へ(頭を逃した LN は bit3 を立てて吸わない)。★ログ `late-miss passthrough` は未検証(遅れ押しテストが必要)。
- LN 早離しの再ホールド禁止(hook_jrel, slot 21): sub_10014C794 が 0x40000(終端−130ms より前の離し)を返したら音符 state|=8(終了済み)。実機で確認済み(`LN early release` ログ、LN 中の他ノーツに反応)。設定キー bm.no_rehold。
- 診断用に残した予約フック: jtap(slot 18 = sub_10014C6A4)/ jtapfair(19)/ kjudge(17 = UIS)。slot 未設定なら元の処理へ。
- 次: Turbo(MOD 画面の 12 項目目 + メニュー): 段階1。M(早ミス無視)は JudgeKey +60 を +56 に揃える。

## 2026-10-16 Turbo モッド(実装済み・実機確認済み)
MOD 画面の 12 個目(id 11, ビット 0x800)。選ぶとメニュー(skin `_turbo`、tag 40〜55)が出る。V の Turbo の再現。コード=dylib/turbo.inc / gameplay.inc、スキン=tools/build_skin_mod.py。
- **(S) 音楽速度**(50〜200%, ±5): playstart(sub_10007D658)が a1+808 の既定速度を差し替え(音楽チャンネルへ sub_1001F5A84)。流れる速さの補正は Key=sub_1000DD464 後に a1+1236、Slide=sub_1000D0BD8 / sub_1001163C8 後に this+0x5e8 へ ×100/速度(標準の DASH 等が入れる 1/速度 と同じ)。★Turbo は DASH/RUSH/SLOW より優先(元の処理を呼ぶ間だけモッドの速度ビットを外す)。
- **判定窓は実時間で一定**: 窓は譜面 ms 固定なので、全モード共通の準備 sub_10010C1C4 の後に判定器(JudgeBase 派生)の int[10..] を速度に比例して補正(Key/Slide=10..15, Pad=10..19, Jubeat=10..17, IIDX=10..16, Taiko=10..14, LoveLive=10..11; vptr で判別)。LN 終端の固定値 130ms は未補正。
- **(M) 早すぎるミスの無視**: JudgeKey の早押し窓 jd[15] を Bad 窓 jd[14] に揃える。**(P) 一時停止の無効**: sub_10007FBA8 の「止める側」だけ通さない(トーストなし)。(F) 複数の指=不要、**(E) ホールドを終端で判定=見送り**(工数に対し需要不明。実装は osu!mania 風まで試作して撤去)。
- **スコア非公認**: オンライン送信+保存 sub_1001F78DC(a4+4 のモッドに 0x800 → 何もせず 0)と、ローカル記録 sub_1001458DC(戻り先 0x10002E4A4 = リザルト sub_10002E1F0 からの呼び出しだけ、gPlayMods で判定)を止める。
- **表示**: 読み込み画面/リザルトの小アイコン `mod%ds.png`(sub_1001F3A08 のループ 11→12 を静的パッチ、mod11s.png をアトラスへ追加)。譜面名 sub_1001B5638 は戻り先 0x10007257C(読み込み)/ 0x100035774(リザルト)のときだけ末尾に " 1.2x"(x8 の std::string 返却を 24B 構造体として受ける)。
- ★**選択状態の正はプレイ設定のモッド(+4)**: MOD 画面のフラグだけに頼ると、再起動後(モッドはゲームが保持)に OFF 扱いになる不具合になった。各フックで turbo_sync(sets) して同期、読み込み画面用に NSUserDefaults `bm.turbo.on` へ控える。
- スキンの教訓: 追加ノードは `+item` の前に入れる / 位置は % でなく `_mask` 相対 / mod アイコンはアトラス+plist へ(空き枠は plist 全フレームと照合)/ 暗幕は不透明 `_turbosolid`。
- フック台帳(slot): 20 kpress, 21 jrel, 22 modname, 23 modsel, 24 modbuild, 25 playstart(0x100005220), 26 prepkey(…238), 27 pausetgl(…250), 28 savescore(…268), 29 localrec(…280), 30 jtick(予約・未使用), 31 diffname(…2B0), 32 slideprep(…2C8), 33 slideprep2(…2E0), 34 baseprep(…2F8)。※jtap(18)/jtapfair(19)/kjudge(17) は予約・未使用。
- 未対応/候補: LN 終端 130ms の補正、Catch/Ring 等のスクロール補正(Slide と同型の可能性)、Taiko は元から問題なし。

## 2026-10-16 Taiko エディタの拡張(実装済み・実機確認済み)
コード=dylib/taiko.inc(+ bm.m の is_taiko 分岐)、スキン=tools/build_skin_taiko.py。EditorTaiko(vtable 0x10091E870)は横スクロールの別実装(K = this[2580]*this[2584]、ノート x = K*beat、コンテナ x = -pos*K)。
- **小節線ツール**: 右ツールメニュー 7 番目(tag 88、アイコン bm-bar.png)。ON でタップ位置の拍(sub_100546980 でスナップ。24B 構造体返却で受ける=x8 sret)から新しい小節(sign effect を追加/解除。直前の sign を継承、既定 4)。effect 追加後 sub_10013FAE8(this,1) で小節線を引き直す。他のツール選択で OFF。
- **FX プレビュー**: 左の FX ボタン(Key と共通)。scroll/jump は G(beat) の積分、**hs はノート個別の速度係数**として判定線(現在位置)からの距離に掛ける: X = K*(G(pos) + hs*(G(b)-G(pos)))。ノート/ロング長(sub_100174DF4)/グリッド点(sub_100143C98)/小節線に適用。hs 有りの譜面だけ毎フレーム t_hs_update(現在位置が動くたび)。プレビュー中は編集帯のタッチを無効。下のサムネは線形のまま。hs の BPM 補正(Processor +59)・区間またぎは未対応。
- **Effect の追加**: 右ツールバー 2 番目(メトロノーム、tag 18)→ BPM / Effect / Folder(Taiko は Plugins なし)。Effect パネルは script_edit_pc.mui 共通で Key/Slide と同じ(Scroll/HS/Jump/Sign、Apply=現在位置に追加、Remove)。確定後(g_build_if_needed)に小節線を引き直し。
- フック台帳(slot): 35 ttouch(sub_100140664)、36 trelayout(sub_10013F280, cave 0x100005328)、37 trefresh(sub_10013EDF8)、38 tbars(sub_10013FAE8)、39 tgrid(sub_1001402C4)、40 tgridpos(sub_10013EF0C)。
- 未移植(Key/Slide のみ): 選択/コピペ/Undo/Plugins/スペクトログラム/マーカー/情報画面拡張/その他メニューのカテゴリ。Taiko は NoteData ベースの別モデルなので、必要になったら調査して追加。

## 2026-10-16 Slide プラグイン検証(要望対応・実機確認待ち)
- Downloads の 3 zip(色々 16本 / Giza 4本+画像 / 敷スライド系 3本)を export_plugins/<zip名>/ に展開して検証。既存プラグインは backup/export_plugins.20261016 へ退避(削除なし。戻すときは入れ替え)。
- ハーネス(tools/lua_harness.sh): Slide 選択を環境変数 `HARNESS_SEL="3,6"` で指定可能に(mode 7 は id%3==0=Slide 2048 / 1=Tap 1 / 2=Wipe 1024)。モックの型番号を実値に修正。結果: 27 本すべて 0 エラー(Giza は Interact の全イベント、join 系は 3 本選択で結合まで通過)。実際の見た目・ドラッグ操作は実機で確認する。

## 2026-10-16 Slide プラグイン検証の結果(要望の 3 zip: 実機確認済み)
修正した不具合(すべて BetterMalody 側。プラグインのファイルは無改造):
- **レーンが縮んだとき(一覧を開いた状態)にタップと違う場所へノートが置かれる**: 標準のノート配置(Slide=sub_1000C6744、Key=sub_1001B1D30/sub_1001B1D9C)は、タッチを容器(+1472)の**ローカル座標**(縮尺込みの逆変換 sub_100546980)で判定する。縮んだ容器へ「元の座標系に戻した座標」を渡すと縮尺が二重にかかる。→ 自前の処理(プラグイン / 投げ縄)には戻した座標、標準の処理には元の画面座標を渡す(`pl_right_lane_unmap`。容器が縮み対象のときだけ)。
- **GizaSlideNested**: 先頭の節(頭と同じ拍)に 6 点を同じ拍で足すため、同じ拍の節が連続する → 反映前に連続する同拍の節は最後の 1 つだけ残す(`pl_rebuild_dirty`)。
- **つまみ(Giza 系)が掴めない**: 原因は 2 つ。(1) プラグインの描画物(モジュールのスプライト)の上のタッチは、ゲームの dispatcher が編集領域(etag 105)へ渡さない → 描画物ノードの etag(**ノート +492**)を 105 にして編集領域の処理へ届ける。(2) `GetClickBeat` が分割幅の**切り捨て**で、1/4 分割だとつまみの真上でも 0.25 拍ずれ、Giza の「|クリック−つまみ| ≤ 1/8 拍」判定を外す → **最寄りの線へ丸める**に変更(`h_click_beat`)。※標準のタップ配置は切り捨てだが、プラグインの当たり判定には最寄りの方が自然。他プラグインへの影響が出たら fine/floor を切替可能にする。
- 描画物の高さ: Width だけ指定した大きな正方形画像(Giza のライン 500×500)が画面を覆わないよう、高さは元の大きさにする(`mod_update`)。
- ハーネス(tools/lua_harness.sh): `HARNESS_SEL`(選択ノート)/ `HARNESS_CLICK`(クリック位置)/ `HARNESS_V`(節の追加ログ)を追加、モックの型番号を実値(Slide 2048 / Wipe 1024 / Tap 1)に修正。
- ★診断の教訓: ログは `/usr/bin/log show --predicate 'process == "Malody Mobile" AND category == "rt"'`(zsh の `log` は組み込みコマンドで使えない)。os_log の診断で「ゲームに届く前か後か」を切り分けた(UIKit の sendEvent を一時的に swizzle して比較)。診断コードで gPlEd が NULL のセッション外から h_click_* を呼ぶとクラッシュする。

## 2026-10-18 順番に選択(Order select)/ 敷スライド系プラグイン(実機確認済み: 枝分かれ敷)
- **GetOrderNotes / ClearOrderNotes**(Slide Editor の独自 API と同義)を実装。タップした順のノート ID を返す。`LHHost.order / order_clear`(lua_host.h)、実体は plugins.inc の `gOrdNotes`。
- **Order select**: 「その他 > Select > Order select」(tag **768**。766 は Plugins と衝突していたので注意)で ON/OFF。ON の間、スライドのタップ順を記録し、始点に赤い大きめの番号(テキストモジュール `bm-ord-N`、`BMMod.fontMul`=2.2)を出す。何も無い所のタップ = リセット、再タップ = 変化なし、OFF にしても順番は残る、プラグイン実行後にリセット。
- `GetOrderNotes` を含むプラグイン(Once 型)は、順番が空なら 1 回目の Run で Order select を ON にして待機、もう一度 Run で実行。順番を先に作っていれば Run で即実行。
- ノート自身のタッチは別の delegate(sub_1000C7224)。フック **slot 41 `snote`**(cave 0x1000053A0)で、選択更新後に `ord_track` を呼ぶ。レーン側(sub_1000C6744、slot 8)は ev=3 で判定。
- 同拍の節を除く処理(Giza Nested 用の推測)は誤りだったので撤去。敷スライド系は「境目の拍の重複を残す」設計。Mourning(バグスライド譜面)は元から同拍連続が多い。
- 友人向けの案内文あり(Lua 3 本を読み直して、プラグイン側の問題は無し。join_end_to_start の Order 扱いは冒頭コメントどおりで仕様)。
- 未確認: 折り返し敷 / join_end_to_start、色々.zip の大半。Order の番号表示は Key では出ない。

## 2026-10-20 Taiko エディタの拡張 その2(実機確認済み)
コード=dylib/taiko.inc / undo.inc。右ツールメニューは 9 枠が埋まっていて足せない(8〜11 は Undo/Redo 等の枠)ため、追加機能は「その他」メニュー(`taiko_add_menu_items`)に項目として足す。
- **選択モード**: Select mode(tag 89)/ Select all(91)/ Deselect(92)/ Delete selected(90)。標準の Taiko は「ノートをタップ = 削除」で、複数選択はコピー機能だけ。選択の集合は this+1992 の std::set<Node*>(+2000 終端 / +2008 件数)。追加 sub_100144AB8、除去 sub_100144D9C、強調 sub_100175370(HitSpriteTaiko, 色)、全解除 sub_100140A18、全選択 = vtable +1584。タップしたノートの検索は標準と同じ sub_1000FF264(コンテナ+1792, x, 許容)。削除は sub_100141108(this, HitObject, flag)。選択モード中のタップ = 選択の追加/解除(何も無い所 = 全解除)。
- **Undo/Redo**: undo.inc を Taiko に拡張(`un_live` はコンテナ +1792 → vt+728 → NoteData → +16 HitObject)。復元の追加 = sub_100142950(スプライト)→ sub_1001C6834(*(this+2064)=譜面)→ sub_10006C1EC/288(音)→ vtable +1648(標準の追加の完了手順 sub_100140BD0 と同じ)。削除 = sub_100141108。Taiko では情報画面項目の取り消しは対象外。`hook_ttouch` / `hook_trefresh` から un_mark / un_tick。
- **選択ノートへ hs を適用**(Apply HS to selection, tag 93): 連続して選んだ区間の先頭の拍に hs 点を 1 つ、区間の次のノートの拍に適用前の hs を戻す。区間内の元の hs 点は消す。値は文字入力。
- **hs ランプ**(HS ramp, tag 94): 選択の最初〜最後のノートの間で、拍ごとに hs 点を置く。入力 `開始,終了[,曲線]`。曲線 l/i/o/s/e。最後の次のノートで適用前に戻す。
- **hs の範囲操作**(tag 95/96): HS clear (range) = 範囲 [最初に選んだ拍, 最後の次のノート) の hs 点を消して次のノートで元の値に戻す / HS multiply (range) = 範囲の hs に倍率(文字入力)。`t_hs_range_op`、範囲は `t_beats_sel`。
- **HS labels**(tag 97, 確認済み): FX プレビュー中、hs≠1 または直前から変わるノートの下に値を赤・大きめ(fontH×2.2)で表示。ラベルはノート用コンテナ(+1792)の親に載せる(`t_parent_of` で実行時に探す。コンテナの子にすると標準処理がノートと誤認する)。位置 = コンテナ位置 + ノート y − 帯高(+2080)/2 − fontH×1.4。毎フレーム `hook_trefresh` から `t_hs_labels`、画面外は省き最大 96 個のプール。
- **Taiko のプラグイン対応**(確認済み): 入口は BPM アイコンの選択メニュー(BPM / Effect / Plugins / Folder)と右メニューのコンセントボタン(tag 772。右の一覧の再構築は `hook_trefresh` から `pl_right_tick`)。`PluginMode=5` が Taiko。ノート = HitObjectTaiko(スプライト vt+728 → NoteData → +16)。API: GetNoteType = 短い 1 / 長い 128、GetNoteFlag = 種類(0 don / 1 大don / 2 ka / 3 大ka / 4 連打 / 5 大連打 / 6 風船 / 7 大風船)、SetNoteFlag(id, 種類)(新規 host.set_flag、種類設定 sub_1001EBAF4)、SetNoteBeat、AddNote(1=don / 128=連打。生成 = new 0x98 → sub_1001EBAA8 → sub_1001EBC00(c,0) → sub_1001EBAF4)、DeleteNote(sub_100141108)、SelectNote、GetSelectNotes。既存ノートの拍 / 種類の変更は「複製して差し替え」(`pl_t_replace`。差し替えは Run 終了時に `pl_commit_pending` が 標準の貼り付けと同じ手順で挿入)。X=0 / Width=1。未対応: Interact 型 / AddText・AddSprite の描画。確認用 = `export_plugins/Taiko/`(swap / shift / add don)。
- **反転ペースト**(Paste mirrored (don/ka), tag 98): 標準の貼り付け(vtable +1728)の前にクリップボード(this+840)の種類を 0↔2 / 1↔3 に入れ替え、貼った後に戻す。教訓: 種類の番号は 偶数 = 小 / 奇数 = 大(0 don / 1 大don / 2 ka / 3 大ka)。最初に 1=ka と取り違えて swap が大小の入れ替えになった。
- **文字入力**: `bm_input_dialog(ed, title, default, block)`(plugins.inc)= プラグインの入力パネルを、プラグインを介さずに使う(`kb` で数値のステップダイアログを避ける)。Taiko でも動くよう PL_INBOX/INOK/CH の処理を is_taiko まで通す。
- 教訓: 新しいメニュー項目のタグは既存と衝突しない値を選ぶ(766=Plugins と衝突して Order select が Plugins を開いた → 768)。skin の右メニュー枠(`_right-icon3-N`)は 9 枠が既存の機能で埋まっている。
- 未移植(Taiko): スペクトログラム / マーカー / 情報画面の拡張 / 移動・量子化(Plugins と反転ペーストは対応済み)。

## 2026-10-20 設定画面 / Web の調整
- 設定画面の値入力パネル: 数字の当たり判定を 200×64(+30 右)に拡大、OK/Cancel に 320×100 の見えない当たり判定を追加、パネル外の暗幕(tag 99)タップ = キャンセル(詰み防止)。オフセットの −1/+1 ボタン(tag 86〜89)は廃止(`tools/build_skin_cfg.py` / `dylib/settings.inc`)。診断ログ `settings touch:`(パネル表示中のタッチ)。
- アプリ内 Web を既定のブラウザで開く(確認済み): `bm_web_external_install`(bm.m)。`UIWebView loadRequest:` を差し替えて http(s) を `UIApplication openURL:options:completionHandler:` へ渡し、`WebviewHandle`(initWebview 0x1000202f0 / show 0x10002050c)の全面ビューを隠す。無効化 = `defaults write me.mugzone.m bm.web_external -bool NO`。ログ `web:`。

