// ---- Taiko エディタ(EditorTaiko, vtable 0x10091E870): 小節線ツール ----
// EditorTaiko は横スクロール(コンテナ x = -beat*K, K = this[2580]*this[2584](float))。編集領域のタッチは sub_100140664(this, ev, node, Vec2*)(etag 105)。
//   ev 0=押下 / 1=移動 / 2=離し。押下位置のコンテナ座標 x → 拍 = round(x*div/K)/div(div = *(u8*)(this+2576))。ノートの追加は sub_100140F48(this, 種類, -2, 拍)。
// 小節線ツール(右ツールメニューの _right-icon3-7, tag 88): ON の間、編集領域の押下でその拍に sign 効果点(直前の拍子を引き継ぐ)を作り / 同じ拍をもう一度押すと消す。
//   Taiko の小節線は sign の変化点から引き直される(Android 版 sub_260218 の解析)。標準のノート追加は行わない。
#define BM_TAG_TBAR 88
static BOOL gBarMode; static void *gBarEd; static BOOL gBarGesture;

static BOOL taiko_snap_beat_div(void *a1, const float *pos, int div, double *beat) {
    uint8_t *s = (uint8_t *)a1;
    void *cont = *(void **)(s + 1792);
    if (!cont) return NO;
    // sub_100546980(node, Vec2*) → Vec2(x8 経由で返る)= convertToNodeSpace。24B 構造体の返却で x8 を渡す
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const float *))VA(0x100546980ULL))(cont, pos);
    float out[2] = { r.v[0], r.v[1] };
    float K = *(float *)(s + 2580) * *(float *)(s + 2584);
    if (K < 1e-3f || div < 1) return NO;
    *beat = round((double)out[0] * div / K) / div;
    return YES;
}
static BOOL taiko_snap_beat(void *a1, const float *pos, double *beat) { return taiko_snap_beat_div(a1, pos, *(uint8_t *)((uint8_t *)a1 + 2576), beat); }
static void taiko_bar_toggle(void *a1, double beat) {
    gEditor = a1;
    gOuter = *(void **)((uint8_t *)a1 + 0x2d8);
    if (!gOuter || !work_load()) { ed_toast_text(a1, "Bar line: effects unavailable", 2); return; }
    double sign = 4.0;
    long idx = -1;
    for (size_t i = 0; i < eff_count(); i++) {
        uint8_t *e = eff_at(i);
        double b = beat_key(e);
        if (fabs(b - beat) < 1e-6) idx = (long)i;
        else if (b < beat && !e[64]) sign = *(double *)(e + 72);        // 直前の拍子を引き継ぐ
    }
    BOOL removed = NO;
    if (idx >= 0 && !eff_at(idx)[64]) {            // 既に sign がある = 小節線を消す
        uint8_t *e = eff_at(idx);
        e[64] = 1;
        if (e[32] && e[48] && e[80] && e[88] && e[104]) eff_remove((size_t)idx);   // 他の項目が無ければ効果点ごと削除
        removed = YES;
    } else if (idx >= 0) {
        uint8_t *e = eff_at(idx); e[64] = 0; *(double *)(e + 72) = sign;
    } else {
        uint8_t e[EFF_SZ]; eff_init(e);
        double fl = floor(beat + 1e-9);
        int m = (int)fl, den = *(uint8_t *)((uint8_t *)a1 + 2576), num = (int)llround((beat - fl) * den);
        if (num >= den) { m++; num = 0; }
        beat_set(e, (BeatV){m, num, den > 0 ? den : 1});
        e[64] = 0; *(double *)(e + 72) = sign;
        eff_insert(e);
    }
    work_commit();
    ((void (*)(void *, int))VA(0x10013FAE8ULL))(a1, 1);     // 小節線スプライトを引き直す(this, 再計算フラグ)
    char msg[64]; snprintf(msg, sizeof msg, removed ? "Bar line removed (beat %.3g)" : "Bar line added (beat %.3g, sign %g)", beat, sign);
    ed_toast_text(a1, msg, 4);
    os_log(L, "taiko bar: %s beat=%.4f sign=%g effects=%zu", removed ? "remove" : "add", beat, sign, eff_count());
}

// ---- Taiko の選択モード / 削除(Key・Slide のゴミ箱モードに相当)----
// 標準の Taiko はノートをタップすると削除し、複数選択はコピー機能だけ。選択の集合は this+1992 の std::set<Node*>(+2000 = 終端、+2008 = 件数)。
//   選択: 集合へ追加(sub_100144AB8)+ HitSpriteTaiko の強調(sub_100175370)/ 解除: 除去(sub_100144D9C)+ 強調を消す / 全解除 sub_100140A18 / 全選択 = vtable +1584
//   削除: sub_100141108(this, HitObject, flag)= 標準の削除(集合からも外す)。タップしたノートの検索は標準と同じ sub_1000FF264(コンテナ, x, 許容)。
#define BM_TAG_TSEL 89      // 選択モード(ON の間、タップ = 選択の追加 / 解除。何も無い所 = 全解除)
#define BM_TAG_TDEL 90      // 選択したノートを削除(ゴミ箱)
#define BM_TAG_TALL 91      // 全選択
#define BM_TAG_TCLR 92      // 選択の解除
static BOOL gSelMode; static BOOL gSelGesture;
static BOOL gHsLbl;   // ノートの上に hs の値を出す(More メニューで切替。FX プレビュー中のみ)
static BOOL bm_safe_read(const void *src, void *dst, size_t n);
static void mod_update(void *ed);
static void *gModEd, *gTipEd;
static void mk_set_color(void *lb, uint8_t r, uint8_t g, uint8_t b);

static NSArray<NSValue *> *t_sel_nodes(void *a1) {   // 選択中のスプライト(標準の集合を中順走査)
    NSMutableArray *a = [NSMutableArray array];
    uint64_t *set = (uint64_t *)((uint8_t *)a1 + 1992);
    if (!*(uint64_t *)((uint8_t *)a1 + 2008)) return a;
    uint64_t *node = (uint64_t *)set[0], *end = (uint64_t *)((uint8_t *)a1 + 2000);
    for (int guard = 0; node != end && node && guard < 200000; guard++) {
        [a addObject:[NSValue valueWithPointer:(void *)node[4]]];
        uint64_t *nx = (uint64_t *)node[1];
        if (nx) { while (*nx) nx = (uint64_t *)*nx; node = nx; }
        else { uint64_t *c = node; uint64_t *par = (uint64_t *)c[2]; while (par && (uint64_t *)par[0] != c) { c = par; par = (uint64_t *)c[2]; } node = par; }
    }
    return a;
}
static BOOL t_sel_has(void *a1, void *sp) {
    for (NSValue *v in t_sel_nodes(a1)) if (v.pointerValue == sp) return YES;
    return NO;
}
static void t_sel_highlight(void *a1, void *sp, BOOL on) {
    void *hs = g_dynamic_cast(sp, 0x10095EF00ULL, 0x100923570ULL);       // HitSpriteTaiko
    if (!hs) return;
    typedef void (*hl_fn)(void *, int64_t);
    if (!on) { ((hl_fn)VA(0x100175370ULL))(hs, 0); return; }
    void *ref = VT(sp, 728, void *(*)(void *))(sp);
    uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;   // EditorTaiko::NoteData
    if (!nd || !nd[2]) return;
    int type = *(int *)((uint8_t *)nd[2] + 148) - 1;
    uint64_t idx = ((unsigned)type <= 6) ? ((uint64_t *)VA(0x100803590ULL))[type] : 0;
    int64_t col = ((int64_t (*)(void *))VA(0x1001F97E8ULL))(*(void **)((uint8_t *)a1 + 8 * idx + 1976));
    ((hl_fn)VA(0x100175370ULL))(hs, col);
}
static void t_sel_set(void *a1, void *sp, BOOL on) {
    void *set = (uint8_t *)a1 + 1992;
    void *key = sp;
    if (on) ((void (*)(void *, void *, void *))VA(0x100144AB8ULL))(set, &key, &key);
    else ((void (*)(void *, void *))VA(0x100144D9CULL))(set, &key);
    t_sel_highlight(a1, sp, on);
}
static void t_sel_clear(void *a1) { ((void (*)(void *))VA(0x100140A18ULL))(a1); }
static void *t_note_at(void *a1, const float *pos) {   // タップ位置のノート(スプライト)。標準のタッチ処理(sub_100140664)と同じ検索
    uint8_t *s = (uint8_t *)a1;
    void *cont = *(void **)(s + 1792);
    if (!cont) return NULL;
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const float *))VA(0x100546980ULL))(cont, pos);
    int div = *(uint8_t *)(s + 2576); if (div < 1) div = 1;
    float K = *(float *)(s + 2580) * *(float *)(s + 2584);
    float w = K / div, lim = *(float *)(s + 2516);
    float tol = (w < lim ? w : lim) * 0.5f;
    return ((void *(*)(void *, float, float))VA(0x1000FF264ULL))(cont, r.v[0], tol);
}
static void t_sel_toast(void *a1) {
    char b[48]; snprintf(b, sizeof b, "Selected: %lu", (unsigned long)t_sel_nodes(a1).count);
    ed_toast_text(a1, b, 4);
}
static void t_sel_all(void *a1) { VT(a1, 1584, void (*)(void *))(a1); t_sel_toast(a1); }
static void t_sel_delete(void *a1) {
    NSMutableArray<NSValue *> *hits = [NSMutableArray array];
    for (NSValue *v in t_sel_nodes(a1)) {
        void *sp = v.pointerValue;
        void *ref = VT(sp, 728, void *(*)(void *))(sp);
        uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;
        if (nd && nd[2]) [hits addObject:[NSValue valueWithPointer:(void *)nd[2]]];
    }
    if (!hits.count) { ed_toast_text(a1, "Nothing selected", 4); return; }
    for (NSUInteger i = 0; i < hits.count; i++)
        ((long (*)(void *, void *, int))VA(0x100141108ULL))(a1, hits[i].pointerValue, i + 1 == hits.count);
    un_mark();
    char b[48]; snprintf(b, sizeof b, "Deleted %lu note(s)", (unsigned long)hits.count);
    ed_toast_text(a1, b, 4);
}

// ---- 選択したノートへ hs を適用 ----
// hs は「次の hs 点まで引き継がれる」ので、連続して選んだノートの区間(ラン)は、先頭の拍に hs=値 を 1 点置けば流れる。
// ランの次のノート(選んでいないノート)の拍には、適用前の hs(元の効果列でのその拍の値)を置いて戻す。断続的な選択は長さ 1 のランとして同じ扱い。
// ラン内に元からあった hs の点は消す(他の項目は残す)。同じ拍に複数ノートがある場合は、1 つでも選ばれていればその拍を選択扱い。
#define BM_TAG_THS 93
typedef struct { double key; int m, num, den; BOOL sel; } THit;
static void t_apply_hs(void *a1, double v) {
    gEditor = a1;
    gOuter = *(void **)((uint8_t *)a1 + 0x2d8);
    NSArray<NSValue *> *selNodes = t_sel_nodes(a1);
    if (!selNodes.count) { ed_toast_text(a1, "Select notes first", 4); return; }
    NSMutableSet *selSet = [NSMutableSet set];
    for (NSValue *x in selNodes) [selSet addObject:x];
    nvec *ch = node_children(*(void **)((uint8_t *)a1 + 1792));
    NSMutableArray<NSValue *> *hits = [NSMutableArray array];
    for (void **p = ch->begin; p != ch->end; p++) {
        void *ref = VT(*p, 728, void *(*)(void *))(*p);
        uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;
        if (!nd || !nd[2]) continue;
        uint8_t *hit = (uint8_t *)nd[2];
        THit h; h.m = *(int *)(hit + 64); h.num = (int)*(unsigned *)(hit + 72); h.den = (int)*(unsigned *)(hit + 76);
        if (h.den <= 0) h.den = 1;
        h.key = h.m + (double)h.num / h.den;
        h.sel = [selSet containsObject:[NSValue valueWithPointer:*p]];
        [hits addObject:[NSValue valueWithBytes:&h objCType:@encode(THit)]];
    }
    NSArray *sorted = [hits sortedArrayUsingComparator:^NSComparisonResult(NSValue *a, NSValue *b) {
        THit x, y; [a getValue:&x]; [b getValue:&y];
        return x.key < y.key ? NSOrderedAscending : (x.key > y.key ? NSOrderedDescending : NSOrderedSame);
    }];
    // 同じ拍をまとめる
    NSMutableArray<NSValue *> *beats = [NSMutableArray array];
    for (NSValue *val in sorted) {
        THit h; [val getValue:&h];
        if (beats.count) {
            THit last; [beats.lastObject getValue:&last];
            if (fabs(last.key - h.key) < 1e-9) { if (h.sel) { last.sel = YES; beats[beats.count - 1] = [NSValue valueWithBytes:&last objCType:@encode(THit)]; } continue; }
        }
        [beats addObject:val];
    }
    if (!work_load()) { ed_toast_text(a1, "Effects unavailable", 2); return; }
    // 元の hs 列(拍, 値)
    NSMutableArray<NSNumber *> *oKey = [NSMutableArray array], *oVal = [NSMutableArray array];
    for (size_t i = 0; i < eff_count(); i++) { uint8_t *e = eff_at(i); if (!e[48]) continue; [oKey addObject:@(beat_key(e))]; [oVal addObject:@(*(double *)(e + 56))]; }
    double (^origHS)(double) = ^double(double k) {
        double r = 1.0;
        for (NSUInteger i = 0; i < oKey.count; i++) if (oKey[i].doubleValue <= k + 1e-9) r = oVal[i].doubleValue;
        return r;
    };
    // ランの計画: (開始拍, 次の拍(無ければ NULL))
    NSMutableArray<NSDictionary *> *plan = [NSMutableArray array];
    NSUInteger i = 0, nsel = 0;
    while (i < beats.count) {
        THit h; [beats[i] getValue:&h];
        if (!h.sel) { i++; continue; }
        NSUInteger j = i;
        while (j + 1 < beats.count) { THit nx; [beats[j + 1] getValue:&nx]; if (!nx.sel) break; j++; }
        THit e1, nx; [beats[j] getValue:&e1];
        BOOL hasNext = j + 1 < beats.count;
        if (hasNext) [beats[j + 1] getValue:&nx];
        [plan addObject:@{ @"i": @(i), @"j": @(j), @"next": @(hasNext), @"from": beats[i], @"nextBeat": hasNext ? beats[j + 1] : beats[j] }];
        nsel += j - i + 1;
        i = j + 1;
    }
    // 1) ラン内(開始 ≤ 拍 < 次の拍。次が無ければ最後の拍まで)の元の hs 点を消す
    for (NSDictionary *r in plan) {
        THit f; [r[@"from"] getValue:&f];
        THit nb; [r[@"nextBeat"] getValue:&nb];
        BOOL hasNext = [r[@"next"] boolValue];
        for (size_t k = 0; k < eff_count(); k++) {
            uint8_t *e = eff_at(k);
            if (e[48]) continue;
            double kk = beat_key(e);
            if (kk >= f.key - 1e-9 && (hasNext ? kk < nb.key - 1e-9 : kk <= nb.key + 1e-9)) e[48] = 1;
        }
    }
    // 2) 戻す値を先に確定(元の列で)→ 3) 点を置く(ランの先頭に v、次の拍に元の値)
    NSMutableArray<NSDictionary *> *puts = [NSMutableArray array];
    for (NSDictionary *r in plan) {
        THit f; [r[@"from"] getValue:&f];
        [puts addObject:@{ @"m": @(f.m), @"num": @(f.num), @"den": @(f.den), @"v": @(v) }];
        if ([r[@"next"] boolValue]) {
            THit nb; [r[@"nextBeat"] getValue:&nb];
            double back = origHS(nb.key);
            if (fabs(back - v) > 1e-9) [puts addObject:@{ @"m": @(nb.m), @"num": @(nb.num), @"den": @(nb.den), @"v": @(back) }];
        }
    }
    for (NSDictionary *pt in puts) {
        BeatV b = { [pt[@"m"] intValue], [pt[@"num"] intValue], [pt[@"den"] intValue] };
        double key = b.m + (double)b.num / b.den;
        long found = -1;
        for (size_t k = 0; k < eff_count(); k++) if (fabs(beat_key(eff_at(k)) - key) < 1e-9) { found = (long)k; break; }
        if (found >= 0) { uint8_t *e = eff_at(found); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; }
        else { uint8_t e[EFF_SZ]; eff_init(e); beat_set(e, b); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; eff_insert(e); }
    }
    for (long k = (long)eff_count() - 1; k >= 0; k--) if (!pl_fx_any(eff_at(k))) eff_remove((size_t)k);   // 何も残らない点は削除
    work_commit();
    un_mark();
    char msg[80]; snprintf(msg, sizeof msg, "HS %g applied: %lu note(s), %lu run(s)", v, (unsigned long)nsel, (unsigned long)plan.count);
    ed_toast_text(a1, msg, 4);
    os_log(L, "taiko hs: v=%g beats=%lu runs=%lu effects=%zu", v, (unsigned long)beats.count, (unsigned long)plan.count, eff_count());
}
// ---- hs ランプ: 選択の最初のノートと最後のノートの間で、hs を直線的(または曲線的)に増減させる ----
// 区間内のノートの拍(重複なし)ごとに hs の点を置く。値 = v0 → v1 を曲線で補間。最後のノートの次のノートの拍で、適用前の hs へ戻す。
//   曲線: l=直線 / i=加速(ease-in) / o=減速(ease-out) / s=なめらか(smoothstep) / e=指数(v0,v1 > 0 のとき等比)
#define BM_TAG_THR 94
static double t_curve(char c, double t, double v0, double v1) {
    switch (c) {
        case 'i': t = t * t; break;
        case 'o': t = 1.0 - (1.0 - t) * (1.0 - t); break;
        case 's': t = t * t * (3.0 - 2.0 * t); break;
        case 'e': if (v0 > 0 && v1 > 0) return v0 * pow(v1 / v0, t); break;
        default: break;
    }
    return v0 + (v1 - v0) * t;
}
static void t_apply_hs_ramp(void *a1, double v0, double v1, char curve) {
    gEditor = a1;
    gOuter = *(void **)((uint8_t *)a1 + 0x2d8);
    NSArray<NSValue *> *selNodes = t_sel_nodes(a1);
    NSMutableSet *selSet = [NSMutableSet set];
    for (NSValue *x in selNodes) [selSet addObject:x];
    nvec *ch = node_children(*(void **)((uint8_t *)a1 + 1792));
    NSMutableArray<NSValue *> *hits = [NSMutableArray array];
    for (void **p = ch->begin; p != ch->end; p++) {
        void *ref = VT(*p, 728, void *(*)(void *))(*p);
        uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;
        if (!nd || !nd[2]) continue;
        uint8_t *hit = (uint8_t *)nd[2];
        THit h; h.m = *(int *)(hit + 64); h.num = (int)*(unsigned *)(hit + 72); h.den = (int)*(unsigned *)(hit + 76);
        if (h.den <= 0) h.den = 1;
        h.key = h.m + (double)h.num / h.den;
        h.sel = [selSet containsObject:[NSValue valueWithPointer:*p]];
        [hits addObject:[NSValue valueWithBytes:&h objCType:@encode(THit)]];
    }
    NSArray *sorted = [hits sortedArrayUsingComparator:^NSComparisonResult(NSValue *a, NSValue *b) {
        THit x, y; [a getValue:&x]; [b getValue:&y];
        return x.key < y.key ? NSOrderedAscending : (x.key > y.key ? NSOrderedDescending : NSOrderedSame);
    }];
    NSMutableArray<NSValue *> *beats = [NSMutableArray array];
    for (NSValue *val in sorted) {
        THit h; [val getValue:&h];
        if (beats.count) {
            THit last; [beats.lastObject getValue:&last];
            if (fabs(last.key - h.key) < 1e-9) { if (h.sel) { last.sel = YES; beats[beats.count - 1] = [NSValue valueWithBytes:&last objCType:@encode(THit)]; } continue; }
        }
        [beats addObject:val];
    }
    NSInteger iS = -1, iE = -1;
    for (NSUInteger i = 0; i < beats.count; i++) { THit h; [beats[i] getValue:&h]; if (h.sel) { if (iS < 0) iS = (NSInteger)i; iE = (NSInteger)i; } }
    if (iS < 0 || iE <= iS) { ed_toast_text(a1, "Select a start note and an end note", 4); return; }
    if (!work_load()) { ed_toast_text(a1, "Effects unavailable", 2); return; }
    NSMutableArray<NSNumber *> *oKey = [NSMutableArray array], *oVal = [NSMutableArray array];
    for (size_t i = 0; i < eff_count(); i++) { uint8_t *e = eff_at(i); if (e[48]) continue; [oKey addObject:@(beat_key(e))]; [oVal addObject:@(*(double *)(e + 56))]; }
    double (^origHS)(double) = ^double(double k) {
        double r = 1.0;
        for (NSUInteger i = 0; i < oKey.count; i++) if (oKey[i].doubleValue <= k + 1e-9) r = oVal[i].doubleValue;
        return r;
    };
    THit bs, be, nx; [beats[(NSUInteger)iS] getValue:&bs]; [beats[(NSUInteger)iE] getValue:&be];
    BOOL hasNext = (NSUInteger)iE + 1 < beats.count;
    if (hasNext) [beats[(NSUInteger)iE + 1] getValue:&nx];
    for (size_t k = 0; k < eff_count(); k++) {      // 区間内の元の hs の点を消す
        uint8_t *e = eff_at(k);
        if (e[48]) continue;
        double kk = beat_key(e);
        if (kk >= bs.key - 1e-9 && (hasNext ? kk < nx.key - 1e-9 : kk <= be.key + 1e-9)) e[48] = 1;
    }
    NSMutableArray<NSDictionary *> *puts = [NSMutableArray array];
    double last = NAN;
    double span = be.key - bs.key;
    for (NSInteger i = iS; i <= iE; i++) {
        THit h; [beats[(NSUInteger)i] getValue:&h];
        double t = span > 1e-12 ? (h.key - bs.key) / span : 0.0;
        double val = round(t_curve(curve, t, v0, v1) * 1000.0) / 1000.0;
        if (val < 0.001) val = 0.001;
        if (!isnan(last) && fabs(val - last) < 1e-9 && i != iE) continue;       // 同じ値が続くところは省く
        [puts addObject:@{ @"m": @(h.m), @"num": @(h.num), @"den": @(h.den), @"v": @(val) }];
        last = val;
    }
    if (hasNext) {
        double back = origHS(nx.key);
        if (fabs(back - last) > 1e-9) [puts addObject:@{ @"m": @(nx.m), @"num": @(nx.num), @"den": @(nx.den), @"v": @(back) }];
    }
    for (NSDictionary *pt in puts) {
        BeatV b = { [pt[@"m"] intValue], [pt[@"num"] intValue], [pt[@"den"] intValue] };
        double key = b.m + (double)b.num / b.den;
        long found = -1;
        for (size_t k = 0; k < eff_count(); k++) if (fabs(beat_key(eff_at(k)) - key) < 1e-9) { found = (long)k; break; }
        if (found >= 0) { uint8_t *e = eff_at(found); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; }
        else { uint8_t e[EFF_SZ]; eff_init(e); beat_set(e, b); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; eff_insert(e); }
    }
    for (long k = (long)eff_count() - 1; k >= 0; k--) if (!pl_fx_any(eff_at(k))) eff_remove((size_t)k);
    work_commit();
    un_mark();
    char msg[96]; snprintf(msg, sizeof msg, "HS ramp %g -> %g (%c): %lu point(s)", v0, v1, curve, (unsigned long)puts.count);
    ed_toast_text(a1, msg, 4);
    os_log(L, "taiko hs ramp: %g->%g curve=%c points=%lu beats=%ld..%ld", v0, v1, curve, (unsigned long)puts.count, (long)iS, (long)iE);
}
// ---- hs の一括操作: 選択範囲(最初〜最後に選んだノート)の hs を消す / 倍率をかける ----
//   範囲 = 最初に選んだノートの拍 〜 最後に選んだノートの次のノートの手前。範囲の外の hs は変えない(範囲の次のノートの拍で元の値へ戻す)。
#define BM_TAG_THC 95
#define BM_TAG_THM 96
#define BM_TAG_THL 97
#define BM_TAG_TPM 98
static NSMutableArray<NSValue *> *t_beats_sel(void *a1, NSInteger *iS, NSInteger *iE) {
    NSMutableSet *selSet = [NSMutableSet set];
    for (NSValue *x in t_sel_nodes(a1)) [selSet addObject:x];
    nvec *ch = node_children(*(void **)((uint8_t *)a1 + 1792));
    NSMutableArray<NSValue *> *hits = [NSMutableArray array];
    for (void **p = ch->begin; p != ch->end; p++) {
        void *ref = VT(*p, 728, void *(*)(void *))(*p);
        uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;
        if (!nd || !nd[2]) continue;
        uint8_t *hit = (uint8_t *)nd[2];
        THit h; h.m = *(int *)(hit + 64); h.num = (int)*(unsigned *)(hit + 72); h.den = (int)*(unsigned *)(hit + 76);
        if (h.den <= 0) h.den = 1;
        h.key = h.m + (double)h.num / h.den;
        h.sel = [selSet containsObject:[NSValue valueWithPointer:*p]];
        [hits addObject:[NSValue valueWithBytes:&h objCType:@encode(THit)]];
    }
    NSArray *sorted = [hits sortedArrayUsingComparator:^NSComparisonResult(NSValue *a, NSValue *b) {
        THit x, y; [a getValue:&x]; [b getValue:&y];
        return x.key < y.key ? NSOrderedAscending : (x.key > y.key ? NSOrderedDescending : NSOrderedSame);
    }];
    NSMutableArray<NSValue *> *beats = [NSMutableArray array];
    for (NSValue *val in sorted) {
        THit h; [val getValue:&h];
        if (beats.count) {
            THit last; [beats.lastObject getValue:&last];
            if (fabs(last.key - h.key) < 1e-9) { if (h.sel) { last.sel = YES; beats[beats.count - 1] = [NSValue valueWithBytes:&last objCType:@encode(THit)]; } continue; }
        }
        [beats addObject:val];
    }
    *iS = *iE = -1;
    for (NSUInteger i = 0; i < beats.count; i++) { THit h; [beats[i] getValue:&h]; if (h.sel) { if (*iS < 0) *iS = (NSInteger)i; *iE = (NSInteger)i; } }
    return beats;
}
// mode 0 = 消す / 1 = 倍率 f をかける
static void t_hs_range_op(void *a1, int mode, double f) {
    gEditor = a1;
    gOuter = *(void **)((uint8_t *)a1 + 0x2d8);
    NSInteger iS, iE;
    NSMutableArray<NSValue *> *beats = t_beats_sel(a1, &iS, &iE);
    if (iS < 0) { ed_toast_text(a1, "Select notes first", 4); return; }
    if (!work_load()) { ed_toast_text(a1, "Effects unavailable", 2); return; }
    NSMutableArray<NSNumber *> *oKey = [NSMutableArray array], *oVal = [NSMutableArray array];
    for (size_t i = 0; i < eff_count(); i++) { uint8_t *e = eff_at(i); if (e[48]) continue; [oKey addObject:@(beat_key(e))]; [oVal addObject:@(*(double *)(e + 56))]; }
    double (^origHS)(double) = ^double(double k) {
        double r = 1.0;
        for (NSUInteger i = 0; i < oKey.count; i++) if (oKey[i].doubleValue <= k + 1e-9) r = oVal[i].doubleValue;
        return r;
    };
    BOOL (^hasPointAt)(double) = ^BOOL(double k) { for (NSNumber *x in oKey) if (fabs(x.doubleValue - k) < 1e-9) return YES; return NO; };
    THit bs, be, nx; [beats[(NSUInteger)iS] getValue:&bs]; [beats[(NSUInteger)iE] getValue:&be];
    BOOL hasNext = (NSUInteger)iE + 1 < beats.count;
    if (hasNext) [beats[(NSUInteger)iE + 1] getValue:&nx];
    double endKey = hasNext ? nx.key : be.key;
    double prior = origHS(bs.key - 1e-7), startVal = origHS(bs.key);
    int changed = 0;
    NSMutableArray<NSDictionary *> *puts = [NSMutableArray array];
    for (size_t k = 0; k < eff_count(); k++) {          // 範囲内の既存の hs 点
        uint8_t *e = eff_at(k);
        if (e[48]) continue;
        double kk = beat_key(e);
        if (kk < bs.key - 1e-9 || (hasNext ? kk >= endKey - 1e-9 : kk > endKey + 1e-9)) continue;
        if (mode == 0) e[48] = 1;
        else *(double *)(e + 56) = fmax(0.001, round(*(double *)(e + 56) * f * 1000.0) / 1000.0);
        changed++;
    }
    if (mode == 1 && !hasPointAt(bs.key)) {              // 範囲の先頭に点が無ければ、引き継いでいる値に倍率をかけた点を置く
        [puts addObject:@{ @"m": @(bs.m), @"num": @(bs.num), @"den": @(bs.den), @"v": @(fmax(0.001, round(startVal * f * 1000.0) / 1000.0)) }];
    }
    if (hasNext && !hasPointAt(nx.key)) {                // 範囲の次のノートで、元の値へ戻す
        double after = (mode == 0) ? prior : origHS(nx.key - 1e-7) * f;
        double back = origHS(nx.key);
        if (fabs(back - after) > 1e-9) [puts addObject:@{ @"m": @(nx.m), @"num": @(nx.num), @"den": @(nx.den), @"v": @(back) }];
    }
    for (NSDictionary *pt in puts) {
        BeatV b = { [pt[@"m"] intValue], [pt[@"num"] intValue], [pt[@"den"] intValue] };
        double key = b.m + (double)b.num / b.den;
        long found = -1;
        for (size_t k = 0; k < eff_count(); k++) if (fabs(beat_key(eff_at(k)) - key) < 1e-9) { found = (long)k; break; }
        if (found >= 0) { uint8_t *e = eff_at(found); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; }
        else { uint8_t e[EFF_SZ]; eff_init(e); beat_set(e, b); e[48] = 0; *(double *)(e + 56) = [pt[@"v"] doubleValue]; eff_insert(e); }
    }
    for (long k = (long)eff_count() - 1; k >= 0; k--) if (!pl_fx_any(eff_at(k))) eff_remove((size_t)k);
    work_commit();
    un_mark();
    char msg[96];
    if (mode == 0) snprintf(msg, sizeof msg, "HS cleared in range (%d point(s))", changed);
    else snprintf(msg, sizeof msg, "HS x%g in range (%d point(s), +%lu)", f, changed, (unsigned long)puts.count);
    ed_toast_text(a1, msg, 4);
    os_log(L, "taiko hs range op: mode=%d f=%g changed=%d puts=%lu", mode, f, changed, (unsigned long)puts.count);
}
// 「その他」メニューへ Taiko 用の項目を足す(右ツールメニューは 9 枠が埋まっていて足せない): 選択モード / 全選択 / 解除 / 選択を削除
static void *gTMenuFor;
static void taiko_add_menu_items(void *self) {
    if (gTMenuFor == self) return;
    gTMenuFor = self;
    void *tb = (uint8_t *)self + 1400;
    add_menu_item(tb, "ui-edit-icon12.png", BM_TAG_TSEL, "Select mode");
    add_menu_item(tb, "ui-edit-icon13.png", BM_TAG_TALL, "Select all");
    add_menu_item(tb, "ui-edit-icon21.png", BM_TAG_TCLR, "Deselect");
    add_menu_item(tb, "ui-edit-icon25.png", BM_TAG_TDEL, "Delete selected");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_THS, "Apply HS to selection");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_THR, "HS ramp (start-end)");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_THM, "HS multiply (range)");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_THC, "HS clear (range)");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_THL, "HS labels (show values)");
    add_menu_item(tb, "ui-edit-icon26.png", BM_TAG_TPM, "Paste mirrored (don/ka)");
    add_menu_item(tb, "ui-edit-icon28.png", BM_TAG_PDEV, "Plugin Dev...");
}
// 右ツールメニューのボタン(tag 88)/ 他のツール選択でのモード解除。hook_touch から呼ぶ(戻り値 1 = 処理した)
// 反転ペースト: 標準の貼り付け(vtable +1728)を使い、クリップボード内の don/ka(種類 0↔2, 1↔3)を入れ替えてから貼る。貼った後はクリップボードを元に戻す
static void t_clip_swap(void *a1) {
    uint8_t *c = *(uint8_t **)((uint8_t *)a1 + 840);
    if (!c) return;
    for (void **p = *(void ***)c; p != *(void ***)(c + 8); p++) {
        if (!*p) continue;
        uint8_t *h = g_dynamic_cast(*p, 0x1009340E0ULL, 0x100934270ULL);   // HitObject → HitObjectTaiko
        if (!h) continue;
        int t = *(int *)(h + 148);
        if (t >= 0 && t < 4) ((void (*)(void *, long, long))VA(0x1001EBAF4ULL))(h, t ^ 2, 0);   // 種類設定(+148 と音の種類も更新)
    }
}
static void t_paste_mirror(void *a1) {
    un_mark();
    t_clip_swap(a1);
    VT(a1, 1728, void (*)(void *))(a1);
    t_clip_swap(a1);
    un_mark();
}
static int taiko_menu_click(void *self, void *node, int ev, int tag) {
    if (!is_taiko(self)) return 0;
    if (self != gBarEd) { gBarEd = self; gBarMode = NO; gBarGesture = NO; gSelMode = NO; gSelGesture = NO; }
    if (tag == BM_TAG_TBAR) {
        if (ev == 3) {
            gBarMode = !gBarMode;
            if (gBarMode) gSelMode = NO;
            VT(node, 1032, void (*)(void *, int))(node, gBarMode ? 255 : 130);     // 点灯 / 減光
            ed_toast_text(self, gBarMode ? "Bar line tool: tap to add / remove" : "Bar line tool: off", 4);
        }
        return 1;
    }
    if (tag == BM_TAG_TSEL) {
        if (ev == 3) {
            gSelMode = !gSelMode;
            if (gSelMode) gBarMode = NO; else t_sel_clear(self);
            ed_toast_text(self, gSelMode ? "Select mode: tap notes (empty = deselect)" : "Select mode: off", 4);
        }
        return 1;
    }
    if (tag == BM_TAG_THL) {
        if (ev == 3) {
            gHsLbl = !gHsLbl;
            ed_toast_text(self, gHsLbl ? (gPreview ? "HS labels: on" : "HS labels: on (turn on FX preview to see them)") : "HS labels: off", 4);
        }
        return 1;
    }
    if (tag == BM_TAG_TPM) { if (ev == 3) { gEditor = self; t_paste_mirror(self); } return 1; }
    if (tag == BM_TAG_TDEL) { if (ev == 3) t_sel_delete(self); return 1; }
    if (tag == BM_TAG_TALL) { if (ev == 3) t_sel_all(self); return 1; }
    if (tag == BM_TAG_TCLR) { if (ev == 3) { t_sel_clear(self); ed_toast_text(self, "Selected: 0", 4); } return 1; }
    if (ev == 3 && ((tag >= 60 && tag <= 63) || tag == 85)) { gBarMode = NO; gSelMode = NO; }   // 他のツールを選んだらノートに戻る(アイコンの減光は次回の入場で戻る)
    else if (ev == 3 && tag >= 64 && tag <= 69) gBarMode = NO;                                   // ステップボタンは選択中のノートに効くので選択モードは保つ
    return 0;
}

typedef intptr_t (*ttouch_fn)(void *, int, void *, float *);
static ttouch_fn orig_ttouch;
static intptr_t hook_ttouch(void *a1, int ev, void *node, float *pos) {
    if (ev != 7) un_mark();      // 編集(ノートの追加 / 削除 / 移動)が落ち着いたら Undo の履歴に記録する
    if (ev != 7 && pos && pl_right_lane_map(a1, pos)) return 0;   // プラグイン一覧(右)の上のタッチは編集に渡さない
    if (ev != 7 && pos && pl_touch(a1, ev, pos)) return 1;         // Interact 型プラグインが有効な間は、帯のタッチをプラグインへ(OnClick / OnDrag)
    if (gPreview && a1 == gEditor && ev != 7 && pos) {   // FX プレビュー中は編集(帯の中のタッチ)を無効化。帯の外(スクロール)は標準のまま
        uint8_t *s = (uint8_t *)a1;
        if (fabsf(pos[1] - *(float *)(s + 2072)) * 2.0f < *(float *)(s + 2080)) return 1;
    }
    if (gSelMode && a1 == gBarEd && ev != 7) {
        uint8_t *s = (uint8_t *)a1;
        if (gSelGesture) { if (ev == 2 || ev == 3) gSelGesture = NO; return 1; }     // 進行中の選択ジェスチャは最後まで飲み込む
        float yc = *(float *)(s + 2072), band = *(float *)(s + 2080);
        BOOL inBand = pos && fabsf(pos[1] - yc) * 2.0f < band && *(void **)(s + 2488) != node;
        if (ev == 0 && inBand && !s[901] && !s[903]) {
            void *sp = t_note_at(a1, pos);
            if (sp) { t_sel_set(a1, sp, !t_sel_has(a1, sp)); t_sel_toast(a1); }
            else { t_sel_clear(a1); ed_toast_text(a1, "Selected: 0", 4); }
            gSelGesture = YES;
            return 1;
        }
    }
    if (gBarMode && a1 == gBarEd && ev != 7) {
        uint8_t *s = (uint8_t *)a1;
        if (gBarGesture) { if (ev == 2 || ev == 3) gBarGesture = NO; return 1; }      // 進行中の小節線ジェスチャは最後まで飲み込む
        float yc = *(float *)(s + 2072), band = *(float *)(s + 2080);
        BOOL inBand = pos && fabsf(pos[1] - yc) * 2.0f < band && *(void **)(s + 2488) != node;
        if (ev == 0 && inBand && !s[901] && !s[903]) {
            double beat;
            if (taiko_snap_beat(a1, pos, &beat)) { taiko_bar_toggle(a1, beat); gBarGesture = YES; return 1; }
        }
    }
    return orig_ttouch(a1, ev, node, pos);
}

// ---- Taiko の FX プレビュー(表示のみ) ----
// 配置: ノート x = K*(G(beat)-gG0)(gG0 = 0)、コンテナ x = -K*G(pos)。G は effect の scroll / jump の積分(Key と同じ G())。
//   コンテナ: +1792 = ノート / +1816 = 小節線 / +1808 = グリッド(子=点)/ +1800 = 小節線のサムネ(線形のまま)。
//   ノート: 子の vt+728 = NoteData(+16 = HitObject, +64 = 開始 Beat{m,num,den} / +88 = 終端 Beat)。ロングは sub_100174DF4(sprite, 長さ)で伸ばす(長さ = Δbeat*zoom)。
// ___dynamic_cast はゲーム本体の import スタブ(0x100742274)を使う
static inline void node_set_x(void *n, float x) { VT(n, 184, void (*)(void *, float))(n, x); }
static inline double t_K(void *a1) { return (double)(*(float *)((uint8_t *)a1 + 2580) * *(float *)((uint8_t *)a1 + 2584)); }
static double gTgPos = 1e30;
static void t_scroll_apply(void *a1) {
    uint8_t *s = (uint8_t *)a1;
    double pos = *(double *)(s + 872);
    float x = -(float)(t_K(a1) * G(pos));
    node_set_x(*(void **)(s + 1792), x);
    node_set_x(*(void **)(s + 1816), x);
    node_set_x(*(void **)(s + 1808), x);
}
// 画面上の位置(コンテナ座標): hs はノート個別の速度係数 = 判定線(現在位置 gp)からの距離にかかる。X = K*(gp + hs*(G(b)-gp))
static inline double t_X(double K, double gp, double hs, double g) { return K * (gp + hs * (g - gp)); }
typedef struct { void *n, *hs; double b0, b1; } TNote;
static TNote *gTN; static size_t gTNn, gTNcap;
static double *gGV, *gGH; static size_t gGn, gGcap;   // グリッドの点: G 値 / hs(子の生成順)
static void t_note_apply(void *a1, TNote *t, double K, double gp, double base) {
    double h = HS(t->b0), g0 = G(t->b0);
    node_set_x(t->n, (float)t_X(K, gp, h, g0));
    if (t->hs) ((void (*)(void *, float))VA(0x100174DF4ULL))(t->hs, (float)(K * h * (G(t->b1) - g0) / base));
}
static void t_place_notes(void *a1) {
    uint8_t *s = (uint8_t *)a1;
    double K = t_K(a1), base = *(float *)(s + 2584), gp = G(*(double *)(s + 872));
    nvec *ch = node_children(*(void **)(s + 1792));
    size_t cnt = (size_t)(ch->end - ch->begin);
    if (cnt > gTNcap) { gTNcap = cnt + 256; gTN = realloc(gTN, gTNcap * sizeof(TNote)); }
    gTNn = 0;
    for (void **p = ch->begin; p != ch->end; p++) {
        void *ref = VT(*p, 728, void *(*)(void *))(*p);
        uint8_t *hit = ref ? *(uint8_t **)((uint8_t *)ref + 16) : NULL;
        if (!hit) continue;
        TNote t = { *p, NULL, (double)*(int *)(hit + 64) + (*(unsigned *)(hit + 76) ? (double)*(unsigned *)(hit + 72) / (double)*(unsigned *)(hit + 76) : 0.0), 0 };
        t.hs = g_dynamic_cast(*p, 0x10095EF00ULL, 0x100923570ULL);   // HitSpriteTaiko(ロング)
        if (t.hs) t.b1 = ((double (*)(void *))VA(0x1001F2F9CULL))(hit + 88);
        gTN[gTNn++] = t;
        t_note_apply(a1, &gTN[gTNn - 1], K, gp, base);
    }
}
static void t_grid(void *a1) {
    uint8_t *s = (uint8_t *)a1;
    void *gn = *(void **)(s + 1808);
    VT(gn, 640, void (*)(void *))(gn);                                  // 子を全部外す(標準の再構築と同じ)
    int div = *(uint8_t *)(*(uint8_t **)(s + 1112) + 1452);
    *(uint8_t *)(s + 2576) = (uint8_t)div;
    if (div < 1) div = 1;
    double K = t_K(a1), pos = *(double *)(s + 872), gp = G(pos);
    float W = *(float *)(s + 664), y = *(float *)(s + 2072), jx = *(float *)(s + 2088);
    gTgPos = gp; gGn = 0;
    if (!gGV) { gGcap = 8192; gGV = malloc(gGcap * 8); gGH = malloc(gGcap * 8); }
    uint64_t colBeat = (uint64_t)*(uint16_t *)VA(0x1016CC9F8ULL) | ((uint64_t)*(uint8_t *)VA(0x1016CC9FAULL) << 16);
    typedef void (*dot_fn)(void *, float *, long, long, uint64_t);
    dot_fn dot = (dot_fn)VA(0x100143C98ULL);
    long c = (long)floor(pos);
    for (long n = c - 300; n <= c + 300; n++) {
        if (n < -4) continue;
        double h = HS((double)n), g0 = G((double)n), g1 = G((double)n + 1);
        double x0 = K * h * (g0 - gp), x1 = K * h * (g1 - gp);
        if (fmax(x0, x1) < -jx - W || fmin(x0, x1) > W - jx + W) continue;   // 画面の左右 1 画面ぶんまで
        float p[2] = { (float)t_X(K, gp, h, g0), y };
        dot(a1, p, 1, 204, colBeat);
        if (gGn < gGcap) { gGV[gGn] = g0; gGH[gGn] = h; gGn++; }
        if (div >= 2 && fabs(x1 - x0) / div >= 2.0) {
            for (int j = 1; j < div; j++) {
                int r, g, b;
                if (2 * j == div) { r = 102; g = 205; b = 239; }
                else if (4 * j == div || 4 * j == 3 * div) { r = 239; g = 102; b = 238; }
                else if (div == 6 || div == 3) { r = 102; g = 239; b = 170; }
                else { r = 150; g = 150; b = 150; }
                double gj = g0 + (g1 - g0) * j / div;
                float q[2] = { (float)t_X(K, gp, h, gj), y };
                dot(a1, q, 0, 178, (uint64_t)r | ((uint64_t)g << 8) | ((uint64_t)b << 16));
                if (gGn < gGcap) { gGV[gGn] = gj; gGH[gGn] = h; gGn++; }
            }
        }
    }
}
static void t_bars_warp(void *a1) {   // 小節線スプライト(コンテナ +1816 の子、位置 = a1+1952 の beat 配列)を G / hs で配置
    uint8_t *s = (uint8_t *)a1;
    double K = t_K(a1), gp = G(*(double *)(s + 872));
    double *bt = *(double **)(s + 1952);
    size_t n = (*(uint8_t **)(s + 1960) - *(uint8_t **)(s + 1952)) / 8;
    nvec *ch = node_children(*(void **)(s + 1816));
    size_t m = (size_t)(ch->end - ch->begin);
    for (size_t i = 0; i < n && i < m; i++) node_set_x(ch->begin[i], (float)t_X(K, gp, HS(bt[i]), G(bt[i])));
}
// hs があるとき: 現在位置が動くたびにノート / グリッド / 小節線を置き直す(距離が現在位置基準のため)
static double gHsLastGp = 1e30;
static void t_hs_update(void *a1) {
    uint8_t *s = (uint8_t *)a1;
    double gp = G(*(double *)(s + 872));
    if (gp == gHsLastGp) return;
    gHsLastGp = gp;
    double K = t_K(a1), base = *(float *)(s + 2584);
    nvec *ch = node_children(*(void **)(s + 1792));
    if ((size_t)(ch->end - ch->begin) != gTNn) t_place_notes(a1);      // 子の数が変わった(キャッシュ失効)
    else for (size_t i = 0; i < gTNn; i++) t_note_apply(a1, &gTN[i], K, gp, base);
    nvec *gc = node_children(*(void **)(s + 1808));
    size_t m = (size_t)(gc->end - gc->begin);
    for (size_t i = 0; i < gGn && i < m; i++) node_set_x(gc->begin[i], (float)t_X(K, gp, gGH[i], gGV[i]));
    t_bars_warp(a1);
}

// ---- hs の値の表示: hs が 1 以外のノート / 直前から値が変わるノートの上に数値を出す(FX プレビュー中) ----
//   ラベルはノート用コンテナ(+1792)の親に載せる(コンテナの子にすると、標準処理がラベルをノートとして扱ってしまう)
#define T_HSL_MAX 96
static void *gHsLblEd, *gHsLblParent, *gHsLblPool[T_HSL_MAX]; static int gHsLblN;
static void *t_parent_of(void *a1, void *cont) {
    if (cont == NULL) return NULL;
    uintptr_t img = VA(0x100000000ULL);
    for (int off = 8; off < 0x300; off += 8) {
        void *p = NULL;
        if (!bm_safe_read((uint8_t *)cont + off, &p, 8) || !p || ((uintptr_t)p & 7) || (uintptr_t)p < 0x100000000ULL || (uintptr_t)p >= 0x800000000000ULL) continue;
        if (p == a1) return a1;
        uintptr_t vt = 0;
        if (!bm_safe_read(p, &vt, 8) || vt < img || vt >= img + 0x20000000ULL) continue;
        void *c0 = NULL;
        void *vtf = NULL;
        if (!bm_safe_read((void *)(vt + 552), &vtf, 8) || !vtf) continue;
        nvec *ch = node_children(p);
        if (!ch) continue;
        void **b = NULL, **e = NULL;
        if (!bm_safe_read(&ch->begin, &b, 8) || !bm_safe_read(&ch->end, &e, 8) || !b || e < b || (size_t)(e - b) > 100000) continue;
        for (void **q = b; q != e; q++) { if (!bm_safe_read(q, &c0, 8)) break; if (c0 == cont) return p; }
    }
    return NULL;
}
static void t_hs_labels_hide(void) { for (int i = 0; i < gHsLblN; i++) node_visible(gHsLblPool[i], 0); }
static void t_hs_labels(void *a1) {
    uint8_t *s = (uint8_t *)a1;
    if (a1 != gHsLblEd) { gHsLblEd = a1; gHsLblParent = NULL; gHsLblN = 0; }      // 別のエディタ: プールは捨てる(ノードはエディタと一緒に消える)
    if (!gHsLbl || !gPreview || !gSegN) { t_hs_labels_hide(); return; }
    void *cont = *(void **)(s + 1792);
    if (!gHsLblParent) { gHsLblParent = t_parent_of(a1, cont); if (!gHsLblParent) { gHsLbl = NO; ed_toast_text(a1, "HS labels: container not found", 4); return; } }
    float cx = VT(cont, 192, float (*)(void *))(cont), cy = VT(cont, 208, float (*)(void *))(cont);
    float W = *(float *)(s + 664), band = *(float *)(s + 2080);
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(s + 672), "NormalFont_H");
    nvec *ch = node_children(cont);
    int used = 0;
    for (void **p = ch->begin; p != ch->end && used < T_HSL_MAX; p++) {
        float nx = VT(*p, 192, float (*)(void *))(*p);
        float sx = cx + nx;
        if (sx < -fontH * 3 || sx > W + fontH * 3) continue;                     // 画面外は省く
        void *ref = VT(*p, 728, void *(*)(void *))(*p);
        uint8_t *hit = ref ? *(uint8_t **)((uint8_t *)ref + 16) : NULL;
        if (!hit) continue;
        double b0 = (double)*(int *)(hit + 64) + (*(unsigned *)(hit + 76) ? (double)*(unsigned *)(hit + 72) / (double)*(unsigned *)(hit + 76) : 0.0);
        double h = HS(b0), hp = HS(b0 - 1e-7);
        if (fabs(h - 1.0) < 1e-9 && fabs(hp - 1.0) < 1e-9) continue;              // 1 のまま = 何も出さない
        if (used >= gHsLblN) {
            void *lb = ((void *(*)(float))VA(0x1001F3608ULL))(fontH * 2.2f);
            if (!lb) break;
            VT(gHsLblParent, 504, void (*)(void *, void *, int))(gHsLblParent, lb, 89002);
            float an[2] = { 0.5f, 0.5f };
            VT(lb, 296, void (*)(void *, float *))(lb, an);
            mk_set_color(lb, 255, 50, 50);
            gHsLblPool[gHsLblN++] = lb;
        }
        void *lb = gHsLblPool[used++];
        char tx[24]; snprintf(tx, sizeof tx, "%.3g", h);
        set_label_text(lb, tx);
        float ny = VT(*p, 208, float (*)(void *))(*p);
        float pos[2] = { sx, cy + ny - band * 0.5f - fontH * 1.4f };
        VT(lb, 136, void (*)(void *, float *))(lb, pos);
        node_visible(lb, 1);
    }
    for (int i = used; i < gHsLblN; i++) node_visible(gHsLblPool[i], 0);
}

typedef void (*trelayout_fn)(void *, float);
static trelayout_fn orig_trelayout;
static void hook_trelayout(void *a1, float zoom) {   // sub_10013F280: ズーム/再配置
    orig_trelayout(a1, zoom);
    if (gPreview && a1 == gEditor) { gHsLastGp = 1e30; t_place_notes(a1); t_grid(a1); t_bars_warp(a1); t_scroll_apply(a1); }
}
static void t_relayout(void *a1) { orig_trelayout ? hook_trelayout(a1, *(float *)((uint8_t *)a1 + 2580)) : (void)0; }
typedef void (*trefresh_fn)(void *, float);
static trefresh_fn orig_trefresh;
static void hook_trefresh(void *a1, float dt) {      // sub_10013EDF8(EditorTaiko の毎フレーム)
    orig_trefresh(a1, dt);
    un_tick(a1);
    pl_right_tick(a1);           // 右のプラグイン一覧(コンセントボタン)の作り直し
    if (gPreview && a1 == gEditor) {
        t_scroll_apply(a1);
        if (gHsOn) t_hs_update(a1);
        double gp = G(*(double *)((uint8_t *)a1 + 872));
        if (fabs(gp - gTgPos) * t_K(a1) > *(float *)((uint8_t *)a1 + 664) * 0.4) t_grid(a1);   // 画面幅の 0.4 倍動いたらグリッドを作り直す
    }
    if (a1 == gEditor || a1 == gHsLblEd) t_hs_labels(a1);
    if (a1 == gModEd || a1 == gTipEd) mod_update(a1);          // プラグインの描画物 / ShowTip
}
typedef void (*tbars_fn)(void *, int);
static tbars_fn orig_tbars;
static void hook_tbars(void *a1, int recalc) {       // sub_10013FAE8: 小節線スプライトの配置
    orig_tbars(a1, recalc);
    if (gPreview && a1 == gEditor) t_bars_warp(a1);
}
typedef void (*tgrid_fn)(void *);
static tgrid_fn orig_tgrid;
static void hook_tgrid(void *a1) {                   // sub_1001402C4: グリッド再構築(最後に小節線も引き直す)
    if (gPreview && a1 == gEditor) { t_grid(a1); ((void (*)(void *, int))VA(0x10013FAE8ULL))(a1, 1); return; }
    orig_tgrid(a1);
}
typedef void (*tgridpos_fn)(void *);
static tgridpos_fn orig_tgridpos;
static void hook_tgridpos(void *a1) {                // sub_10013EF0C: グリッドの位置(プレビュー中は t_scroll_apply が決める)
    if (gPreview && a1 == gEditor) return;
    orig_tgridpos(a1);
}
