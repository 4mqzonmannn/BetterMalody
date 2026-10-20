#!/bin/bash
# PlayCover コンテナ内の Malody Mobile バイナリに直接パッチを当て、ad-hoc 再署名する。
# 使い方: apply_container.sh <VA=旧hex:新hex>...    (VAは元バイナリのアドレス)
#         apply_container.sh --restore                  (最新のバックアップへ戻す)
set -euo pipefail
P="$HOME/Documents/BetterMalody"
APP="$HOME/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app"
BIN="$APP/Malody Mobile"
BK="$P/backup"; mkdir -p "$BK"
if pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null; then echo "ERROR: Malody Mobile が起動中です。終了してから実行してください" >&2; exit 1; fi
resign() {
  codesign -d --entitlements - --xml "$BIN" > "$BK/ent.plist" 2>/dev/null || true
  codesign -f -s - --entitlements "$BK/ent.plist" "$APP"
  codesign -v "$APP" && echo "codesign OK"
}
if [ "${1:-}" = "--restore" ]; then
  LAST=$(ls -t "$BK"/MalodyMobile.bin.* | head -1)
  cp "$LAST" "$BIN"; resign; echo "restored from $LAST"; exit 0
fi
TS=$(date +%Y%m%d-%H%M%S)
[ -f "$BK/MalodyMobile.bin.original" ] || cp "$BIN" "$BK/MalodyMobile.bin.original"
cp "$BIN" "$BK/MalodyMobile.bin.$TS"
TMP=$(mktemp "$BK/tmp.XXXXXX")
python3 -I "$P/tools/patch_bytes.py" "$BIN" "$TMP" "$@"
cp "$TMP" "$BIN"; rm -f "$TMP"
resign
echo "applied. backup: $BK/MalodyMobile.bin.$TS"
