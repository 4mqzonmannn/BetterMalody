#!/usr/bin/env python3
"""Malody 無印 skin-*.bin ("Scorpio" パッケージ) の解析/再構築。読み取り専用の list/dump と pack を提供。
形式(IDA sub_1000C02E0 で確認):
  header : "oiprocS\\0"(8) | u32 version(=1) | u32 count
  entry  : 8B marker FFAC9C908D8F9690(LE) | u32 keylen | key(ascii) | u32 size | u32 a | u32 b | data[size]
使い方: python3 -I skinpkg.py list <bin> | grep <bin> <text> | dump <bin> <key> <out> | replace <bin> <key> <newfile> <out>
"""
import struct, sys, hashlib

MARK = bytes.fromhex("90968f8d909cacff")
MAGIC = b"oiprocS\x00"

def parse(d):
    assert d[:8] == MAGIC, "bad magic"
    ver, cnt = struct.unpack_from("<II", d, 8)
    p, out = 16, []
    while p < len(d):
        assert d[p:p+8] == MARK, f"bad marker at {p:#x}"
        kl, = struct.unpack_from("<I", d, p+8)
        key = d[p+12:p+12+kl].decode("ascii")
        q = p + 12 + kl
        size, a, b = struct.unpack_from("<III", d, q)
        out.append(dict(key=key, size=size, a=a, b=b, hdr=p, data=q+12))
        p = q + 12 + size
    return ver, cnt, out

def replace(d, key, newdata):
    """key のエントリのデータを newdata に差し替えた全体バイト列を返す(サイズ欄を更新)。"""
    ver, cnt, ents = parse(d)
    for e in ents:
        if e["key"] == key:
            size_off = e["data"] - 12
            out = bytearray(d[:size_off])
            out += struct.pack("<III", len(newdata), e["a"], e["b"])
            out += newdata
            out += d[e["data"] + e["size"]:]
            return bytes(out)
    raise KeyError(key)

def add_entries(d, items, a=1, b=4):
    """items=[(ファイル名, bytes)] を末尾に追加(キー=SHA-1(名前))。件数欄を更新。既存キーなら置換。"""
    import hashlib
    ver, cnt, ents = parse(d)
    out = bytearray(d)
    have = {e["key"] for e in ents}
    added = 0
    for name, data in items:
        key = hashlib.sha1(name.encode()).hexdigest()
        if key in have:
            out = bytearray(replace(bytes(out), key, data)); continue
        out += MARK + struct.pack("<I", len(key)) + key.encode() + struct.pack("<III", len(data), a, b) + data
        added += 1
    struct.pack_into("<I", out, 12, cnt + added)
    return bytes(out)

def main():
    cmd, path = sys.argv[1], sys.argv[2]
    d = open(path, "rb").read()
    ver, cnt, ents = parse(d)
    if cmd == "list":
        print(f"version={ver} count={cnt} parsed={len(ents)} bytes={len(d)}")
        for e in ents:
            head = d[e["data"]:e["data"]+8]
            kind = "PNG" if head[:4] == b"\x89PNG" else ("TXT" if all(32 <= c < 127 or c in (9,10,13) for c in head) else "BIN")
            print(f'{e["key"]} size={e["size"]:>9} a={e["a"]} b={e["b"]} off={e["data"]:#x} {kind}')
    elif cmd == "grep":
        needle = sys.argv[3].encode()
        for e in ents:
            if needle in d[e["data"]:e["data"]+e["size"]]:
                print(e["key"], e["size"], hex(e["data"]))
    elif cmd == "replace":          # replace <in> <key> <newfile> <out>
        key, newf, outf = sys.argv[3], sys.argv[4], sys.argv[5]
        out = replace(d, key, open(newf, "rb").read())
        open(outf, "wb").write(out); print("ok", len(d), "->", len(out))
    elif cmd == "dump":
        for e in ents:
            if e["key"] == sys.argv[3]:
                open(sys.argv[4], "wb").write(d[e["data"]:e["data"]+e["size"]]); print("ok", e["size"]); return
        raise SystemExit("key not found")

if __name__ == "__main__":
    main()
