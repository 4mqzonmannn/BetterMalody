// ---- 設定画面の改善(スキン: tools/build_skin_cfg.py が script_config.mui へ追加するノードを結び付ける) ----
//  ・スライダーの値(数字)をタップ → 小パネルで数値を手入力(Music / Effect / Hit offset / Graphic offset / Column angle / Master)
//  ・Master Volume: 全チャンネル音量(sub_1001F50B4)へ掛ける。値は NSUserDefaults(bm.master_volume)。スライダー行は Audio の先頭
//  ・Custom key sound を選んだときに skin/custom.wav が無ければ、ファイル選択シートで wav を取り込む
// 画面のノードはタグで探す: 80..85 = 値の当たり判定 / 90 = Master の背景(操作面) / 96 = バー / 97 = 値 /
//   91 = OK / 92 = Cancel / 93 = 見出し / 94 = 入力欄 / 99 = パネル全体(暗幕)
#define CFG_TAG_HIT0   80
#define CFG_TAG_MBG    90
#define CFG_TAG_OK     91
#define CFG_TAG_CANCEL 92
#define CFG_TAG_TITLE  93
#define CFG_TAG_INPUT  94
#define CFG_TAG_MBAR   96
#define CFG_TAG_MLBL   97
#define CFG_TAG_PANEL  99
#define CFG_TAG_OSU    100   // 「osu! Store」ボタン(設定画面の最下段)
#define CFG_TAG_OPTS   101   // 「Editor options」ボタン

static int gMasterVol = 100;
static struct { void *scene, *mbar, *mlbl, *panel, *title, *tb; int target; } gCfg;

// 値の種類: 0 Music / 1 Effect / 2 Hit offset / 3 Graphic offset / 4 Column angle / 5 Master
static const char *const CFG_NAME[6] = { "Music Volume (0-100)", "Effect Volume (0-100)", "Hit Offset (ms)",
                                          "Graphic Offset (ms)", "Column Angle (30-50)", "Master Volume (0-100)" };
static const int CFG_MIN[6] = { 0, 0, -500, -500, 30, 0 }, CFG_MAX[6] = { 100, 100, 500, 500, 50, 100 };

static uint8_t *cfg_conf(void) { return ((uint8_t *(*)(void *))VA(0x1001DFA40ULL))(NULL); }   // 設定の本体(config.json の読み込み先)
static int cfg_tag(void *n) { return ((int (*)(void *))(*(void ***)n)[0x2a0 / 8])(n); }
static void cfg_walk(void *n, int depth, int *count) {
    if (!n || depth > 10 || *count > 4000) return;
    (*count)++;
    int t = cfg_tag(n);
    if (t == CFG_TAG_MBAR && !gCfg.mbar) gCfg.mbar = n;
    else if (t == CFG_TAG_MLBL && !gCfg.mlbl) gCfg.mlbl = n;
    else if (t == CFG_TAG_PANEL && !gCfg.panel) gCfg.panel = n;           // 暗幕の子も 99 だが、親(パネル全体)が先に見つかる
    else if (t == CFG_TAG_TITLE && !gCfg.title) gCfg.title = n;
    else if (t == CFG_TAG_INPUT && !gCfg.tb) {
        void *tb = cast_to(n, 0x1008F0B00ULL);                              // _cfgpin(Sprite)と _cfgpit(Textbox)が同じタグ
        if (tb) gCfg.tb = tb;
    }
    nvec *ch = node_children(n);
    if (!ch) return;
    for (void **p = ch->begin; p != ch->end; p++) cfg_walk(*p, depth + 1, count);
}
static void **cfg_vec(void *scene, int off) { return *(void ***)((uint8_t *)scene + off); }   // 設定画面の std::vector<Node*>(+880 = バー / +904 = 値の文字)

static int cfg_get(int t) {
    uint8_t *c = cfg_conf();
    switch (t) {
        case 0: return *(int *)(c + 152);
        case 1: return *(int *)(c + 156);
        case 2: return *(int *)(c + 160);
        case 3: return *(int *)(c + 164);
        case 4: return c[238];
        default: return gMasterVol;
    }
}
static float cfg_pct(int t, int v) {
    switch (t) {
        case 2: case 3: return (v + 200) * 0.25f;
        case 4: return (v - 30) * 100.0f / 20.0f;
        default: return (float)v;
    }
}
static void cfg_apply_music(void) {
    void *m = ((void *(*)(void))VA(0x1000FDBA0ULL))();
    ((void (*)(void *))VA(0x1000FE6CCULL))(m);                              // 標準のスライダー(Music)と同じ再適用
}
static void cfg_show(int t, int v) {   // バーと値の文字を更新
    void *bar = NULL, *lbl = NULL;
    if (t == 5) { bar = gCfg.mbar; lbl = gCfg.mlbl; }
    else if (gCfg.scene) {
        void **b = cfg_vec(gCfg.scene, 880), **l = cfg_vec(gCfg.scene, 904);
        bar = b ? b[t] : NULL; lbl = l ? l[t] : NULL;
    }
    if (bar) ((void (*)(void *, float))VA(0x100166D10ULL))(bar, cfg_pct(t, v));
    if (lbl) { char s[16]; snprintf(s, sizeof s, "%d", v); set_label_text(lbl, s); }
}
static void cfg_set(int t, int v) {
    if (v < CFG_MIN[t]) v = CFG_MIN[t];
    if (v > CFG_MAX[t]) v = CFG_MAX[t];
    uint8_t *c = cfg_conf();
    switch (t) {
        case 0: *(int *)(c + 152) = v; break;
        case 1: *(int *)(c + 156) = v; break;
        case 2: *(int *)(c + 160) = v; break;
        case 3: *(int *)(c + 164) = v; break;
        case 4: c[238] = (uint8_t)v; break;
        default:
            gMasterVol = v;
            [NSUserDefaults.standardUserDefaults setInteger:v forKey:@"bm.master_volume"];
    }
    if (gCfg.scene) *((uint8_t *)gCfg.scene + 680) = 1;                       // 変更あり(標準のトグルと同じ印)
    cfg_show(t, v);
    if (t == 0 || t == 5) cfg_apply_music();
}

static void cfg_panel(BOOL show) {
    if (!gCfg.panel) return;
    if (!show && gCfg.tb) ((void (*)(void *))VA(0x10006D2E0ULL))(gCfg.tb);   // 入力中なら確定して編集状態を終える
    VT(gCfg.panel, 336, void (*)(void *, int))(gCfg.panel, show ? 1 : 0);
}
static void cfg_open_input(int t) {
    if (!gCfg.panel || !gCfg.tb) return;
    gCfg.target = t;
    if (gCfg.title) set_label_text(gCfg.title, CFG_NAME[t]);
    ((void (*)(void *))VA(0x10006D2E0ULL))(gCfg.tb);
    tb_set(gCfg.tb, [NSString stringWithFormat:@"%d", cfg_get(t)]);
    cfg_panel(YES);
    ((void (*)(void *))VA(0x10006DA24ULL))(gCfg.tb);                          // 入力開始(キーボード)
}

typedef intptr_t (*cfgbuild_fn)(void *);
static cfgbuild_fn orig_cfgbuild;
static intptr_t hook_cfgbuild(void *scene) {   // sub_1000174F8: 画面の構築後に追加ノードを結び付ける
    intptr_t r = orig_cfgbuild(scene);
    memset(&gCfg, 0, sizeof gCfg);
    gCfg.scene = scene;
    int cnt = 0;
    cfg_walk(scene, 0, &cnt);
    if (gCfg.mbar) cfg_show(5, gMasterVol);
    if (gCfg.panel) VT(gCfg.panel, 336, void (*)(void *, int))(gCfg.panel, 0);
    os_log(L, "settings: bound mbar=%p mlbl=%p panel=%p tb=%p (visited %d)", gCfg.mbar, gCfg.mlbl, gCfg.panel, gCfg.tb, cnt);
    return r;
}

typedef intptr_t (*cfgtouch_fn)(void *, int, void *, float *);
static cfgtouch_fn orig_cfgtouch;
static intptr_t hook_cfgtouch(void *scene, int ev, void *node, float *pos) {   // sub_100019B8C
    if (node && ev != 7 && scene == gCfg.scene) {
        int tag = cfg_tag(node);
        if (tag == CFG_TAG_OSU) {                                             // osu! Store を開く(クリック 1 回につき 1 度)
            static CFAbsoluteTime last;
            CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
            if ((ev == 3 || ev == 2) && now - last > 1.0) { last = now; dispatch_async(dispatch_get_main_queue(), ^{ osu_settings_menu(); }); }
            return orig_cfgtouch(scene, ev, node, pos);                       // ボタンの押下表示は標準の処理に任せる
        }
        if (tag == CFG_TAG_OPTS) {                                            // Editor options を開く(クリック 1 回につき 1 度)
            static CFAbsoluteTime last;
            CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
            if ((ev == 3 || ev == 2) && now - last > 1.0) { last = now; dispatch_async(dispatch_get_main_queue(), ^{ bm_opts_menu(); }); }
            return orig_cfgtouch(scene, ev, node, pos);
        }
        if (tag >= CFG_TAG_HIT0 && tag <= CFG_TAG_PANEL) {
            BOOL panelOn = gCfg.panel && VT(gCfg.panel, 344, int (*)(void *))(gCfg.panel);
            if (panelOn) os_log(L, "settings touch: ev=%d tag=%d node=%p", ev, tag, node);   // 診断: 入力パネルが開いている間のタッチ
            if (tag >= CFG_TAG_HIT0 && tag <= CFG_TAG_HIT0 + 5) {            // 値の数字 → 手入力
                if (ev == 0 && !panelOn) cfg_open_input(tag - CFG_TAG_HIT0);
            } else if (tag == CFG_TAG_MBG) {                                  // Master のスライダー
                if (ev <= 1 && !panelOn && gCfg.mbar && pos) {
                    ((void (*)(void *, const float *))VA(0x100166E3CULL))(gCfg.mbar, pos);
                    int v = (int)llroundf(*(float *)((uint8_t *)gCfg.mbar + 1012));
                    if (v != gMasterVol) cfg_set(5, v);
                }
            } else if (tag == CFG_TAG_OK) {
                if (ev == 0) {
                    NSString *s = tb_text(gCfg.tb);
                    NSScanner *sc = [NSScanner scannerWithString:s];
                    int v;
                    if ([sc scanInt:&v]) cfg_set(gCfg.target, v);
                    cfg_panel(NO);
                }
            } else if (tag == CFG_TAG_CANCEL) {
                if (ev == 0) cfg_panel(NO);
            } else if (tag == CFG_TAG_PANEL) {                                // 暗幕(パネルの外)をタップ = キャンセル(閉じられなくなるのを防ぐ)
                if (ev == 0 && panelOn) cfg_panel(NO);
            }
            return 1;                                                         // 入力欄 / 暗幕も含め、下の項目へは通さない
        }
    }
    return orig_cfgtouch(scene, ev, node, pos);
}

typedef void (*setvol_fn)(void *, float);
static setvol_fn orig_setvol;
static void hook_setvol(void *chan, float v) {   // sub_1001F50B4: 全チャンネルの音量へ Master を掛ける
    orig_setvol(chan, v * (float)gMasterVol / 100.0f);
}

// ---- Custom key sound: skin/custom.wav が無ければ wav を取り込む ----
static NSString *cfg_dir(long kind) {
    typedef struct { uint8_t b[24]; } Str24;
    Str24 r = ((Str24 (*)(long))VA(0x1001DF274ULL))(kind);                    // 保存先の種類 → パス(skin/ など)
    return [NSString stringWithUTF8String:stdstr_c(r.b)] ?: @"";
}
static void cfg_audio_picked(NSURL *u) {
    NSString *dir = cfg_dir(4);
    if (!u || !dir.length) return;
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *dst = [dir stringByAppendingPathComponent:@"custom.wav"];
    [fm removeItemAtPath:dst error:nil];
    BOOL sc = [u startAccessingSecurityScopedResource];
    NSError *er = nil;
    BOOL ok = [fm copyItemAtURL:u toURL:[NSURL fileURLWithPath:dst] error:&er];
    if (sc) [u stopAccessingSecurityScopedResource];
    os_log(L, "settings: custom.wav import %{public}@ -> %{public}@ ok=%d %{public}@", u.lastPathComponent, dst, ok, er.localizedDescription ?: @"");
    if (gCfg.scene) ed_toast_text(gCfg.scene, ok ? [NSString stringWithFormat:@"custom.wav: %@", u.lastPathComponent].UTF8String : "custom.wav: import failed", ok ? 4 : 2);
}
static void cfg_pick_audio(void) {
    gPickKind = 103;
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            Class pc = NSClassFromString(@"UIDocumentPickerViewController");
            if (!pc) return;
            id p = ((id (*)(id, SEL))objc_msgSend)((id)pc, @selector(alloc));
            p = ((id (*)(id, SEL, id, NSInteger))objc_msgSend)(p, NSSelectorFromString(@"initWithDocumentTypes:inMode:"), @[@"public.audio"], 0 /* Import */);
            if (!gPickDelegate) gPickDelegate = [BMPickDelegate new];
            [p setValue:gPickDelegate forKey:@"delegate"];
            id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
            id win = [app valueForKey:@"keyWindow"];
            id vc = [win valueForKey:@"rootViewController"];
            while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
            ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), p, YES, nil);
        } @catch (NSException *ex) {
            os_log(L, "settings: picker exception %{public}@", ex.reason);
        }
    });
}
typedef void (*keydlg_fn)(void *, void *, int);
static keydlg_fn orig_keydlg;
static void hook_keydlg(void *scene, void *node, int sel) {   // sub_10001BDA8: ダイアログの選択結果(tag 7 = キー音の種類)
    orig_keydlg(scene, node, sel);
    if (sel != 4 || !node || cfg_tag(node) != 7) return;
    NSFileManager *fm = NSFileManager.defaultManager;
    for (NSNumber *k in @[@4, @13]) {
        NSString *d = cfg_dir(k.longValue);
        if (d.length && [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"custom.wav"]]) return;   // 設定済み
    }
    gCfg.scene = scene;
    ed_toast_text(scene, "Custom key sound: choose a wav file", 4);
    cfg_pick_audio();
}
