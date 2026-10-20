#!/bin/bash
# dylib をビルドしてコンテナへ導入(+ フックのコードパッチ)。アプリ終了を待つ。
set -euo pipefail
P="$HOME/Documents/BetterMalody"
APP="$HOME/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app"
BIN="$APP/Malody Mobile"
"$P/dylib/build.sh"
for i in $(seq 1 900); do pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null || break; sleep 2; done
if pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null; then echo "TIMEOUT: still running"; exit 1; fi
cp "$P/dylib/BetterMalody.dylib" "$APP/BetterMalody.dylib"
# skin: 分割メニュー版(work/divide)へ FX ボタンを追加
mkdir -p "$P/work/fx"
SRC="$P/work/divide/skin-ios.bin"; [ -f "$SRC" ] || SRC="$P/orig/extracted/Payload/Malody Mobile.app/skin-ios.bin"
python3 -I "$P/tools/build_skin_fx.py" "$SRC" "$P/work/fx/skin-ios.bin"
python3 -I "$P/tools/build_skin_cfg.py" "$P/work/fx/skin-ios.bin" "$P/work/fx/skin-ios-cfg.bin"
python3 -I "$P/tools/build_skin_mod.py" "$P/work/fx/skin-ios-cfg.bin" "$P/work/fx/skin-ios-mod.bin"
python3 -I "$P/tools/build_skin_taiko.py" "$P/work/fx/skin-ios-mod.bin" "$P/work/fx/skin-ios-taiko.bin"
cp "$P/work/fx/skin-ios-taiko.bin" "$APP/skin-ios.bin"
if ! otool -L "$BIN" | grep -q BetterMalody.dylib; then
  cp "$BIN" "$P/backup/MalodyMobile.bin.pre-dylib"
  optool install -c load -p "@executable_path/BetterMalody.dylib" -t "$BIN"
fi
# ファイルアプリ: Documents 共有は元から有効。他アプリ/ファイルアプリからの「その場で編集」を許可する
plutil -replace LSSupportsOpeningDocumentsInPlace -bool true "$APP/Info.plist"
SPECS=$(python3 -I "$P/tools/make_hook_patch.py" "$BIN")
if [ -n "$SPECS" ]; then "$P/tools/apply_container.sh" $SPECS; else codesign -f -s - --entitlements "$P/backup/ent.plist" "$APP"; echo "no code diff; resigned"; fi
echo "dylib applied"
