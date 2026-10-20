# PlayCover での検証手順 (BetterMalody)

前提: Apple Silicon Mac / macOS 26.0 / PlayCover (/Applications/PlayCover.app) 導入済み。
対象: bundle `me.mugzone.m`、Mach-O は arm64 単一、`LC_VERSION_MIN_IPHONEOS` 8.0 (SDK 12.2)、cryptid=0。

## 0. ベースライン(無改造で動くか) — 最初の関門
1. `orig/Malody_Mobile_4.3.7.ipa` を PlayCover にドラッグして導入(書込不可のままでよい。PlayCover はコピーを作る)。
2. 起動して確認: 描画(OpenGL ES)・音・タッチ(マウス)・エディタ画面に入れること。
3. 失敗する場合の切り分け: 旧形式のロードコマンド(`LC_VERSION_MIN_IPHONEOS`)が原因なら、`LC_BUILD_VERSION` への書換が必要(別途 plan を出す)。

## 1. パッチ検証の基本ルール (過去の教訓)
- **実行されるのは PlayCover のコンテナ内コピー**:
  `~/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app/`
  ソース側 (`work/`) だけ直しても反映されない。パッチ後は**コンテナ内の実バイナリをバイト単位で確認**する。
- **IDA の DB はバニラの静的スナップショット**。「いま空いているか/何を指しているか」は実バイナリを直接読んで確認する(アドレス = 仮想アドレス − 0x100000000)。
- ASLR: 実行時はスライドが乗る。手書きコードは ADRP+ADD / B / BL(PC 相対)だけで組み、静的データに絶対ポインタを埋め込まない。
- `__TEXT` は実行時書込不可。書込み可能な永続データは `__bss` 等の zerofill に置く(静的パッチ=ファイル書換は可)。
- 再署名: `codesign --force --deep --sign - <app>` (ad-hoc)。PlayCover の再インポートでも可。

## 2. 検証サイクル
1. `work/` の Mach-O にパッチを当てる(`patches/` の定義から `tools/` で生成)。
2. IPA を作る(`build/`) → PlayCover でアプリを削除→再インポート(確実)、または コンテナ内へ直接コピー + ad-hoc 再署名(速い)。
3. 起動確認 → 実バイナリのバイトを再確認 → 結果を `docs/` に記録。
4. 失敗時: クラッシュログ(`~/Library/Logs/DiagnosticReports/`)の PC・far をイメージベース/スライドと照合。

## 3. ロールバック
- `orig/Malody_Mobile_4.3.7.ipa`(SHA256 は `orig/SHA256SUMS`)を PlayCover に入れ直す。
- コンテナ内の直接編集を戻すときは、アプリを削除して再インポートする。

## 4. PlayCover 固有の注意
- キーマップ/タッチ変換は PlayTools 側。エディタのジェスチャ(ピンチ・長押し・ドラッグ)が PlayCover の入力変換で再現できるかを、ベースラインで確認しておく。
- 無印の操作感の検証は、最終的に**実機 iPhone でも行う**(PlayCover は入力経路が異なる)。

## 5. ベースライン結果 (2026-10-14)
- 無改造IPAをPlayCoverに導入し、起動・ログインまで確認(ユーザー報告)。
- PlayCoverは導入時にMach-Oを書換える: `LC_VERSION_MIN_IPHONEOS 8.0` → `LC_BUILD_VERSION platform 6(Catalyst) minos 11.0 sdk 14.0`、`PlayTools.framework` のLC_LOAD_DYLIB追加、再署名。
- **コード部(`__text`, file offset 24480, size 0x73b53c)は元とバイト同一**。差異はヘッダ(先頭2ページ)と署名領域のみ → 元バイナリのファイルオフセット(= VA − 0x100000000)で書いたパッチは、コンテナ内コピーにもそのまま当てられる。
- 元バイナリsha256(先頭16)= b3c5ebbc473c3218 / 導入後 = 7233ed9a51fd2a19(署名・ヘッダ差のため一致しない。正常)。

## 6. 運用方式(2026-10-14〜): コンテナ直接パッチ
IPA再生成・再導入はせず、PlayCoverコンテナ内のバイナリを直接書き換えて ad-hoc 再署名する(wrcwewと同じ方式)。
- 適用: `tools/apply_container.sh 0xVA=旧hex:新hex ...` (起動中なら中止。毎回 `backup/MalodyMobile.bin.<時刻>` を作る。`.original` は初回の無改造バイナリ)
- 復元: `tools/apply_container.sh --restore` (最新バックアップへ) / 無改造へ戻すには `backup/MalodyMobile.bin.original` をコンテナへコピーして再署名。
- 署名: `codesign -f -s - --entitlements backup/ent.plist <app>` (entitlementsは導入済みアプリから抽出。改変のたび必須)
- skin(`skin-ios.bin`)も同じ要領で、コンテナ内の該当ファイルを差し替える(署名のseal更新のため再署名する)。
