#!/bin/bash
# 無改造バイナリ(PlayCover再導入直後など)に、完成済みパッチ一式(E2 灰色明るく + E3 保存値丸め解除 + 分割メニュー)を再適用する。
set -euo pipefail
P="$HOME/Documents/BetterMalody"
APP="$HOME/Library/Containers/io.playcover.PlayCover/Applications/me.mugzone.m.app"
ORIG="$P/orig/extracted/Payload/Malody Mobile.app/Malody Mobile"
for i in $(seq 1 900); do pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null || break; sleep 2; done
GRAY=$(python3 -I "$P/tools/make_gray_patch.py" "$ORIG" 0.75)
E3="0x100074630=1f410071:f803082a 0x100074634=69000054:12000014"
# E2/E3 は元バイト基準。既に適用済みなら apply_container.sh が旧バイト不一致で止まる(安全)
"$P/tools/apply_container.sh" $GRAY $E3 || echo "note: E2/E3 already applied or mismatch"
"$P/tools/apply_divide.sh"
# effect マネージャ + プレビュー(dylib) とフックのコードパッチ、FX ボタン(skin)
"$P/tools/apply_dylib.sh"
