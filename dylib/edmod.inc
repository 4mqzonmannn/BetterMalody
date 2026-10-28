// ============================================================================
// エディタのテストプレイで MOD 画面を出す(issue #21。オプション bm.opt.ed_mod)
//   元の仕組み(IDA):
//     エディタのテストプレイ = EditorBase のタッチ(sub_100075E60)のタグ 202 → sub_100077EE0(editor)。プレイ設定(シングルトン sub_1000FF5E0)を作り直し
//       (sub_1000FF8D4 が [0] = 選んだモッドを 0 にする)、scene 3 を積む。モッドは sess[0] を、プレイ画面の初期化 sub_10007D18C の中の sub_1000FF730 が
//       sess[1](実際に使うモッド。+4)へ写す(モードに合わないビットは落とす)ので、標準ではエディタからのテストプレイは常にモッド無し
//     MOD 画面 = レイヤー 107(sub_1000544CC)。曲選択(sub_10010872C)は「プレイ情報の箱」(sub_1001E9B44 → sub_1001EA450 で置く)へ
//       ms(0x20 バイト: +0 モード種別 / +4 選択中のモッド / +8 使えないモッド / +12 スピード / +16 読み取り専用 / +24 譜面)を置いてから sub_100055374(mgr, 107, 100) で積む
//       MOD 画面は構築時(sub_1000AF494)に箱から ms を取り、戻るボタン(タグ 1, ev 0)で sub_100054D6C が閉じる。親には閉じた通知(vtable +1504)だけが行く
//   ここでの変更: テストプレイのボタン(タグ 202)を押すと、先に MOD 画面を積む。戻るボタンで閉じたら、選んだモッドを覚えて、テストプレイを始める
//     (プレイ画面の初期化の直前に sess[0] へ入れ、初期化のあとで元の値に戻す = 標準と同じ「テストプレイのあとモッド無し」の状態)
// ============================================================================
typedef intptr_t (*edplay_fn)(void *);
static edplay_fn orig_edplay;
static void *gEdModEd, *gEdModLayer;     // MOD 画面を出しているエディタ / そのレイヤー(NULL = 出していない)
static BOOL gEdPlayPending, gEdBypass;              // テストプレイを始める直前(プレイ画面の初期化で sess[0] へモッドを入れる)
static CFAbsoluteTime gEdPlayT;
static int gEdMods;
static BOOL gEdModsLoaded;

static void edmod_load(void) {
    if (gEdModsLoaded) return;
    gEdModsLoaded = YES;
    gEdMods = (int)[NSUserDefaults.standardUserDefaults integerForKey:@"bm.edit.mods"] & 0xFFF;
}
static uint8_t *edmod_sess(void) { return ((uint8_t *(*)(void))VA(0x1000FF5E0ULL))(); }
static void *edmod_box(void) { return ((void *(*)(void))VA(0x1001E9B44ULL))(); }

// プレイ画面の初期化(modeoffset.inc の hook_playinit)から。始める直前なら sess[0] へモッドを入れる
static BOOL edmod_play_begin(int *saved) {
    if (!gEdPlayPending) return NO;
    gEdPlayPending = NO;
    if (CFAbsoluteTimeGetCurrent() - gEdPlayT > 5.0) return NO;
    uint8_t *sess = edmod_sess();
    *saved = *(int *)sess;
    *(int *)sess = gEdMods;
    os_log(L, "edmod: play with mods=0x%x", gEdMods);
    return YES;
}
static void edmod_play_end(int saved) { *(int *)edmod_sess() = saved; }

// テストプレイ開始(sub_100077EE0)の入口。MOD 画面を積んだら YES(標準のテストプレイは始めない)
static BOOL edmod_open(void *ed) {
    if (gEdModEd) return YES;                                   // もう出ている
    edmod_load();
    uint8_t *e = (uint8_t *)ed;
    void *mgr = *(void **)(e + 632), *holder = *(void **)(e + 728);
    uint8_t *chart = holder ? ((uint8_t *(*)(void *))VA(0x1001C66E0ULL))(holder) : NULL;
    if (!mgr || !chart) return NO;
    *(void **)((uint8_t *)holder + 504) = chart;                 // sub_1001C66E0 は譜面を取り出すと holder[63] を空にする(標準のテストプレイが同じ関数で取る)ので、戻しておく
    int variant;                                                // 標準のテストプレイ(sub_100077EE0)と同じ判定
    switch (*(int *)(chart + 344)) {
        case 128: variant = 3; break;
        case 256: variant = 4; break;
        case 512: variant = 5; break;
        case 2048: variant = 6; break;
        case 4096: variant = 7; break;
        default: variant = *(int *)(edmod_sess() + 36); break;
    }
    if (variant == 3) return NO;                                // この種別は MOD 画面が操作できない(sub_1000B0078 の先頭で弾かれる)
    uint8_t *ms = calloc(1, 0x20);
    if (!ms) return NO;
    *(int *)ms = variant;
    *(int *)(ms + 4) = gEdMods;
    *(int *)(ms + 8) = 0x400;                                   // 曲選択と同じ「使えないモッド」
    *(int *)(ms + 12) = ((int (*)(void *))(*(void ***)chart)[12])(chart);   // 今のノーツスピード(vtable +96)
    *(void **)(ms + 24) = chart;
    ((void (*)(void *, void *))VA(0x1001EA450ULL))(edmod_box(), ms);
    void *layer = ((void *(*)(void *, int, long))VA(0x100055374ULL))(mgr, 107, 0x1B3);
    if (!layer) {                                               // 積めなかった: 箱に残った ms を回収して標準のテストプレイへ
        void *back = orig_playinfo(edmod_box());
        if (back == ms) free(ms);
        return NO;
    }
    gEdModEd = ed; gEdModLayer = layer;
    os_log(L, "edmod: MOD screen opened (mods=0x%x variant=%d)", gEdMods, variant);
    return YES;
}

// MOD 画面の戻るボタン(hook_modsel から。元の処理の前に呼び、YES なら元の処理のあとに edmod_after_back)
static BOOL edmod_before_back(void *layer, unsigned ev, void *node, EdModBack *bk) {
    if (!gEdModEd || layer != gEdModLayer || ev != 0 || !node || cfg_tag(node) != 1) return NO;
    uint8_t *ms = *(uint8_t **)((uint8_t *)layer + 688);
    if (!ms) { gEdModEd = gEdModLayer = NULL; return NO; }
    bk->ms = ms; bk->mods = *(int *)(ms + 4); bk->speed = *(int *)(ms + 12); bk->chart = *(void **)(ms + 24); bk->ed = gEdModEd;
    gEdModEd = gEdModLayer = NULL;
    return YES;
}
static void edmod_after_back(EdModBack *bk) {
    gEdMods = bk->mods & 0xFFF;
    [NSUserDefaults.standardUserDefaults setInteger:gEdMods forKey:@"bm.edit.mods"];
    if (bk->chart) ((void (*)(void *, int))(*(void ***)bk->chart)[13])(bk->chart, bk->speed);   // 標準の閉じ処理(曲選択)と同じ: スピードを譜面へ(vtable +104)
    void *back = orig_playinfo(edmod_box());                              // 親が取らなかった ms(箱に戻されている)を回収
    if (back == (void *)bk->ms) free(bk->ms);
    else if (back) ((void (*)(void *, void *))VA(0x1001EA450ULL))(edmod_box(), back);
    void *ed = bk->ed;
    os_log(L, "edmod: MOD screen closed (mods=0x%x speed=%d) -> test play", gEdMods, bk->speed);
    dispatch_async(dispatch_get_main_queue(), ^{
        gEdPlayPending = YES; gEdPlayT = CFAbsoluteTimeGetCurrent();
        os_log(L, "edmod: launching test play ed=%p", ed);
        gEdBypass = YES;
        orig_edplay(ed);                                        // 標準のテストプレイ開始
        gEdBypass = NO;
    });
}

static intptr_t hook_edplay(void *ed) {   // sub_100077EE0: ダイアログ(開始位置の選択)の結果から呼ばれる
    if (!gEdBypass && bm_opt(BMO_ED_MOD) && edmod_open(ed)) return 0;
    return orig_edplay(ed);
}
