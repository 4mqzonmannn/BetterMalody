#!/usr/bin/env python3
"""dylib/builtin/*.lua を C の文字列表(dylib/builtin_gen.inc)にする。起動時に Documents/export_plugins/BetterMalody/ へ書き出される組み込みプラグイン。"""
import os, sys
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dylib")
src = os.path.join(d, "builtin")
def lit(b):
    out = []
    for c in b:
        if c == 0x5c: out.append("\\\\")
        elif c == 0x22: out.append('\\"')
        elif c == 0x0a: out.append("\\n\"\n    \"")
        elif c == 0x09: out.append("\\t")
        elif c == 0x0d: out.append("\\r")
        elif 0x20 <= c < 0x7f: out.append(chr(c))
        else: out.append("\\%03o" % c)
    return "".join(out)
rows = []
for f in sorted(os.listdir(src)):
    if f.endswith(".lua"):
        rows.append('    { "%s",\n    "%s" },' % (f, lit(open(os.path.join(src, f), "rb").read())))
text = "// 自動生成(tools/gen_builtin.py): dylib/builtin/*.lua\nstatic const struct { const char *name, *src; } kBuiltinPlugins[] = {\n" + "\n".join(rows) + "\n};\n"
out = os.path.join(d, "builtin_gen.inc")
if not os.path.exists(out) or open(out).read() != text: open(out, "w").write(text)
