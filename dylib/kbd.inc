#ifndef BM_TAG_ORDER
#define BM_TAG_ORDER 768
#endif
// ---- キーボードのショートカット(Mac / iPad の外付けキーボード)----
// キー入力は GameController(GCKeyboard)で受ける(プラグインの OnKey と同じ口。plugins.inc の pl_kb_register)。キー → 動作 を保存し、押したら編集中のエディタで実行する。
// 動作 = (1) BM のメニュー項目(Lasso / 選択 / Undo など)= 偽のボタンノードで hook_touch へ通す、(2) 標準のボタン(ズーム / ツール)= エディタの本物のボタンを探して押す、(3) プラグイン
static intptr_t hook_touch(void *self, int ev, void *node, void *pos);
typedef struct { const char *id, *title; int tag; int kind; int cat; } KbAct;    // kind 0 = BM タグ / 1 = 標準のボタン(Slide のみ)/ cat = 分類
enum { KC_EDIT, KC_VIEW, KC_OTHER };
static const KbAct kKbActs[] = {
    { "lasso", "Lasso mode", BM_TAG_LASSO, 0, KC_EDIT }, { "order", "Order select", BM_TAG_ORDER, 0, KC_EDIT },
    { "sel_all", "Select all", BM_TAG_SEL_ALL, 0, KC_EDIT }, { "sel_inv", "Invert selection", BM_TAG_SEL_INV, 0, KC_EDIT }, { "sel_clr", "Deselect", BM_TAG_SEL_CLR, 0, KC_EDIT },
    { "copy", "Copy selection", BM_TAG_COPY, 0, KC_EDIT }, { "paste", "Paste", BM_TAG_PASTE, 0, KC_EDIT }, { "paste_mir", "Paste mirrored", BM_TAG_PASTE_MIR, 0, KC_EDIT },
    { "undo", "Undo", BM_TAG_UNDO, 0, KC_EDIT }, { "redo", "Redo", BM_TAG_REDO, 0, KC_EDIT },
    { "mv_later", "Move later", BM_TAG_MV_LATER, 0, KC_EDIT }, { "mv_earl", "Move earlier", BM_TAG_MV_EARL, 0, KC_EDIT },
    { "mv_left", "Move left", BM_TAG_MV_LEFT, 0, KC_EDIT }, { "mv_right", "Move right", BM_TAG_MV_RIGHT, 0, KC_EDIT },
    { "quant", "Quantize", BM_TAG_QUANT, 0, KC_EDIT },
    { "tool_note", "Tool: Note (Slide)", 1, 1, KC_VIEW }, { "tool_wipe", "Tool: Wipe (Slide)", 2, 1, KC_VIEW }, { "tool_slide", "Tool: Slide (Slide)", 81, 1, KC_VIEW },
    { "tool_remove", "Tool: Remove (Slide)", 83, 1, KC_VIEW }, { "tool_modify", "Tool: Slide modify (Slide)", 84, 1, KC_VIEW },
    { "zoom_in", "Zoom in", 6, 1, KC_VIEW }, { "zoom_out", "Zoom out", 7, 1, KC_VIEW },
    { "div_up", "Grid division +1 (finer)", BM_TAG_DIV_UP, 0, KC_VIEW }, { "div_dn", "Grid division -1 (coarser)", BM_TAG_DIV_DN, 0, KC_VIEW },
    { "spec", "Spectrogram on/off", BM_TAG_SPEC_TOGGLE, 0, KC_VIEW }, { "spd_up", "Playback speed +5%", BM_TAG_SPD_UP, 0, KC_VIEW }, { "spd_dn", "Playback speed -5%", BM_TAG_SPD_DN, 0, KC_VIEW },
    { "plugins", "Plugins panel", BM_TAG_PLUGINS, 0, KC_OTHER }, { "pdev", "Plugin Dev", BM_TAG_PDEV, 0, KC_OTHER }, { "opts", "Editor options", BM_TAG_OPTS, 0, KC_OTHER },
};
#define KB_NACT ((int)(sizeof kKbActs / sizeof kKbActs[0]))
static void *gKbEd; static CFAbsoluteTime gKbEdT;        // 画面にあるエディタ(毎フレーム更新。0.5 秒以内に更新があれば有効)
static NSMutableDictionary<NSString *, NSString *> *gKbBinds;   // "mods:code" → 動作 ID("lasso" / "plugin:ファイル名")
static int gKbMods;                                              // 1 = shift / 2 = ctrl / 4 = alt / 8 = cmd
static void (^gKbCapture)(int mods, long code);
static id gKbAlert;

static void kb_load(void) {
    if (gKbBinds) return;
    NSDictionary *d = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"bm.kb.binds"];
    gKbBinds = d ? [d mutableCopy] : [NSMutableDictionary dictionary];
}
static void kb_save(void) { [NSUserDefaults.standardUserDefaults setObject:gKbBinds forKey:@"bm.kb.binds"]; [NSUserDefaults.standardUserDefaults synchronize]; }
static NSString *kb_key_name(long c) {
    static const char *fk[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    if (c >= 4 && c <= 29) return [NSString stringWithFormat:@"%c", 'A' + (int)(c - 4)];
    if (c >= 30 && c <= 38) return [NSString stringWithFormat:@"%d", (int)(c - 29)];
    if (c == 39) return @"0";
    if (c >= 58 && c <= 69) return @(fk[c - 58]);
    switch (c) { case 40: return @"Enter"; case 41: return @"Esc"; case 42: return @"Backspace"; case 43: return @"Tab"; case 44: return @"Space";
                 case 45: return @"-"; case 46: return @"="; case 47: return @"["; case 48: return @"]"; case 49: return @"\\"; case 51: return @";"; case 52: return @"'"; case 53: return @"`";
                 case 54: return @","; case 55: return @"."; case 56: return @"/"; case 74: return @"Home"; case 75: return @"PageUp"; case 76: return @"Delete"; case 77: return @"End"; case 78: return @"PageDown";
                 case 79: return @"Right"; case 80: return @"Left"; case 81: return @"Down"; case 82: return @"Up"; }
    return [NSString stringWithFormat:@"key%ld", c];
}
static NSString *kb_combo_name(int mods, long code) {
    return [NSString stringWithFormat:@"%@%@%@%@%@", (mods & 8) ? @"Cmd+" : @"", (mods & 2) ? @"Ctrl+" : @"", (mods & 4) ? @"Alt+" : @"", (mods & 1) ? @"Shift+" : @"", kb_key_name(code)];
}
static NSString *kb_combo_key(int mods, long code) { return [NSString stringWithFormat:@"%d:%ld", mods, code]; }
static NSString *kb_combo_title(NSString *key) {
    NSArray *p = [key componentsSeparatedByString:@":"];
    return p.count == 2 ? kb_combo_name([p[0] intValue], [p[1] integerValue]) : key;
}
static const KbAct *kb_act(NSString *aid) { for (int i = 0; i < KB_NACT; i++) if (!strcmp(kKbActs[i].id, aid.UTF8String)) return &kKbActs[i]; return NULL; }
static NSString *kb_act_title(NSString *aid) {
    const KbAct *a = kb_act(aid);
    if (a) return @(a->title);
    if ([aid hasPrefix:@"plugin:"]) return [@"Plugin: " stringByAppendingString:[aid substringFromIndex:7]];
    return aid;
}

// 偽のボタンノード(タグだけ答える)。BM のメニュー項目は hook_touch の中でタグだけ見て動くので、これで通せる
static int gKbFakeTag;
static int kb_fake_tag(void *n) { (void)n; return gKbFakeTag; }
static long kb_fake_zero(void *n) { (void)n; return 0; }
static void *kb_fake_node(int tag) {
    static void *node;
    if (!node) {
        void **vt = calloc(640, sizeof(void *));
        vt[0] = NULL; vt[1] = (void *)VA(0x10095EF00ULL);            // offset-to-top / typeinfo(cocos2d::Node。dynamic_cast しても落ちない)
        for (int i = 2; i < 640; i++) vt[i] = (void *)kb_fake_zero;
        vt[2 + 0x2a0 / 8] = (void *)kb_fake_tag;
        node = calloc(1, 0x800);
        *(void ***)node = vt + 2;
    }
    gKbFakeTag = tag;
    return node;
}
static void *kb_find_tag(void *ed, void *n, int tag, int depth) {   // ノートのコンテナ(+1472)には入らない
    if (!n || depth > 8) return NULL;
    nvec *ch = node_children(n);
    if (!ch || !ch->begin || ch->end < ch->begin || (ch->end - ch->begin) > 300) return NULL;
    for (void **p = ch->begin; p != ch->end; p++) {
        if (*p == *(void **)((uint8_t *)ed + 1472)) continue;
        if (((int (*)(void *))(*(void ***)*p)[0x2a0 / 8])(*p) == tag) return *p;
    }
    for (void **p = ch->begin; p != ch->end; p++) {
        if (*p == *(void **)((uint8_t *)ed + 1472)) continue;
        void *r = kb_find_tag(ed, *p, tag, depth + 1);
        if (r) return r;
    }
    return NULL;
}
static void kb_run(NSString *aid) {
    void *ed = gKbEd;
    if (!ed || CFAbsoluteTimeGetCurrent() - gKbEdT > 0.5 || gPreview) return;
    if (!(lay_of(ed) || is_taiko(ed))) return;
    gEditor = ed;
    float pos[2] = { 0, 0 };
    if ([aid hasPrefix:@"plugin:"]) {
        if (!gPlFiles) pl_scan();
        NSString *fn = [aid substringFromIndex:7];
        for (NSUInteger i = 0; i < gPlFiles.count; i++) if ([gPlFiles[i].lastPathComponent isEqualToString:fn]) { pl_run(ed, i); return; }
        ed_toast_text(ed, "Shortcut: plugin not found", 2);
        return;
    }
    const KbAct *a = kb_act(aid);
    if (!a) return;
    if (a->kind == 0) { hook_touch(ed, 3, kb_fake_node(a->tag), pos); return; }
    void *btn = kb_find_tag(ed, ed, a->tag, 0);
    if (!btn) { ed_toast_text(ed, "Shortcut: button not found", 2); return; }
    if (lay_of(ed) && lay_of(ed)->slide) { int64_t p = 0; ((intptr_t (*)(void *, int, void *, void *))VA(0x1000C5520ULL))(ed, 3, btn, &p); }
    else hook_touch(ed, 3, btn, pos);
    os_log(L, "shortcut: %{public}@ -> tag %d", aid, a->tag);
}
static BOOL kb_alert_up(void) {
    id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
    id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
    return vc && [vc valueForKey:@"presentedViewController"] != nil;
}
static BOOL kb_key_event(long code, BOOL pressed) {                // 戻り値 YES = ショートカットとして処理(プラグインの OnKey には渡さない)
    static const struct { int c, m; } mk[] = { { 225, 1 }, { 229, 1 }, { 224, 2 }, { 228, 2 }, { 226, 4 }, { 230, 4 }, { 227, 8 }, { 231, 8 } };
    for (int i = 0; i < 8; i++) if (code == mk[i].c) { if (pressed) gKbMods |= mk[i].m; else gKbMods &= ~mk[i].m; return NO; }
    if (!pressed) return NO;
    if (!gKbCapture && gPlAlertVC && gPlAlertKeys.count && [gPlAlertVC valueForKey:@"presentingViewController"]) {   // アラートが出ている間: 数字キー(1〜9, 0)= その番号の項目 / Esc = 最後の項目(Close / Back)
        long idx = (code >= 30 && code <= 38) ? code - 30 : (code == 39 ? 9 : (code == 41 ? (long)gPlAlertKeys.count - 1 : -1));
        if (idx >= 0 && idx < (long)gPlAlertKeys.count) {
            void (^run)(void) = gPlAlertKeys[(NSUInteger)idx];
            id ac = gPlAlertVC;
            ((void (*)(id, SEL, BOOL, id))objc_msgSend)(ac, NSSelectorFromString(@"dismissViewControllerAnimated:completion:"), YES, nil);
            run();
            return YES;
        }
        return NO;
    }
    if (gKbCapture) {
        void (^cb)(int, long) = gKbCapture; gKbCapture = nil;
        int m = gKbMods;
        if (gKbAlert) { ((void (*)(id, SEL, BOOL, id))objc_msgSend)(gKbAlert, NSSelectorFromString(@"dismissViewControllerAnimated:completion:"), YES, nil); gKbAlert = nil; }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ cb(m, code); });
        return YES;
    }
    kb_load();
    NSString *aid = gKbBinds[kb_combo_key(gKbMods, code)];
    if (!aid || kb_alert_up() || !gKbEd || CFAbsoluteTimeGetCurrent() - gKbEdT > 0.5) return NO;
    kb_run(aid);
    return YES;
}
static void kb_tick(void *ed) {                                    // エディタの毎フレーム: 有効なエディタの記録とキーボードの登録(未接続なら再試行)
    gKbEd = ed; gKbEdT = CFAbsoluteTimeGetCurrent();
    static int f;
    if ((f++ % 120) == 0) pl_kb_register();
}

// ---- 設定 UI(Editor options > Keyboard shortcuts) ----
static void kb_menu(void);
static void kb_capture_ui(NSString *aid) {
    NSString *t = kb_act_title(aid);
    gKbCapture = ^(int mods, long code) {
        kb_load();
        NSString *key = kb_combo_key(mods, code);
        NSString *old = gKbBinds[key];
        gKbBinds[key] = aid; kb_save();
        pl_alert(@"Shortcut saved", [NSString stringWithFormat:@"%@  ->  %@%@", kb_combo_name(mods, code), t, old ? [NSString stringWithFormat:@"\n(replaced: %@)", kb_act_title(old)] : @""], nil, @[ @"OK" ], @[ @0 ], ^(NSInteger i, NSString *x) { kb_menu(); });
    };
    @try {
        Class cAC = NSClassFromString(@"UIAlertController"), cAA = NSClassFromString(@"UIAlertAction");
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        id ac = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)((id)cAC, NSSelectorFromString(@"alertControllerWithTitle:message:preferredStyle:"), [@"Press a key for: " stringByAppendingString:t], @"Press the key (with Shift / Ctrl / Alt / Cmd if needed) on the connected keyboard.", 1);
        void (^h)(id) = ^(id a) { gKbCapture = nil; gKbAlert = nil; dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ kb_menu(); }); };
        id act = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)((id)cAA, NSSelectorFromString(@"actionWithTitle:style:handler:"), @"Cancel", 1, h);
        ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addAction:"), act);
        gKbAlert = ac;
        pl_present(ac, 0);
    } @catch (NSException *ex) { gKbCapture = nil; os_log(L, "kb: capture alert exception %{public}@", ex.reason); }
}
// 項目が多い一覧は 1 ページ 7 件(数字キー 1〜7)。8 / 9 = 前 / 次のページ、最後 = 戻る(Esc でも戻る)
static void kb_list(NSString *title, NSString *msg, NSArray<NSString *> *items, NSInteger style, NSUInteger page, void (^pick)(NSInteger i)) {
    NSUInteger per = 7, from = page * per, to = MIN(items.count, from + per);
    NSMutableArray *titles = [NSMutableArray array], *styles = [NSMutableArray array];
    for (NSUInteger i = from; i < to; i++) { [titles addObject:items[i]]; [styles addObject:@(style)]; }
    BOOL prev = page > 0, more = to < items.count;
    if (prev) { [titles addObject:@"< Previous page"]; [styles addObject:@0]; }
    if (more) { [titles addObject:@"Next page >"]; [styles addObject:@0]; }
    [titles addObject:@"< Back"]; [styles addObject:@1];
    pl_alert(title, [NSString stringWithFormat:@"%@%@", msg, items.count > per ? [NSString stringWithFormat:@"  (%lu/%lu)", (unsigned long)page + 1, (unsigned long)((items.count + per - 1) / per)] : @""], nil, titles, styles, ^(NSInteger i, NSString *x) {
        NSInteger n = (NSInteger)(to - from);
        if (i >= 0 && i < n) { pick((NSInteger)from + i); return; }
        NSInteger k = n;
        if (prev) { if (i == k) { kb_list(title, msg, items, style, page - 1, pick); return; } k++; }
        if (more) { if (i == k) { kb_list(title, msg, items, style, page + 1, pick); return; } }
        pick(-1);
    });
}
static void kb_pick_action(NSArray<NSString *> *ids) {
    NSMutableArray *titles = [NSMutableArray array];
    for (NSString *i in ids) [titles addObject:kb_act_title(i)];
    kb_list(@"Action", @"Choose what the key does.", titles, 0, 0, ^(NSInteger i) {
        if (i < 0 || i >= (NSInteger)ids.count) { kb_menu(); return; }
        kb_capture_ui(ids[(NSUInteger)i]);
    });
}
static void kb_add_menu(void) {
    pl_alert(@"Add shortcut", @"Choose a category.", nil, @[ @"Select / Edit", @"Tools / View", @"Plugins", @"Other", @"< Back" ], @[ @0, @0, @0, @0, @1 ], ^(NSInteger i, NSString *x) {
        NSMutableArray<NSString *> *ids = [NSMutableArray array];
        if (i >= 0 && i <= 1) { for (int k = 0; k < KB_NACT; k++) if (kKbActs[k].cat == (int)i) [ids addObject:@(kKbActs[k].id)]; }
        else if (i == 3) { for (int k = 0; k < KB_NACT; k++) if (kKbActs[k].cat == KC_OTHER) [ids addObject:@(kKbActs[k].id)]; }
        else if (i == 2) { if (!gPlFiles) pl_scan(); for (NSString *f in gPlFiles) [ids addObject:[@"plugin:" stringByAppendingString:f.lastPathComponent]]; }
        else { kb_menu(); return; }
        if (!ids.count) { kb_add_menu(); return; }
        kb_pick_action(ids);
    });
}
static void kb_remove_menu(void) {
    kb_load();
    NSArray<NSString *> *keys = [gKbBinds.allKeys sortedArrayUsingSelector:@selector(compare:)];
    NSMutableArray *titles = [NSMutableArray array];
    for (NSString *k in keys) [titles addObject:[NSString stringWithFormat:@"%@: %@", kb_combo_title(k), kb_act_title(gKbBinds[k])]];
    kb_list(@"Remove shortcut", @"Select a shortcut to delete it.", titles, 2, 0, ^(NSInteger i) {
        if (i >= 0 && i < (NSInteger)keys.count) { [gKbBinds removeObjectForKey:keys[(NSUInteger)i]]; kb_save(); kb_remove_menu(); return; }
        kb_menu();
    });
}
static void kb_menu(void) {
    kb_load();
    NSMutableString *msg = [NSMutableString string];
    for (NSString *k in [gKbBinds.allKeys sortedArrayUsingSelector:@selector(compare:)]) [msg appendFormat:@"%@  ->  %@\n", kb_combo_title(k), kb_act_title(gKbBinds[k])];
    if (!msg.length) [msg appendString:@"(no shortcuts yet)"];
    [msg appendString:@"\n\nWorks while editing, with a connected keyboard.\nIn menus, number keys pick the item (Esc = last item)."];
    pl_alert(@"Keyboard shortcuts", msg, nil, @[ @"Add shortcut...", @"Remove shortcut...", @"Close" ], @[ @0, @0, @1 ], ^(NSInteger i, NSString *x) {
        if (i == 0) kb_add_menu(); else if (i == 1) kb_remove_menu();
    });
}
