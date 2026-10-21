// BetterMalody のエディタ設定(オプトインの機能。Mac では設定画面の CUI が押せないため、現在は既定 ON。保存した値があればそちらが優先)。設定画面の「Editor options」ボタンで切り替える
typedef struct { const char *key, *title; BOOL on; } BmOpt;
// 公開版(tools/build_ipa.sh が -DBM_PUBLIC を付ける)は標準の感覚を優先して全部 OFF。Mac のテスト用(apply_dylib.sh)は ON(Mac では設定画面の CUI が押せないため)
#ifdef BM_PUBLIC
#define BMO_DEF(v) NO
#else
#define BMO_DEF(v) v
#endif
enum { BMO_REMOVE_CUT, BMO_S2W, BMO_SEG_ANY, BMO_SEG_VERT, BMO_SEG_HIT, BMO_COUNT };
static BmOpt gBmOpts[BMO_COUNT] = {
    { "bm.opt.remove_cut", "Slide Remove tool: cut after touched point", YES },   // 公開版も既定 ON(v1.0.5 から。issue #9 の修正)。保存済みの値があればそちら
    { "bm.opt.s2w", "Slide to Wipe: keep start and seg positions", BMO_DEF(YES) },
    { "bm.opt.seg_any", "Allow segs not in time order (tap before end adds a seg)", BMO_DEF(YES) },
    { "bm.opt.seg_vert", "Slide modify: drag a seg vertically (beat)", BMO_DEF(YES) },
    { "bm.opt.seg_hit", "Slide modify: pick the seg nearest to the touch", BMO_DEF(YES) },
};
static BOOL bm_opt(int i) {
    static BOOL loaded;
    if (!loaded) {
        loaded = YES;
        for (int k = 0; k < BMO_COUNT; k++) {
            id v = [NSUserDefaults.standardUserDefaults objectForKey:@(gBmOpts[k].key)];
            if (v) gBmOpts[k].on = [v boolValue];                    // 保存が無ければ既定値のまま
        }
    }
    return gBmOpts[i].on;
}
static void kb_menu(void);
static void bm_opts_menu(void) {
    (void)bm_opt(0);
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    NSMutableArray<NSNumber *> *styles = [NSMutableArray array];
    for (int k = 0; k < BMO_COUNT; k++) { [titles addObject:[NSString stringWithFormat:@"%@ %s", gBmOpts[k].on ? @"[ON] " : @"[OFF]", gBmOpts[k].title]]; [styles addObject:@0]; }
    [titles addObject:@"Keyboard shortcuts..."]; [styles addObject:@0];
    [titles addObject:@"Close"]; [styles addObject:@1];
    pl_alert(@"Editor options", @"Tap an item to switch it. Changes are saved.", nil, titles, styles, ^(NSInteger i, NSString *t) {
        if (i == BMO_COUNT) { kb_menu(); return; }
        if (i < 0 || i >= BMO_COUNT) return;
        gBmOpts[i].on = !gBmOpts[i].on;
        [NSUserDefaults.standardUserDefaults setBool:gBmOpts[i].on forKey:@(gBmOpts[i].key)];
        [NSUserDefaults.standardUserDefaults synchronize];
        dispatch_async(dispatch_get_main_queue(), ^{ bm_opts_menu(); });
    });
}
