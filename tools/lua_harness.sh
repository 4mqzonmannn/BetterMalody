#!/bin/bash
# usage: tools/lua_harness.sh <mode 0|7> plugin.lua...  (ゲーム無しで Lua プラグインを通す)
set -euo pipefail
D="$(cd "$(dirname "$0")/.." && pwd)/dylib"
LS=$D/third_party/lua/lua-5.4.7/src
OUT=${TMPDIR:-/tmp}/bm_lua_harness
if [ ! -x "$OUT" ] || [ "$D/lua_host.c" -nt "$OUT" ] || [ "$D/lua_host.h" -nt "$OUT" ] || [ "$0" -nt "$OUT" ] || [ "$(dirname "$0")/lua_harness.c" -nt "$OUT" ]; then
  SRC=""; for f in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib; do SRC="$SRC $LS/$f.c"; done
  clang -O1 -w -I"$D" -I"$LS" -o "$OUT" "$(dirname "$0")/lua_harness.c" "$D/lua_host.c" $SRC
fi
exec "$OUT" "$@"
