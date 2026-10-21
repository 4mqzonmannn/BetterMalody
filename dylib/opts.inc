// BetterMalody のエディタ設定(すべてオプトイン。既定は OFF = 標準の動き)。設定画面の「Editor options」ボタンで切り替える
typedef struct { const char *key, *title; BOOL on; } BmOpt;
enum { BMO_REMOVE_CUT, BMO_COUNT };
static BmOpt gBmOpts[BMO_COUNT] = {
    { "bm.opt.remove_cut", "Slide Remove tool: cut after touched point", YES },   // 既定 ON(Mac では CUI が押せないため。保存済みの値があればそちら)
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
static void bm_opts_menu(void) {
    (void)bm_opt(0);
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    NSMutableArray<NSNumber *> *styles = [NSMutableArray array];
    for (int k = 0; k < BMO_COUNT; k++) { [titles addObject:[NSString stringWithFormat:@"%@ %s", gBmOpts[k].on ? @"[ON] " : @"[OFF]", gBmOpts[k].title]]; [styles addObject:@0]; }
    [titles addObject:@"Close"]; [styles addObject:@1];
    pl_alert(@"Editor options", @"Tap an item to switch it. Changes are saved.", nil, titles, styles, ^(NSInteger i, NSString *t) {
        if (i < 0 || i >= BMO_COUNT) return;
        gBmOpts[i].on = !gBmOpts[i].on;
        [NSUserDefaults.standardUserDefaults setBool:gBmOpts[i].on forKey:@(gBmOpts[i].key)];
        [NSUserDefaults.standardUserDefaults synchronize];
        dispatch_async(dispatch_get_main_queue(), ^{ bm_opts_menu(); });
    });
}
