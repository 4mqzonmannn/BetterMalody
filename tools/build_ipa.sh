#!/bin/bash
# 実機(iPhone)向けの BetterMalody.ipa を、元 IPA(orig/Malody_Mobile_4.3.7.ipa)から作る。
#   バイナリのコードパッチ(灰色 / 保存値丸め解除 / 分割メニュー / フック)+ dylib(iOS プラットフォーム化)+ skin 差し替え + アプリ名・バンドル ID の書き換え。
#   署名は ad-hoc(配布先の Sideloadly / AltStore などが自分の証明書で署名し直す)。
# 使い方: tools/build_ipa.sh [出力 IPA パス(既定 build/BetterMalody_v1.0.6.ipa)]
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$P/build/BetterMalody_v1.0.6.ipa}"
BUNDLE_ID="me.bettermalody.m"
APP_NAME="BetterMalody"
ST="$P/build/stage"
rm -rf "$ST"; mkdir -p "$ST" "$(dirname "$OUT")"
unzip -q "$P/orig/Malody_Mobile_4.3.7.ipa" -d "$ST"
APP="$ST/Payload/Malody Mobile.app"; BIN="$APP/Malody Mobile"
ORIG_SKIN="$P/orig/extracted/Payload/Malody Mobile.app/skin-ios.bin"

# 1) バイナリのコードパッチ(順番に。各 make_*_patch は現在のバイナリを見て差分だけを出す)
patch() { local T; T=$(mktemp "$ST/tmp.XXXXXX"); python3 -I "$P/tools/patch_bytes.py" "$BIN" "$T" "$@" >/dev/null; cp "$T" "$BIN"; rm -f "$T"; }
patch $(python3 -I "$P/tools/make_gray_patch.py" "$BIN" 0.75) 0x100074630=1f410071:f803082a 0x100074634=69000054:12000014
SPECS=$(python3 -I "$P/tools/make_divide_patch.py" "$BIN"); [ -z "$SPECS" ] || patch $SPECS
SPECS=$(python3 -I "$P/tools/make_hook_patch.py" "$BIN");   [ -z "$SPECS" ] || patch $SPECS
SPECS=$(python3 -I "$P/tools/make_slow_patch.py" "$BIN");   [ -z "$SPECS" ] || patch $SPECS   # スロー再生の下限 0.5 → 0.125

# 2) skin(分割メニュー → FX → 設定 → モジュール → Taiko の順に拡張)
W="$P/build/skin"; rm -rf "$W"; mkdir -p "$W"
mkdir -p "$P/work"; python3 -I "$P/tools/make_labels.py" "$P/work/labels"
python3 -I "$P/tools/build_skin_divide.py" "$ORIG_SKIN" "$P/work/labels" "$W/1.bin"
python3 -I "$P/tools/build_skin_fx.py" "$W/1.bin" "$W/2.bin"
python3 -I "$P/tools/build_skin_cfg.py" "$W/2.bin" "$W/3.bin"
python3 -I "$P/tools/build_skin_mod.py" "$W/3.bin" "$W/4.bin"
python3 -I "$P/tools/build_skin_taiko.py" "$W/4.bin" "$W/5.bin"
cp "$W/5.bin" "$APP/skin-ios.bin"

# 3) dylib(macabi でビルド → iOS 用に変換: フレームワークのパスを iOS 形式へ、プラットフォームを iOS(2)へ)
BM_CFLAGS=-DBM_PUBLIC "$P/dylib/build.sh" >/dev/null
D="$APP/BetterMalody.dylib"; cp "$P/dylib/BetterMalody.dylib" "$D"
for f in Foundation Accelerate ImageIO CoreGraphics CoreFoundation; do
  OLD=$(otool -L "$D" | awk -v f="$f.framework" '$1 ~ f {print $1}' | head -1)
  [ -z "$OLD" ] || install_name_tool -change "$OLD" "/System/Library/Frameworks/$f.framework/$f" "$D"
done
vtool -set-build-version 2 14.0 18.0 -replace -output "$D.tmp" "$D" >/dev/null && mv "$D.tmp" "$D"
codesign -f -s - "$D" >/dev/null 2>&1
optool install -c load -p "@executable_path/BetterMalody.dylib" -t "$BIN" >/dev/null
python3 -I - "$BIN" <<'PY'
# optool は LC_LOAD_UPWARD_DYLIB を入れるので、実機の dyld でも確実な通常の LC_LOAD_DYLIB(0xC)へ書き換える
import struct, sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
ncmds, = struct.unpack_from("<I", d, 16); off = 32; hit = 0
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", d, off)
    if cmd == 0x80000023 and b"BetterMalody.dylib" in bytes(d[off:off + size]):
        struct.pack_into("<I", d, off, 0xC); hit += 1
    off += size
assert hit == 1, hit
open(p, "wb").write(d)
PY

# 4) アプリ名 / バンドル ID
PL="$APP/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $BUNDLE_ID" -c "Set :CFBundleName $APP_NAME" -c "Set :CFBundleDisplayName $APP_NAME" \
  -c "Set :LSSupportsOpeningDocumentsInPlace true" "$PL"

# 5) 旧署名 / DRM 残骸を除いて ad-hoc 署名し、IPA に固める
rm -rf "$APP/_CodeSignature" "$APP/SC_Info"
codesign -f -s - "$APP" >/dev/null 2>&1
rm -f "$OUT"; (cd "$ST" && zip -qry "$OUT" Payload)
rm -rf "$ST" "$W"
echo "built: $OUT ($(du -h "$OUT" | cut -f1))"
