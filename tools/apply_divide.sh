#!/bin/bash
# 分割メニュー(1列・汎用フォント・16項目)を適用: skin再構築 + コードパッチ。アプリ終了を待つ。
set -euo pipefail
P="$HOME/Documents/BetterMalody"
APP="$HOME/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app"
ORIG_SKIN="$P/orig/extracted/Payload/Malody Mobile.app/skin-ios.bin"
NEW_SKIN="$P/work/divide/skin-ios.bin"; BK="$P/backup"; mkdir -p "$BK" "$P/work/divide"
python3 -I "$P/tools/make_labels.py" "$P/work/labels"
python3 -I "$P/tools/build_skin_divide.py" "$ORIG_SKIN" "$P/work/labels" "$NEW_SKIN"
for i in $(seq 1 900); do pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null || break; sleep 2; done
if pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null; then echo "TIMEOUT: still running"; exit 1; fi
[ -f "$BK/skin-ios.bin.original" ] || cp "$APP/skin-ios.bin" "$BK/skin-ios.bin.original"
cp "$NEW_SKIN" "$APP/skin-ios.bin"
SPECS=$(python3 -I "$P/tools/make_divide_patch.py" "$APP/Malody Mobile")
if [ -n "$SPECS" ]; then "$P/tools/apply_container.sh" $SPECS; else codesign -f -s - --entitlements "$BK/ent.plist" "$APP"; echo "no code diff; resigned"; fi
echo "divide menu applied"
