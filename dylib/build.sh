#!/bin/bash
# BetterMalody.dylib をビルド(PlayCover 用: macabi / platform 6)。
set -euo pipefail
cd "$(dirname "$0")"
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.0.sdk
LS=third_party/lua/lua-5.4.7/src
LUASRC=""
for f in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib; do LUASRC="$LUASRC $LS/$f.c"; done
python3 -I ../tools/gen_builtin.py
clang -target arm64-apple-ios14.0-macabi -isysroot $SDK \
  -iframework $SDK/System/iOSSupport/System/Library/Frameworks -isystem $SDK/System/iOSSupport/usr/include \
  -dynamiclib -fobjc-arc -O2 ${BM_CFLAGS:-} -framework Foundation -framework Accelerate -framework ImageIO -framework CoreGraphics \
  -install_name @executable_path/BetterMalody.dylib -Ithird_party/lua/lua-5.4.7/src -o BetterMalody.dylib bm.m vorbis_impl.c lua_host.c zipx.c $LUASRC -lz -liconv
# アプリ本体/PlayTools と同じ UIKit パスへ(iOSSupport 側だと二重ロードになる)
install_name_tool -change /System/iOSSupport/System/Library/Frameworks/UIKit.framework/Versions/A/UIKit \
  /System/Library/Frameworks/UIKit.framework/UIKit BetterMalody.dylib
codesign -f -s - BetterMalody.dylib
echo built
