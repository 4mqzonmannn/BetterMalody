# Malody V エディタプラグイン(Lua)対応 — 設計と実装状況

## 方式
- Lua 5.4.7(MIT)を dylib に静的に内蔵(`dylib/third_party/lua/lua-5.4.7`)。Lua ヘッダは識別子 `L` を使うため、bm.m(`L`=os_log)とは別翻訳単位 `lua_host.c` に分離。
- `lua_host.c` = `Editor` / `Review` テーブルと Beat テーブル(`{beat,numor,denom}`)。エディタ内部への橋渡しは `LHHost` コールバック(`lua_host.h`)で、実体は `dylib/plugins.inc`(bm.m に include)。
- 安全策: 標準ライブラリは base/table/string/math/utf8/coroutine のみ(os/io/package/require/dofile を除去)。実行 3 秒でタイムアウト。ホスト実行はメインスレッド同期。
- 配置: `Documents/export_plugins/*.lua`(+ 1 階層のサブフォルダ)。ファイルアプリから追加可。初回にサンプル 3 本を自動生成。
- UI: 右ツールバーの BPM ボタン → 「Plugins」→ Effect リストと同じ作りのパネル(skin の `_plpanel`/`_pllist`、tag 330=Run / 331=Reload)。OS の UI は使わない。

## 実装済み API(v1)
- Query: GetSelectNotes/SelectNotes, GetNoteCount, GetNoteAt(拍→x 順), GetCurrentTime/Beat/Divide, GetNoteTime/Beat/X/Type/Flag(常に0)/Width,
  GetNoteSlideBodyCount/Beat/X, ChartInfo(version/creator/title/artist/bpm/key), GetTimeCount/At, GetEffectCount/At(sv/hs/jump に平坦化)
- 編集: SetNoteBeat(id,beat,head), SetNoteX, SetNoteWidth(Slide), DeleteNote(標準の削除手順), AddNote(同種の既存ノートを雛形に複製。Run 終了時に挿入), SelectNote
- 補助: ShowMessage/ShowTip(ゲーム内トースト), MakeBeat, BeatAdd, BeatMinus, GetUserInput(既定値で即コールバック), Review:SetResult
- 追加(2026-10-16): AddNoteSlideBody/SetNoteSlideBodyX/DeleteNoteSlideBody(節 int16 x@+24 の編集 + スプライト作り直し), AddEffect/DeleteEffect(sv/hs/jump/sign), AddTime/DeleteTime(BPM), SeekTo/Beat(+872 を設定),
  CallShortcut(7/8 速度, 10/11 分割, 12 コピー, 13 貼り付け, 16 全選択, 18/19 Undo/Redo。セッション終了後に実行), ReadData/WriteData(NSUserDefaults), ReadFile/WriteFile(譜面フォルダ)
- 描画(2026-10-16): AddText/AddSprite(PNG/JPG は譜面フォルダから)/FindModule/RemoveModule。X(%)・Width(%)・Height(px)・Beat・HeightBeat・Opacity・Visible・Text を代入で反映(modules.inc)
- 追加(2026-10-16 その2): ReadBytes(0 始まり配列+Length), ReadChartCustom/WriteChartCustom(譜面フォルダの bm_chart_custom.json),
  DoRequest(tag,url,method,data) → OnResponse(tag,content)(http/https のみ・15 秒・結果は成功時は本文、失敗時 {"code","message"} の JSON。要求時にホスト名をトースト),
  print(os_log), os.time/clock/date/difftime(os の他は無し), ShowTip=上端の常設 1 行, Review:SetResult は常設行にも残す,
  モジュール: Alpha(0〜100) / Scale / ScaleX / ScaleY / SetColor, GetUserInput(パネル内入力欄)
- 選択(2026-10-16 その3): GetUserSelect(title, cb(bool))=はい/いいえ, GetUserOption(title, o1..o4, cb(int 0 始まり))=パネル内の選択ボタン(最大 4)。シグネチャは IL2CPP ダンプ(bic)で確認、戻り値の基数は推定。
- スライダー(BM 拡張): GetUserSlider(title,min,max,default[,step],cb(number)) / GetUserSlider2(t1,min1,max1,def1,t2,min2,max2,def2,cb(v1,v2))。ゲームの設定ダイアログと同じゲージ。取消はコールバックを呼ばない。
- 親モジュール: AddSprite/AddText の第 3 引数 parent。子の X/Width は親の幅に対する %、Beat は親の拍からの相対、Alpha は親と乗算、親が非表示なら非表示。
- 互換性精査(2026-10-16 その4, 公式 Editor Plugin API.md と突き合わせ):
  モジュールの値の読み出し(Height は HeightBeat 設定後も換算値を返す / FindModule でも X・Width・Alpha・Rotate 等が読める)、Height の単位=画面高 1080
  (Slide はレーン幅 1080。Bezier 系の `b*10.8` 前提)、Rotate(度・時計回り。Node::setRotation = sub_100543EBC を直接呼ぶ。+88 の組は回転ではなかった)、GetUserInput(既定値, cb) の 2 引数形、
  ReadBytesSelect(prompt, cb(bytes|nil))=ファイル選択シート(実機確認済み)、OnKey(key, down)=外付けキーボード(実機確認済み)(GameController 経由。a〜z=97〜122, 0〜9=48〜57, 矢印=273〜276)。
  画像が見つからないモジュールは透明。
- 未対応(呼んでも無視): SetNoteFlag/Group, SetGridLine, Rotate, OnKey

## 制約 / 次の課題
- Interact(PluginType=2): 対応済み(2026-10-16 動作確認)。Run で有効/無効を切替え、有効中はレーン上のタッチを OnClick / OnDragStart/Move/End へ転送(8px 以上動いたらドラッグ)。OnKey は未対応(キーボード無し)。
- StartBatch/FinishBatch は未対応だが、Undo は操作が落ち着いた時点の差分なので Run 1 回=1 Undo(BPM の AddTime/DeleteTime は対象外)。
- 同じ位置のノートは挿入されない(標準の貼り付けと同じ)。AddNote: Slide は直接生成(雛形不要)、Key は既存ノートの複製(空の譜面では不可)。
- 実機未確認の仮定: 譜面のノート一覧 = chart+184/+192(ログ `plugins: chart notes=… sprites=…` で照合)。
