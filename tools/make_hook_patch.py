#!/usr/bin/env python3
"""静的フック(ケイブ+スロット)の仕様を出力。各フックは 24B のケイブ:
   adrp x16,SLOTPAGE / ldr x16,[x16,#off] / cbz x16,ORIG / br x16 / ORIG: <元の先頭命令> / b ENTRY+4
 スロット(__DATA末尾の未使用領域)にdylibが関数ポインタを書く。slot==0 なら元の処理へ(dylib無しでも安全)。
 使い方: make_hook_patch.py <現在のバイナリ> -> apply_container.sh 用 spec"""
import os, struct, sys
BASE = 0x100000000
SLOT0 = 0x1016CF800
# (名前, 入口VA, ケイブVA)  ※ORIG(退避)= ケイブ+16 を dylib 側 bm.m と一致させる
HOOKS = [
    ("touch",   0x100075E60, 0x100005F60),
    ("refresh", 0x1001B0AF0, 0x100005E48),
    ("zoom",    0x1001B10C0, 0x100005E60),
    ("keytouch",0x1001B1804, 0x100005ED0),
    ("grid",    0x1001B1544, 0x100005EE8),
    ("layer",   0x1001B3E40, 0x100005F78),
    # EditorSlide(0x1008xxx): update=slot112 / zoom / touch lambda(tag105)  ※ケイブはヘッダ余白(0x5000〜)
    ("s_refresh",0x1000C5408, 0x100005000),
    ("s_zoom",   0x1000C5AB0, 0x100005018),
    ("s_touch",  0x1000C6744, 0x100005030),
    ("uiinit",   0x100073140, 0x100005048),   # EditorBase UI初期化(layoutオブジェクトを取得してEffectパネルのノードを結び付ける)
    ("openpath", 0x100020120, 0x100005060),   # nullsub_48(std::string*): 曲フォルダ等を開く(デスクトップ版のみの機能。ここで実装)
    ("lang",     0x1000B5980, 0x100005078),   # i18n 参照(sub_1000B5980): 言語表のポインタを控える
    ("addbtn",   0x10017E3D8, 0x100005090),   # 曲メニューのボタン追加(sub_10017E3D8): 「曲のフォルダを開く」を足す
    ("cfgtouch", 0x100019B8C, 0x1000050A8),   # 設定画面のタッチ処理(sub_100019B8C): 数値の手入力 / Master Volume
    ("cfgbuild", 0x1000174F8, 0x1000050C0),   # 設定画面の構築(sub_1000174F8): 構築後に追加ノードを結び付ける
    ("keydlg",   0x10001BDA8, 0x1000050D8),   # 設定画面のダイアログ結果(sub_10001BDA8): Custom 選択時に wav を取り込む
    ("setvol",   0x1001F50B4, 0x1000050F0),   # チャンネル音量の設定(sub_1001F50B4): Master Volume を掛ける
    ("kjudge",   0x1001197F8, 0x100005108),   # Key の押下/離し判定入口(sub_1001197F8): 遅ミスの素通し
    ("jtap",     0x10014C6A4, 0x100005120),   # 診断(予約): JudgeKey の押下判定(sub_10014C6A4)
    ("jtapfair", 0x10014C8B0, 0x100005138),   # 診断(予約): JudgeKeyFair の押下判定(sub_10014C8B0)
    ("kpress",   0x10005D060, 0x100005150),   # Key(ModeLayerKey)のレーン押下/離し(sub_10005D060): 遅ミスの素通し
    ("jrel",     0x10014C794, 0x100005168),   # JudgeKey の離し判定(sub_10014C794): LN の早離しは再ホールド不可
    ("modname",  0x1000A3924, 0x100005180),   # モッド名(sub_1000A3924): id 11 = "TURBO"
    ("modsel",   0x1000B0078, 0x100005198),   # MOD 画面のタッチ(sub_1000B0078): Turbo のメニュー
    ("modbuild", 0x1000AF494, 0x1000051B0),   # MOD 画面の構築(sub_1000AF494): 構築後に Turbo のメニューを結び付ける
    ("playstart", 0x10007D658, 0x100005220),  # 再生開始(sub_10007D658): Turbo (S) 音楽速度(既定の再生速度 a1+808 を差し替える)
    ("prepkey",  0x1000DD464, 0x100005238),   # Key の準備(sub_1000DD464): Turbo (S) のとき、ノーツの流れる速さ(a1+1236)を再生速度に合わせる
    ("pausetgl", 0x10007FBA8, 0x100005250),   # 一時停止/再開(sub_10007FBA8): Turbo (P) 一時停止の無効
    ("savescore",0x1001F78DC, 0x100005268),   # 結果の保存+送信(sub_1001F78DC): Turbo 中は保存も送信もしない(V と同じ「スコア非公認」)
    ("localrec", 0x1001458DC, 0x100005280),   # ローカル記録の保存(sub_1001458DC): 結果画面(戻り先 0x10002E4A4)からの保存は Turbo 中は行わない
    ("jtick",   0x10014C7E0, 0x100005298),   # 予約(未使用): JudgeKey の保持中の刻み(sub_10014C7E0)。Turbo (E) は見送り
    ("diffname",0x1001B5638, 0x1000052B0),   # 譜面名の生成(sub_1001B5638): 読み込み画面/リザルトでは Turbo の音楽速度(" 1.2x")を末尾に付ける
    ("slideprep",0x1000D0BD8, 0x1000052C8),  # ModeLayerSlide の準備(sub_1000D0BD8): Turbo (S) のとき、ノーツの流れる速さの補正(this+0x5e8)を Turbo の速度に合わせる
    ("slideprep2",0x1001163C8, 0x1000052E0), # 同じ構造の別クラス(sub_1001163C8)
    ("baseprep", 0x10010C1C4, 0x1000052F8),  # 全モード共通の準備(sub_10010C1C4): Turbo のとき判定窓を速度に比例させる(実時間で一定)
    ("ttouch",  0x100140664, 0x100005310),   # EditorTaiko の編集領域タッチ(sub_100140664): 小節線ツール
    ("trelayout",0x10013F280, 0x100005328),  # EditorTaiko の再配置(sub_10013F280): FX プレビュー(ノートの x を G で配置)
    ("trefresh", 0x10013EDF8, 0x100005340),  # EditorTaiko の毎フレーム(sub_10013EDF8)
    ("tbars",    0x10013FAE8, 0x100005358),  # EditorTaiko の小節線スプライト配置(sub_10013FAE8)
    ("tgrid",    0x1001402C4, 0x100005370),  # EditorTaiko のグリッド再構築(sub_1001402C4)
    ("tgridpos", 0x10013EF0C, 0x100005388),  # EditorTaiko のグリッド位置(sub_10013EF0C)
    ("snote",    0x1000C7224, 0x1000053A0),  # EditorSlide のノート(スプライト)へのタッチ(sub_1000C7224): 順番に選択でタップ順を控える
    ("pathhit",  0x100167514, 0x1000053B8),  # PathSpriteEditor の当たり判定(vtable +1128): seg の beat が重複するスライドの誤判定(どこを押しても当たり)を外す
    ("notecmp",  0x1001EB6A0, 0x1000053D0),  # ノートの並び順の比較(sub_1001EB6A0): 分数の大小を 32bit の掛け算で比べるため、分母が大きい(1048576 など)と桁あふれして時系列にならない。64bit で比べる
    ("s2wipe",   0x1000C5E10, 0x1000053E8),  # Slide → Wipe(sub_1000C5E10): オプションで、始点と seg の位置のまま Wipe にする(標準は分割グリッドへ再配置)
    ("segadd",   0x1000C6CA8, 0x100005400),  # Slide の seg 追加(sub_1000C6CA8): オプションで、終点より前の beat にも seg を置ける
    ("chit",     0x1001C8F60, 0x100005418),  # Slide の「その位置にノートがあるか」(sub_1000C8F60): 編集中のスライド自身の本体は「ノートあり」と扱わない(時系列でない seg を本体の上にも置けるように)
    ("store",    0x1001D7A24, 0x100005430),  # ストア API の要求組み立て(sub_1001D7A24): osu! ストア選択時は接続先 / key / uid を中継サーバー用に差し替えて呼ぶ
    ("storetouch",0x1000ACEA4, 0x100005448), # ストア画面のタッチ(sub_1000ACEA4, etag 112): 見出しの切替ボタン(tag 9)で Malody / osu! ストアを切り替える
    ("tcprecv",  0x100184C4C, 0x100005460),  # マルチ TCP の受信 1 フレーム(sub_100184C4C → sub_1001868EC): 実験用に生フレームを記録(公開版はスロット未設定=素通し)
]
if os.environ.get("BM_PUBLIC"): HOOKS = [h for h in HOOKS if h[0] != "tcprecv"]   # 実験用フック(最後尾=スロット番号がずれない)は公開版に入れない
def u32(x): return struct.pack("<I", x & 0xFFFFFFFF)
def adrp(rd, target, pc):
    imm = (((target & ~0xFFF) - (pc & ~0xFFF)) >> 12) & ((1 << 21) - 1)
    return 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd
def b(pc, target): return 0x14000000 | (((target - pc) >> 2) & 0x3FFFFFF)
d = open(sys.argv[1], "rb").read()
specs = []
def want(va, new):
    cur = d[va - BASE: va - BASE + len(new)]
    if cur != new: specs.append("0x%X=%s:%s" % (va, cur.hex(), new.hex()))
for i, (name, entry, cave) in enumerate(HOOKS):
    slot = SLOT0 + 8 * i
    cur = d[entry - BASE: entry - BASE + 4]
    if cur == u32(b(entry, cave)):
        # 既に適用済: 退避済み命令をケイブから読む
        orig = d[cave + 16 - BASE: cave + 20 - BASE]
    else:
        orig = cur
    ins = [adrp(16, slot, cave),
           0xF9400000 | ((slot & 0xFFF) // 8 << 10) | (16 << 5) | 16,
           0xB4000000 | (2 << 5) | 16,
           0xD61F0200,
           int.from_bytes(orig, "little"),
           b(cave + 20, entry + 4)]
    want(cave, b"".join(u32(x) for x in ins))
    want(entry, u32(b(entry, cave)))
# --- 譜面置き場を Documents 直下へ (sub_1001DF274 の kind 0): 基準 type 4(Library) -> 1(Documents)、追加文字列 "beatmap/" -> "" ---
#   0x1001DF40C  mov w1,#4      -> mov w1,#1
#   0x1001DF41C  add x1,x1,#0x824 ("beatmap/") -> add x1,x1,#0x82c (直後の NUL = 空文字列)
FILES_PATCH = [
    (0x1001DF40C, u32(0x52800021)),
    (0x1001DF41C, u32(0x91000000 | (0x82C << 10) | (1 << 5) | 1)),
]
# --- 読み込み画面/リザルトのモッド小アイコン(sub_1001F3A08): ビット 0〜10 のループを 0〜11 へ(CMP W22,#0xB -> #0xC) ---
FILES_PATCH.append((0x1001F3B58, u32(0x710032DF)))
for va, new in FILES_PATCH:
    want(va, new)
# --- MOD 画面: モッドを 11 -> 12 個に(TURBO = id 11)。順序表を余白へ移して 1 つ足し、ループ上限と表の参照を差し替える ---
MODTAB = 0x100005200
want(MODTAB, bytes([0, 1, 2, 3, 4, 5, 8, 6, 9, 7, 10, 11]))
want(0x1000AF99C, u32(adrp(8, MODTAB, 0x1000AF99C)))                 # ADRL X8, 順序表 (adrp)
want(0x1000AF9A0, u32(0x91000000 | ((MODTAB & 0xFFF) << 10) | (8 << 5) | 8))   # (add)
want(0x1000AFBA4, u32(0xF100327F))                                    # CMP X19, #0xB -> #0xC
print(" ".join(specs))
