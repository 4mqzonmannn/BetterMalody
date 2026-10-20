# アドレス台帳 (iOS 4.3.7, imagebase 0x100000000)

状態: ✅=デコンパイル/逆アセンブルで確認 / ❓=推定

## effect / Processor (Androidと同一ロジック ✅)
| 役割 | iOS | Android | 状態 |
|---|---|---|---|
| effectパーサ (beat/scroll/hs/sign/jump/showbar/ggt) | `sub_100064540` | `sub_24D7D8` | ✅ |
| JSONキー取得ヘルパ | `sub_1000644C8` | - | ✅ |
| effectライタ | `sub_1001D2CF4` | `sub_211520` | ❓ |
| effect構造体vtable (112B) | `0x10091B850` | - | ✅ |
| Processor::Process(bool) | `sub_10016DED0` | `sub_2627E0` | ✅ |
| 初期carrier | `sub_10016E870` | - | ✅ |
| effect点carrier | `sub_10016E930` | `sub_25DD90` | ✅ |
| BPM点carrier | `sub_10016EB58` | `sub_25DC10` | ✅ |
| scale算出 | `sub_10016F8F4` | - | ✅ |
| ref算出(時間重み33%分位) | `sub_10016E6C0` | `sub_2624E0` | ✅ |
| carrier二分探索 | `sub_10016F704` | `sub_25D8E0` | ✅ |
| ProcessorSlide slot2 | `sub_100200170` | `0x265054` | ✅ |

### Processor フィールド
+8 meta / +16 notes / +24 BPM点vec(48B,+24=time,+40=拍長ms) / +32 effectvec(112B,+24=time) /
+40 ref(double) / +48 userSpeed / +56 固定速 / +57 固定速時scroll適用 / +58 hs無視 / +59 hsのBPM補正 /
+60 小節線計算 / +120 carrier deque(16B要素)

### carrier (0x50B, shared_ptr管理, obj=+24)
+0 t0(double) / +8 y0(double) / +24 scale(float) / +28 scroll(float) / +32 hs(float) / +36,37 showbar系

## vtable (typeinfo参照+8 = slot0)
Processor 0x100922F80 / ProcessorOld 0x100922FB8 / ProcessorSlide 0x100934990 /
ProcessorTaiko 0x100909F60 / ProcessorCatch 0x100933448 / EditorKey 0x10092BF28(多重継承) /
EditorToolbar 0x100934710 (slot1=強調, slot2=tag→値)

## エディタ ツールバー ✅
- `sub_10007296C` = EditorBase ctor。メンバ: +1272 ToolbarDivide(vt 0x100934798) / +1304 ToolbarSpeed(0x100934750) / +1336 ToolbarNote(0x1009347E8)
- 基底slot0 `sub_1001FDCAC`: 親Nodeの子のうち tag>=1 をボタンvectorへ(ボタン/ラベルはレイアウト資源側)
- Divide: 強調 `sub_1001FDE70` / tag400..408→値 `sub_1001FDF90` / 値表 `dword_10080AFA0` = [2,3,4,6,8,12,16,24,32](範囲外=4)
- Speed: tag500→100, 501→75, 502→50 (`sub_1001FDDC0`, 即値) / 強調 `sub_1001FDD50`

## Processor::Process 呼出元 (全て仮想メソッド, EditorKeyの外)
`sub_1000D1CFC` / `sub_1000DD848` / `sub_10011780C` / `sub_1001A8CFC`(MaChart/ChartOSU変換)

## EditorKey (実装は0x1001B4xxx付近)
+1544/+1568 ノートリスト2本、+1592..1600 収集配列、`sub_1001B2A9C(ed,col,note)` = Scale3Sprite/Sprite別の配置ディスパッチ(yではない)

## 未特定
- エディタのノートy算出経路(拍→y)
- 選択・Undo/Redo・コピペの実装
- ボタンラベルの出所(skin-ios.bin 等のレイアウト資源と推定)

## 曲選択メニュー(2026-10-16)
- `sub_10010872C`(case 4)= 「何をしますか?」ダイアログ構築。lang 1031 題、ボタン: 139 共有(id209) / 100 曲削除(200) / 102 譜面削除(201) / 103 ハイスコア削除(202) / (104→203,123→204,109→205,126→206,110→207 は条件付き)
- ダイアログ追加ボタン `sub_10017E3D8(dlg, text, sub, id)` / ダイアログ内ボタン処理 `sub_10010A114(scene, id)`(case 208 = 曲フォルダを開く, 実処理 nullsub_48)
- i18n `sub_1000B5980(langTable@x0, id@w1, sret@x8)`、langTable = scene+656。曲リストのフォルダ項目 `sub_1001DB05C` も nullsub_48 を使う。
