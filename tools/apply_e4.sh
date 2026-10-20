#!/bin/bash
# E4: 分割メニュー2列目 = skin差し替え + コードパッチ(ツールバー値表/強調/範囲)。アプリ終了を待って実行。
set -euo pipefail
P="$HOME/Documents/BetterMalody"
APP="$HOME/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app"
ORIG_BIN="$P/orig/extracted/Payload/Malody Mobile.app/Malody Mobile"
ORIG_SKIN="$P/orig/extracted/Payload/Malody Mobile.app/skin-ios.bin"
NEW_SKIN="$P/work/e4/skin-ios.bin"
BK="$P/backup"; mkdir -p "$BK"
for i in $(seq 1 900); do pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null || break; sleep 2; done
if pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null; then echo "TIMEOUT: still running"; exit 1; fi
[ -f "$BK/skin-ios.bin.original" ] || cp "$APP/skin-ios.bin" "$BK/skin-ios.bin.original"
cmp -s "$APP/skin-ios.bin" "$ORIG_SKIN" || echo "note: container skin differs from orig (previously patched?)"
cp "$NEW_SKIN" "$APP/skin-ios.bin"
SPECS=$(python3 -I "$P/tools/make_divide_patch.py" "$ORIG_BIN")
"$P/tools/apply_container.sh" $SPECS
echo "E4 applied (skin + code)"
