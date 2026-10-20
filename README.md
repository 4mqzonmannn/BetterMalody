# BetterMalody

無印 **Malody 4.3.7 (iOS)** のエディタ/プレイを便利にする非公式の改造版です。
元アプリの操作感はそのまま、追加機能は新しいボタン/メニュー項目として足してあります(既定は無印と同じ挙動)。

> **Unofficial mod of Malody 4.3.7 for iOS** (dylib injection + static binary patches + skin edits).
> Not affiliated with Mugzone. Malody and its assets belong to Mugzone.

## 主な機能
- **グリッド(分割)拡張**: 分割メニューを 1 列 16 項目に拡張、グリッド線の見え方を調整
- **Effect マネージャ + FX プレビュー**: scroll / hs / jump / sign の一覧・追加・範囲操作、エディタ上のプレビューとマーカー(Key / Slide / Taiko)
- **選択・編集の強化**: 全選択・反転・解除・Lasso、コピー/ペースト/反転ペースト、移動、クオンタイズ、幅の一括変更、Undo / Redo
- **Taiko 拡張**: 選択モード、HS の一括適用・ランプ・倍率・クリア・値表示(HS labels)、小節線ツール、don/ka 反転ペースト
- **Lua プラグイン**: Malody V のエディタプラグイン(PluginMode 0 = Key / 5 = Taiko / 7 = Slide)を実行。右メニューのコンセントボタンから一覧、Interact 型・描画物(AddText / AddSprite)にも対応。API は [docs/LUA_PLUGINS.md](docs/LUA_PLUGINS.md)
- **スペクトログラム表示**: エディタのレーン上に半透明で表示(オフセット検証つき)
- **設定画面**: 値の手入力、Master Volume、カスタムキー音の取り込み
- **その他**: 譜面置き場を `Documents` 直下へ(ファイル App で編集可)、情報画面に Video / BGA / Skin 追加、アプリ内 Web を既定のブラウザで開く

## インストール(実機)
1. [Releases](../../releases) から `BetterMalody-*.ipa` をダウンロード
2. **Sideloadly** や **AltStore** などで、自分の Apple ID の証明書で署名してインストール(IPA は ad-hoc 署名のため、そのままでは入りません)
3. バンドル ID は `me.bettermalody.m`、アプリ名は `BetterMalody` です。公式の Malody とは **別アプリ**として共存し、データ(セーブ・譜面・設定)も別です

> **実機での動作は未検証です。** 開発・検証は Apple Silicon Mac の PlayCover で行っています。
> 起動しない / 落ちる場合は Issue にデバイスと iOS のバージョンを添えて報告してください。

## ビルド(自分で作る)
元の IPA(Malody 4.3.7)を自分で用意し、`orig/Malody_Mobile_4.3.7.ipa` に置いてから:

```bash
tools/build_ipa.sh        # → build/BetterMalody-4.3.7-bm1.ipa
```

必要なもの: macOS + Xcode Command Line Tools(MacOSX SDK)、`optool`、Python 3(Pillow)。
バイナリのコードパッチ(`tools/make_*_patch.py`)・skin の拡張(`tools/build_skin_*.py`)・dylib(`dylib/`)を元 IPA へ適用し、アプリ名とバンドル ID を書き換えます。

## ディレクトリ
- `dylib/`   BetterMalody.dylib のソース(`bm.m` と `*.inc`、Lua ホスト、Lua 5.4)
- `tools/`   パッチ生成 / skin 拡張 / IPA 作成スクリプト
- `patches/` パッチの設計メモ(アドレス・旧/新バイト・理由)
- `docs/`    解析メモ(アドレス台帳、Lua プラグイン API、スペクトログラム)

## 注意
- 非公式の改造です。Mugzone および Malody とは無関係で、公式サポートの対象外です。
- Malody 本体・画像・音声・収録曲の権利は Mugzone にあります。権利者から要請があった場合、リリースの IPA は削除します。
- オンラインの機能(ランキング・アカウント等)で不具合が起きても、公式には問い合わせないでください。
- このリポジトリの独自コードは MIT ライセンスです([LICENSE](LICENSE))。
