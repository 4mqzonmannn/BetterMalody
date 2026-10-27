// ============================================================================
// モードごとのオフセット: 判定の Offset と、表示の Hitposition(標準の名前は Hit offset / Graphic offset)を、モードごとに持つ
//   元の仕組み(IDA): 設定 config+160 = 判定のオフセット / +164 = 表示のオフセット(どちらも ms の int。全モード共通)
//     プレイの画面 sub_10007D18C(初期化): this+856 = config+160 / this+860 = config+164 + this+856 を控える。毎フレーム sub_100080228 が
//     時刻 = 音 + 譜面のオフセット + this+856(判定)/ + this+860(表示)で使う。this+712 = 今の譜面の入れ物 → +8 = MaChart → +436 = モード(meta の mode)
//   ここでの変更: プレイの初期化の後で、this+856 / +860 をモードごとの値に書き換える(それ以外は標準のまま)
//     モードごとの値は NSUserDefaults(bm.offset.<mode> / bm.hitpos.<mode>)。まだ決めていないモード(と、下の表に無いモード)は、標準の config の値のまま
//     config+160 / +164 は他の画面(校正など)が変えうるので、控えとの差が出たら、決めてある全モードの値に同じ差を足す
// ============================================================================
#define OF_N 7
static const struct { int mode; const char *name; } OF_ROW[OF_N] = {
    { 0, "Key" }, { 3, "Catch" }, { 4, "Pad" }, { 5, "Taiko" }, { 6, "Ring" }, { 7, "Slide" }, { 8, "Live" } };   // 譜面の meta の mode
#define OF_MAX 500
static int gOfVal[2][OF_N], gOfMirror[2];
static BOOL gOfHas[2][OF_N], gOfLoaded;

static NSString *of_key(int g, int i) { return [NSString stringWithFormat:g ? @"bm.hitpos.%d" : @"bm.offset.%d", OF_ROW[i].mode]; }
static int of_cfg(int g) { return *(int *)(((uint8_t *(*)(void))VA(0x1001DFA40ULL))() + (g ? 164 : 160)); }
static int of_clamp(int v) { return v < -OF_MAX ? -OF_MAX : (v > OF_MAX ? OF_MAX : v); }
static void of_sync(void) {   // 初回は保存値を読む。以後は、config の値が控えから動いていたら、決めてある全モードへ同じ差を足す
    if (!gOfLoaded) {
        for (int g = 0; g < 2; g++) {
            gOfMirror[g] = of_cfg(g);
            for (int i = 0; i < OF_N; i++) {
                id v = [NSUserDefaults.standardUserDefaults objectForKey:of_key(g, i)];
                if (v) { gOfHas[g][i] = YES; gOfVal[g][i] = of_clamp([v intValue]); }
            }
        }
        gOfLoaded = YES;
        return;
    }
    for (int g = 0; g < 2; g++) {
        int cur = of_cfg(g), d = cur - gOfMirror[g];
        if (!d) continue;
        for (int i = 0; i < OF_N; i++) if (gOfHas[g][i]) {
            gOfVal[g][i] = of_clamp(gOfVal[g][i] + d);
            [NSUserDefaults.standardUserDefaults setInteger:gOfVal[g][i] forKey:of_key(g, i)];
        }
        gOfMirror[g] = cur;
    }
}
static int of_index(int mode) { for (int i = 0; i < OF_N; i++) if (OF_ROW[i].mode == mode) return i; return -1; }
static int of_get(int g, int i) { of_sync(); return gOfHas[g][i] ? gOfVal[g][i] : of_cfg(g); }
static void of_set(int g, int i, int v) {
    of_sync();
    gOfHas[g][i] = YES; gOfVal[g][i] = of_clamp(v);
    [NSUserDefaults.standardUserDefaults setInteger:gOfVal[g][i] forKey:of_key(g, i)];
}

typedef intptr_t (*playinit_fn)(void *);
static playinit_fn orig_playinit;
static intptr_t hook_playinit(void *scene) {   // sub_10007D18C: プレイの画面の初期化の後で、判定 / 表示のオフセットをモードごとの値にする
    intptr_t r = orig_playinit(scene);
    uint8_t *sess = *(uint8_t **)((uint8_t *)scene + 712);
    uint8_t *chart = sess ? *(uint8_t **)(sess + 8) : NULL;
    if (!chart) return r;
    int mode = *(int *)(chart + 436), i = of_index(mode);
    if (i >= 0) {
        int off = of_get(0, i), hp = of_get(1, i);
        *(int *)((uint8_t *)scene + 856) = off;
        *(int *)((uint8_t *)scene + 860) = off + hp;
    }
#ifndef BM_PUBLIC
    {   // 確かめ用の記録(非公開ビルドのみ)
        NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSString *path = [docs stringByAppendingPathComponent:@"offset_log.txt"];
        NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
        if (!fh) { [NSFileManager.defaultManager createFileAtPath:path contents:nil attributes:nil]; fh = [NSFileHandle fileHandleForWritingAtPath:path]; }
        [fh seekToEndOfFile];
        [fh writeData:[[NSString stringWithFormat:@"play: mode=%d idx=%d offset=%d display=%d (cfg %d / %d)\n", mode, i,
                        *(int *)((uint8_t *)scene + 856), *(int *)((uint8_t *)scene + 860), of_cfg(0), of_cfg(1)] dataUsingEncoding:NSUTF8StringEncoding]];
        [fh closeFile];
    }
#endif
    return r;
}
