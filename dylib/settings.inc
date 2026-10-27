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

// ---- ノーツスピード(ツリー): 見出しの行(開閉)+ モードごとの行(-0.1 / -0.01 / 値(手入力)/ +0.01 / +0.1)。値の実体は notespeed.inc ----
#define CFG_TAG_NSHEAD   102   // 見出しの開閉ボタン
#define CFG_TAG_NSHEADT  103   // その文字(Show... / Hide)
#define CFG_TAG_NSBTN0   200   // 200 + 10×行 + j(0 = -0.1 / 1 = -0.01 / 2 = 値の当たり判定(手入力)/ 3 = +0.01 / 4 = +0.1)
#define CFG_TAG_NSROW0   270   // 行の入れ物(開閉で表示を切り替える)
#define CFG_TAG_NSVAL0   280   // 値の文字
#define NS_ROWS 3
static const struct { int slot; const char *name; } NS_ROW[NS_ROWS] = { { 0, "Key" }, { 1, "Taiko" }, { 4, "Slide" } };   // 設定の枠 → モード(実機のログで確認済み)
static BOOL gNsOpen;
static struct { void *row[NS_ROWS], *val[NS_ROWS], *headt, *list; } gNsUI;

// ---- Offset / Hitposition(ツリー): 見出しの開閉 + モードごとのゲージ(Master と同じ操作)。値の実体は modeoffset.inc ----
//   g = 0 Offset(判定)/ 1 Hitposition(表示)。番号 k = 10g + i(i = OF_ROW の添字)
#define CFG_TAG_OFHEAD0  104   // 見出しの開閉ボタン(104 + 4g)/ その文字(105 + 4g)
#define CFG_TAG_OFROW0   300   // 行の入れ物(300 + k)/ 当たり判定 320 + k / バー 340 + k / 値の文字 360 + k
#define CFG_TAG_OFBG0    320
#define CFG_TAG_OFBAR0   340
#define CFG_TAG_OFVAL0   360
static BOOL gOfOpen[2];
static void *gOfStockBg[2];   // 元の Hit offset / Graphic offset のスライダーの背景(tag 13 / 15)。ゲージに置き換えたので隠す
static struct { void *row[20], *bar[20], *val[20], *headt[2]; } gOfUI;
static void *gWalkStack[16];

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
    if (depth < 16) gWalkStack[depth] = n;
    int t = cfg_tag(n);
    if (t >= CFG_TAG_NSROW0 && t < CFG_TAG_NSROW0 + NS_ROWS && !gNsUI.row[t - CFG_TAG_NSROW0]) {
        gNsUI.row[t - CFG_TAG_NSROW0] = n;
        if (!gNsUI.list) for (int d = (depth < 16 ? depth : 16) - 1; d >= 0; d--) if (cast_to(gWalkStack[d], 0x1008EADF0ULL)) { gNsUI.list = gWalkStack[d]; break; }   // 祖先の ListSprite
    }
    else if (t >= CFG_TAG_NSVAL0 && t < CFG_TAG_NSVAL0 + NS_ROWS && !gNsUI.val[t - CFG_TAG_NSVAL0]) gNsUI.val[t - CFG_TAG_NSVAL0] = n;
    else if (t == CFG_TAG_NSHEADT && !gNsUI.headt) gNsUI.headt = n;
    else if ((t == 13 || t == 15) && !gOfStockBg[t == 15]) gOfStockBg[t == 15] = n;
    else if (t >= CFG_TAG_OFROW0 && t < CFG_TAG_OFROW0 + 20 && t % 10 < OF_N) {            // Offset / Hitposition の行(一覧の祖先も控える)
        if (!gOfUI.row[t - CFG_TAG_OFROW0]) gOfUI.row[t - CFG_TAG_OFROW0] = n;
        if (!gNsUI.list) for (int d = (depth < 16 ? depth : 16) - 1; d >= 0; d--) if (cast_to(gWalkStack[d], 0x1008EADF0ULL)) { gNsUI.list = gWalkStack[d]; break; }
    }
    else if (t >= CFG_TAG_OFBAR0 && t < CFG_TAG_OFBAR0 + 20 && t % 10 < OF_N) { if (!gOfUI.bar[t - CFG_TAG_OFBAR0]) gOfUI.bar[t - CFG_TAG_OFBAR0] = n; }
    else if (t >= CFG_TAG_OFVAL0 && t < CFG_TAG_OFVAL0 + 20 && t % 10 < OF_N) { if (!gOfUI.val[t - CFG_TAG_OFVAL0]) gOfUI.val[t - CFG_TAG_OFVAL0] = n; }
    else if ((t == CFG_TAG_OFHEAD0 + 1 || t == CFG_TAG_OFHEAD0 + 5) && !gOfUI.headt[(t - CFG_TAG_OFHEAD0) / 4]) gOfUI.headt[(t - CFG_TAG_OFHEAD0) / 4] = n;
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

static void of_ui_show(int g, int i) {   // 1 行のバーと値の文字(標準のスライダーと同じ: バー = (値 + 200) × 0.25 %)
    int k = 10 * g + i, v = of_get(g, i);
    float pct = cfg_pct(2, v < -200 ? -200 : (v > 200 ? 200 : v));
    if (gOfUI.bar[k]) ((void (*)(void *, float))VA(0x100166D10ULL))(gOfUI.bar[k], pct);
    if (gOfUI.val[k]) { char b[16]; snprintf(b, sizeof b, "%d", v); set_label_text(gOfUI.val[k], b); }
}
static void of_ui_update(void) {   // 行の表示 / 見出しの文字 / 各ゲージの値(並べ直しは呼び出し側)
    for (int g = 0; g < 2; g++) {
        for (int i = 0; i < OF_N; i++) {
            void *row = gOfUI.row[10 * g + i];
            if (row) VT(row, 336, void (*)(void *, int))(row, gOfOpen[g] ? 1 : 0);
            of_ui_show(g, i);
        }
        if (gOfUI.headt[g]) set_label_text(gOfUI.headt[g], gOfOpen[g] ? "Hide" : "Show...");
    }
}
static void ns_ui_refresh(BOOL relayout, BOOL keepScroll) {   // 値の文字 / 行の表示 / 見出しの文字を更新し、必要なら一覧を並べ直す(Offset / Hitposition も含む)
    of_ui_update();
    for (int i = 0; i < NS_ROWS; i++) {
        if (gNsUI.row[i]) VT(gNsUI.row[i], 336, void (*)(void *, int))(gNsUI.row[i], gNsOpen ? 1 : 0);
        if (gNsUI.val[i]) { char b[16]; snprintf(b, sizeof b, "%.2f", ns_fine(NS_ROW[i].slot)); set_label_text(gNsUI.val[i], b); }
    }
    if (gNsUI.headt) set_label_text(gNsUI.headt, gNsOpen ? "Hide" : "Show...");
    if (relayout && gNsUI.list) {                                              // 表示中の行だけを積み直す
        uint8_t *ls = gNsUI.list;
        void *box = *(void **)(ls + 1560);                                     // 中身の入れ物。その y = 画面の動き(viewH - 中身の高さ = 先頭 / 0 = 末尾)
        float viewH = *(float *)(ls + 128), y0 = box ? VT(box, 208, float (*)(void *))(box) : 0;
        double c0 = *(double *)(ls + 1568);
        ((void (*)(void *, char))VA(0x100048FF8ULL))(ls, keepScroll ? 1 : 0);   // 標準の並べ直しは、スクロールを末尾へ戻してしまう
        if (keepScroll && box) {                                               // 先頭からの距離を保つ: 並べ直しの後の y を計算して、標準のスクロール更新(sub_100048594)に渡す
            float y1 = (float)((double)y0 + c0 - viewH) + viewH - (float)*(double *)(ls + 1568);
            float lo = viewH - (float)*(double *)(ls + 1568);
            if (y1 < lo) y1 = lo;
            if (y1 > 0) y1 = 0;
            *(float *)(ls + 1460) = y1;
            ((void (*)(void *))VA(0x100048594ULL))(ls);
        }
    }
}
static void ns_ui_set(int i, float v) {
    ns_store(NS_ROW[i].slot, v);
    if (gCfg.scene) *((uint8_t *)gCfg.scene + 680) = 1;                       // 変更あり(標準のトグルと同じ印)
    ns_ui_refresh(NO, YES);
}

static void cfg_panel(BOOL show) {
    if (!gCfg.panel) return;
    if (!show && gCfg.tb) ((void (*)(void *))VA(0x10006D2E0ULL))(gCfg.tb);   // 入力中なら確定して編集状態を終える
    VT(gCfg.panel, 336, void (*)(void *, int))(gCfg.panel, show ? 1 : 0);
}
static void cfg_open_input(int t) {
    if (!gCfg.panel || !gCfg.tb) return;
    gCfg.target = t;
    if (t >= 100) {                                                            // ノーツスピード(100 + 行): 小数で入力
        char ttl[48]; snprintf(ttl, sizeof ttl, "%s note speed (-20 to 20)", NS_ROW[t - 100].name);
        if (gCfg.title) set_label_text(gCfg.title, ttl);
        ((void (*)(void *))VA(0x10006D2E0ULL))(gCfg.tb);
        tb_set(gCfg.tb, [NSString stringWithFormat:@"%.2f", ns_fine(NS_ROW[t - 100].slot)]);
        cfg_panel(YES);
        ((void (*)(void *))VA(0x10006DA24ULL))(gCfg.tb);
        return;
    }
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
    memset(&gNsUI, 0, sizeof gNsUI); gNsOpen = NO;
    memset(&gOfUI, 0, sizeof gOfUI); gOfOpen[0] = gOfOpen[1] = NO; memset(gOfStockBg, 0, sizeof gOfStockBg);
    gCfg.scene = scene;
    int cnt = 0;
    cfg_walk(scene, 0, &cnt);
    for (int t = 2; t <= 3; t++) {                                             // 元の Hit offset / Graphic offset のスライダー(背景 / バー / 値の文字)を隠す(スキンの show=0 は効かない)
        void **b = cfg_vec(scene, 880), **l = cfg_vec(scene, 904);
        if (gOfStockBg[t - 2]) VT(gOfStockBg[t - 2], 336, void (*)(void *, int))(gOfStockBg[t - 2], 0);
        if (b && b[t]) VT(b[t], 336, void (*)(void *, int))(b[t], 0);
        if (l && l[t]) VT(l[t], 336, void (*)(void *, int))(l[t], 0);
    }
    ns_ui_refresh(YES, NO);                                                    // 既定は閉じた状態(行を隠して並べ直す)
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
        if (tag == 13 || tag == 15) return 1;                                 // 元の Hit offset / Graphic offset のスライダー(隠してある)は、ゲージに置き換えたので捨てる
        if (tag == CFG_TAG_OFHEAD0 || tag == CFG_TAG_OFHEAD0 + 4) {           // Offset / Hitposition の開閉(クリック 1 回につき 1 度)
            static CFAbsoluteTime last;
            CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
            if ((ev == 3 || ev == 2) && now - last > 0.3) {
                last = now; int g = (tag - CFG_TAG_OFHEAD0) / 4; gOfOpen[g] = !gOfOpen[g]; ns_ui_refresh(YES, YES);
            }
            return orig_cfgtouch(scene, ev, node, pos);                       // ボタンの押下表示は標準の処理に任せる
        }
        if (tag >= CFG_TAG_OFBG0 && tag < CFG_TAG_OFBG0 + 20 && tag % 10 < OF_N) {   // モードごとのゲージ(スワイプ)
            int k = tag - CFG_TAG_OFBG0, g = k / 10, i = k % 10;
            BOOL panelOn = gCfg.panel && VT(gCfg.panel, 344, int (*)(void *))(gCfg.panel);
            if (ev <= 1 && gOfOpen[g] && !panelOn && gOfUI.bar[k] && pos) {
                ((void (*)(void *, const float *))VA(0x100166E3CULL))(gOfUI.bar[k], pos);
                int v = (int)llroundf(*(float *)((uint8_t *)gOfUI.bar[k] + 1012) * 4.0f - 200.0f);   // バーの % → ms(標準の逆: (v + 200) × 0.25)
                v = v < -200 ? -200 : (v > 200 ? 200 : v);
                if (v != of_get(g, i)) { of_set(g, i, v); of_ui_show(g, i); }
            }
            return 1;
        }
        if (tag == CFG_TAG_NSHEAD) {                                          // Note speed の開閉(クリック 1 回につき 1 度)
            static CFAbsoluteTime last;
            CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
            if ((ev == 3 || ev == 2) && now - last > 0.3) {
                last = now; gNsOpen = !gNsOpen; ns_ui_refresh(YES, YES);
            }
            return orig_cfgtouch(scene, ev, node, pos);                       // ボタンの押下表示は標準の処理に任せる
        }
        if (tag >= CFG_TAG_NSBTN0 && tag < CFG_TAG_NSBTN0 + 10 * NS_ROWS) {   // Note speed の行(閉じている間は何もしない)
            int i = (tag - CFG_TAG_NSBTN0) / 10, j = (tag - CFG_TAG_NSBTN0) % 10;
            BOOL panelOn = gCfg.panel && VT(gCfg.panel, 344, int (*)(void *))(gCfg.panel);
            if (!gNsOpen || panelOn) return 1;
            if (j == 2) { if (ev == 0) cfg_open_input(100 + i); return 1; }   // 値 → 手入力
            static CFAbsoluteTime last;
            CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
            if ((ev == 3 || ev == 2) && now - last > 0.12) {
                last = now;
                static const float STEP[5] = { -0.1f, -0.01f, 0, 0.01f, 0.1f };
                ns_ui_set(i, ns_fine(NS_ROW[i].slot) + STEP[j]);
            }
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
                    if (gCfg.target >= 100) {                                 // ノーツスピード: 小数
                        float fv;
                        if ([sc scanFloat:&fv]) ns_ui_set(gCfg.target - 100, fv);
                    } else {
                        int v;
                        if ([sc scanInt:&v]) cfg_set(gCfg.target, v);
                    }
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
