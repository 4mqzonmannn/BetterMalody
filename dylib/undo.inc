// ---- 取り消し / やり直し(Undo / Redo) ----
// 4.3.7 の標準エディタには Undo が無い。ノート(全モード)と effect の編集をスナップショット差分で記録する。
//   ・各ノートに「影」(= clone() で複製した直前の状態)を持ち、編集が落ち着いた時点(最後のタッチの 0.35 秒後)で現在の譜面と比較
//   ・差分(追加 / 削除 / 変更)を 1 件の履歴としてまとめる。Undo は逆順に、Redo は順に適用する
//   ・復元は標準の「削除」「追加」手順で行う(変更 = 削除して複製を追加。ノートのポインタは変わるため ID 相当の UnNode で追跡)
//   ・比較は clone と原本の差(パディング・ポインタ)を学習したマスクで行い、実際の値だけを見る
// 未対応: BPM の編集(effect とノートのみ)。
#include <malloc/malloc.h>
#define BM_TAG_UNDO 770
#define BM_TAG_REDO 771
#define UN_MAX 40

@interface UnNode : NSObject { @public void *ptr; void *shadow; }
@end
@implementation UnNode
- (void)dealloc { if (shadow) VT(shadow, 8, void (*)(void *))(shadow); }
@end
@interface UnOp : NSObject { @public int kind; /* 0 追加 1 削除 2 変更 */ UnNode *node; void *oldN; void *newN; }
@end
@implementation UnOp
- (void)dealloc {
    if (oldN) VT(oldN, 8, void (*)(void *))(oldN);
    if (newN) VT(newN, 8, void (*)(void *))(newN);
}
@end
@interface UnEntry : NSObject { @public NSMutableArray<UnOp *> *ops; NSArray<NSData *> *fxOld, *fxNew, *bpmOld, *bpmNew; NSDictionary *metaOld, *metaNew; }
@end
@implementation UnEntry
@end

static inline void *g_dynamic_cast(void *o, uint64_t srcTi, uint64_t dstTi) { return ((void *(*)(void *, const void *, const void *, long))VA(0x100742274ULL))(o, (const void *)VA(srcTi), (const void *)VA(dstTi), 0); }
static void t_sel_clear(void *a1);
static BOOL is_key_editor(void *ed);
static void *gUnEd;
static NSMutableDictionary<NSNumber *, UnNode *> *gUnNodes;
static NSMutableArray<UnEntry *> *gUnUndo, *gUnRedo;
static NSArray<NSData *> *gUnFx;
static NSMutableDictionary<NSNumber *, NSMutableData *> *gUnMask;   // vtable → 学習済みのノイズ語(1 バイト/語)
static BOOL gUnTouched, gUnDirty, gUnBusy;
static CFAbsoluteTime gUnDirtyT;
static NSUInteger gUnBaseCount;

static void un_mark(void) { gUnTouched = YES; gUnDirty = YES; gUnDirtyT = CFAbsoluteTimeGetCurrent(); }
static NSNumber *un_key(const void *p) { return @((uintptr_t)p); }
static NSArray<NSValue *> *un_live(void *ed, BOOL *ok) {   // 画面のノートコンテナの子(スプライト)が持つノートを重複なしで列挙
    NSMutableArray *a = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    *ok = NO;
    BOOL tk = is_taiko(ed);
    void *cont = *(void **)((uint8_t *)ed + (tk ? 1792 : 1472));    // Taiko はノートのコンテナが +1792
    if (!cont) return a;
    nvec *ch = node_children(cont);
    if (!ch || !ch->begin || ch->end < ch->begin || (ch->end - ch->begin) > 3000000) return a;
    for (void **p = ch->begin; p != ch->end; p++) {
        void *note;
        if (tk) {   // Taiko: スプライト → NoteData(vt+728)→ +16 = HitObject
            void *ref = VT(*p, 728, void *(*)(void *))(*p);
            uint64_t *nd = ref ? (uint64_t *)g_dynamic_cast(ref, 0x100965948ULL, 0x10091EF80ULL) : NULL;
            note = nd ? (void *)nd[2] : NULL;
        } else note = VT(*p, 704, void *(*)(void *))(*p);
        if (!note || !malloc_size(note)) continue;
        uint64_t vt = *(uint64_t *)note;
        if (vt < VA(0x1008E0000ULL) || vt > VA(0x100960000ULL)) continue;   // HitObject 系の vtable 範囲外は対象外
        NSNumber *k = @((uintptr_t)note);
        if ([seen containsObject:k]) continue;
        [seen addObject:k];
        [a addObject:[NSValue valueWithPointer:note]];
    }
    *ok = YES;
    return a;
}
static NSArray<NSData *> *un_fx_read(void) {
    NSMutableArray *a = [NSMutableArray array];
    if (!gOuter) return a;
    deq *q = EFFQ();
    if (!deq_sane(q, 112)) return a;
    for (size_t i = 0; i < q->size; i++) {
        uint8_t *o = deq_at(q, i);
        if (!o || *(uintptr_t *)o != VA(BM_VA_EFFECT_VT)) return @[];
        [a addObject:[NSData dataWithBytes:o length:EFF_SZ]];
    }
    return a;
}
static BOOL un_ptrlike(uint64_t w) { return w >= 0x100000000ULL && w < 0x1000000000ULL; }
static NSMutableData *un_mask_for(const uint8_t *n) {
    NSNumber *k = @(*(uint64_t *)n);
    NSMutableData *m = gUnMask[k];
    if (!m) { m = [NSMutableData dataWithLength:64]; gUnMask[k] = m; }
    return m;
}
static void un_learn(const uint8_t *src, const uint8_t *cl) {   // 原本と複製の差(= ノイズ)を学習
    size_t n = malloc_size(src) / 8;
    if (n > 64) n = 64;
    uint8_t *m = (uint8_t *)un_mask_for(src).mutableBytes;
    for (size_t i = 1; i < n; i++)
        if (((const uint64_t *)src)[i] != ((const uint64_t *)cl)[i]) m[i] = 1;
}
static BOOL un_same(const uint8_t *a, const uint8_t *b) {
    if (*(uint64_t *)a != *(uint64_t *)b) return NO;
    size_t n = malloc_size(a) / 8;
    if (n > 64) n = 64;
    const uint8_t *m = (const uint8_t *)un_mask_for(a).bytes;
    for (size_t i = 1; i < n; i++) {
        if (m[i]) continue;
        uint64_t x = ((const uint64_t *)a)[i], y = ((const uint64_t *)b)[i];
        if (x != y && !(un_ptrlike(x) && un_ptrlike(y))) return NO;
    }
    if (!is_taiko(gUnEd) && lay_of(gUnEd)->slide) {  // Slide の節は内容を比較
        const uint8_t *ab = *(uint8_t **)(a + 136), *ae = *(uint8_t **)(a + 144);
        const uint8_t *bb = *(uint8_t **)(b + 136), *be = *(uint8_t **)(b + 144);
        size_t la = (ab && ae >= ab) ? (size_t)(ae - ab) : 0, lb = (bb && be >= bb) ? (size_t)(be - bb) : 0;
        if (la != lb) return NO;
        if (la && memcmp(ab, bb, la)) return NO;
    }
    return YES;
}
static void *un_clone(const uint8_t *src) {
    void *c = note_clone((void *)src);
    if (c) un_learn(src, c);
    return c;
}

// ---- BPM / メタ情報(情報画面の項目)の記録と復元 ----
static NSArray<NSData *> *gUnBpm;
static NSDictionary *gUnMeta;
static NSArray<NSData *> *un_bpm_read(void) {
    NSMutableArray *a = [NSMutableArray array];
    if (!gOuter) return a;
    deq *q = BPMQ();
    if (!deq_sane(q, 48)) return a;
    for (size_t i = 0; i < q->size; i++) {
        uint8_t *o = deq_at(q, i);
        if (!o) return @[];
        [a addObject:[NSData dataWithBytes:o length:48]];
    }
    return a;
}
static void un_ed_refresh_timing(void *ed) {     // 情報画面の BPM 反映(sub_10007AFF8)と同じ後処理
    uint8_t *e = (uint8_t *)ed;
    void *outer = *(void **)(e + 728);
    ((void (*)(void *))VA(0x1001C717CULL))(outer);
    ((void (*)(void *, void *))VA(0x100021630ULL))(e + 744, (uint8_t *)outer + 584);
    ((void (*)(void *))VA(0x10007AA24ULL))(ed);
    VT(ed, 1616, void (*)(void *))(ed);
    e[887] = 1;
}
static void un_bpm_write(void *ed, NSArray<NSData *> *list) {
    typedef void *(*new_fn)(size_t);
    static new_fn nw;
    if (!nw) nw = (new_fn)dlsym(RTLD_DEFAULT, "_Znwm");
    void *outer = *(void **)((uint8_t *)ed + 728);
    if (!nw || !outer) return;
    ((void (*)(deq *))VA(VA_DEQ_CLEAR))(BPMQ());
    for (NSData *d in list) {
        uint8_t *p = nw(0x30);
        memcpy(p, d.bytes, 48);
        ((void (*)(void *, void *))VA(0x1001C6D18ULL))(outer, p);          // 並び順つきで追加(BPM 追加と同じ)
    }
    un_ed_refresh_timing(ed);
}
static NSString *un_strp_get(uint8_t *field) {          // field = std::string* を保持する場所
    uint8_t *p = *(uint8_t **)field;
    uint64_t empty = *(uint64_t *)VA(0x10169F208ULL);
    if (!p || (uint64_t)p == empty || (uintptr_t)p < 0x100000) return @"";
    return [NSString stringWithUTF8String:stdstr_c(p)] ?: @"";
}
static void un_assign(void *str, const char *v) {
    typedef void *(*assign_fn)(void *, const char *);
    static assign_fn as;
    if (!as) as = (assign_fn)dlsym(RTLD_DEFAULT, "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");
    if (as) as(str, v);
}
static void un_strp_set(uint8_t *field, NSString *v) {
    uint64_t empty = *(uint64_t *)VA(0x10169F208ULL);
    if (!*(uint8_t **)field || *(uint64_t *)field == empty) {
        if (!v.length) return;
        // 共有の空文字列を、専用の文字列(v のコピー)へ置き換える: sub_100005FDC(field, const std::string *src)(標準の情報画面と同じ)
        uint8_t tmp[48] = {0};
        ((void (*)(void *, const char *))VA(0x10007309CULL))(tmp, v.UTF8String);
        ((void (*)(void *, void *))VA(0x100005FDCULL))(field, tmp);
        return;
    }
    un_assign(*(uint8_t **)field, v.UTF8String);
}
static NSString *un_stre_get(uint8_t *s) { return [NSString stringWithUTF8String:stdstr_c(s)] ?: @""; }
typedef struct { const char *name; int off; int kind; } UnMetaF;   // kind: 0 = std::string*(ポインタ) 1 = 埋め込み std::string 2 = int
static const UnMetaF kUnMeta[] = {
    { "version", 288, 0 }, { "creator", 320, 0 }, { "video", 112, 1 }, { "bga", 472, 1 }, { "skin", 448, 1 },
    { "preview", 400, 2 }, { "bar_begin", 404, 2 }, { "speed", 348, 2 },
};
static NSDictionary *un_meta_read(void *ed) {
    uint8_t *outer = *(uint8_t **)((uint8_t *)ed + 728);
    NSMutableDictionary *d = [NSMutableDictionary dictionary];
    if (!outer) return d;
    for (size_t i = 0; i < sizeof kUnMeta / sizeof *kUnMeta; i++) {
        const UnMetaF *f = &kUnMeta[i];
        NSString *k = [NSString stringWithUTF8String:f->name];
        if (f->kind == 0) d[k] = un_strp_get(outer + f->off);
        else if (f->kind == 1) d[k] = un_stre_get(outer + f->off);
        else d[k] = @(*(int32_t *)(outer + f->off));
    }
    uint8_t *song = *(uint8_t **)(outer + 416);
    if (song && (uintptr_t)song > 0x100000) {
        d[@"title"] = un_strp_get(song + 160); d[@"artist"] = un_strp_get(song + 168);
        d[@"titleorg"] = un_strp_get(song + 120); d[@"artistorg"] = un_strp_get(song + 128);
    }
    uint8_t *snd = ((uint8_t *(*)(void *, int))VA(0x1001C7F2CULL))(outer, 0);   // offset(情報画面と同じ取得)
    if (snd) d[@"offset"] = @(*(int32_t *)(snd + 80));
    return d;
}
static void un_meta_write(void *ed, NSDictionary *d) {   // 現在値と異なる項目だけを書き戻す(未変更の項目には触れない)
    uint8_t *outer = *(uint8_t **)((uint8_t *)ed + 728);
    if (!outer) return;
    NSDictionary *cur = un_meta_read(ed);
    BOOL num = NO;
    for (size_t i = 0; i < sizeof kUnMeta / sizeof *kUnMeta; i++) {
        const UnMetaF *f = &kUnMeta[i];
        NSString *k = [NSString stringWithUTF8String:f->name];
        id v = d[k];
        if (!v || [v isEqual:cur[k]]) continue;
        os_log(L, "undo: meta %{public}@ <- %{public}@", k, v);
        if (f->kind == 0) un_strp_set(outer + f->off, v);
        else if (f->kind == 1) un_assign(outer + f->off, [(NSString *)v UTF8String]);
        else { *(int32_t *)(outer + f->off) = [(NSNumber *)v intValue]; num = YES; }
    }
    uint8_t *song = *(uint8_t **)(outer + 416);
    if (song && (uintptr_t)song > 0x100000) {
        static const struct { const char *k; int off; } S[4] = { { "title", 160 }, { "artist", 168 }, { "titleorg", 120 }, { "artistorg", 128 } };
        for (int i = 0; i < 4; i++) {
            NSString *k = [NSString stringWithUTF8String:S[i].k];
            id v = d[k];
            if (!v || [v isEqual:cur[k]]) continue;
            os_log(L, "undo: meta %{public}@ <- %{public}@", k, v);
            un_strp_set(song + S[i].off, v);
        }
    }
    id off = d[@"offset"];
    if (off && ![off isEqual:cur[@"offset"]]) {
        uint8_t *snd = ((uint8_t *(*)(void *, int))VA(0x1001C7F2CULL))(outer, 0);
        if (snd) {
            int v = [off intValue];
            os_log(L, "undo: meta offset <- %d", v);
            *(int32_t *)(snd + 80) = v; *(int32_t *)(snd + 84) = v;
            VT(ed, 1544, void (*)(void *))(ed);                        // 情報画面の offset 反映と同じ
        }
    }
    if (num) info_fill_labels(ed);                                      // 情報画面の追加項目の表示を更新
    os_log(L, "undo: meta write done");
}
// 情報画面の入力欄(editor+1152 のベクタ)の表示を現在の値へ合わせる。入力欄の中身が古いままだと、
// 情報画面を閉じたときに標準の確定処理(sub_10007AFF8)が古い値を書き戻してしまう。
static void un_info_boxes_refresh(void *ed) {
    uint8_t *e = (uint8_t *)ed;
    void **b = *(void ***)(e + 1152), **en = *(void ***)(e + 1160);
    if (!b || en < b || (en - b) > 64) return;
    NSDictionary *m = un_meta_read(ed);
    deq *q = BPMQ();
    NSString *bpm = nil;
    if (gOuter && deq_sane(q, 48) && q->size) {
        double bl = *(double *)(deq_at(q, 0) + 40);
        if (bl > 0) bpm = [NSString stringWithFormat:@"%.3f", 60000.0 / bl];
    }
    for (long i = 0; b + i < en; i++) {
        NSString *t = nil;
        switch (i) {
            case 0: t = m[@"title"]; break;
            case 1: t = m[@"artist"]; break;
            case 2: t = m[@"version"]; break;
            case 3: t = m[@"creator"]; break;
            case 6: t = bpm; break;
            case 7: t = m[@"offset"] ? [NSString stringWithFormat:@"%d", [m[@"offset"] intValue]] : nil; break;
            case 11: t = m[@"preview"] ? [NSString stringWithFormat:@"%d", [m[@"preview"] intValue]] : nil; break;
            case 12: t = m[@"titleorg"]; break;
            case 13: t = m[@"artistorg"]; break;
        }
        if (!t) continue;
        void *tb = cast_to(b[i], 0x1008F0B00ULL);        // Textbox
        if (!tb) continue;
        tb_set(tb, t);
        VT(b[i], 720, void (*)(void *, void *))(b[i], NULL);   // 「編集済み」の印を外す(確定処理の対象から外す)
    }
}

static void un_reset(void) {
    gUnNodes = [NSMutableDictionary dictionary];
    gUnUndo = [NSMutableArray array]; gUnRedo = [NSMutableArray array];
    gUnMask = [NSMutableDictionary dictionary];
    gUnFx = nil; gUnBpm = nil; gUnMeta = nil; gUnBaseCount = 0; gUnDirty = NO; gUnTouched = NO;
}
static void un_baseline(void *ed) {
    BOOL ok;
    NSArray *live = un_live(ed, &ok);
    if (!ok) return;
    un_reset();
    gUnEd = ed;
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    for (NSValue *v in live) {
        UnNode *nd = [UnNode new];
        nd->ptr = v.pointerValue;
        nd->shadow = un_clone(nd->ptr);
        gUnNodes[un_key(nd->ptr)] = nd;
    }
    gUnBaseCount = live.count;
    gUnFx = un_fx_read();
    gUnBpm = un_bpm_read();
    gUnMeta = un_meta_read(ed);
    os_log(L, "undo: baseline %lu notes, %lu effects", (unsigned long)live.count, (unsigned long)gUnFx.count);
}

// ---- 標準の追加 / 削除手順(貼り付け・プラグインと同じ) ----
static void un_insert(void *ed, uint8_t *n) {
    uint8_t *e = (uint8_t *)ed;
    if (is_taiko(ed)) {   // Taiko: スプライト生成(sub_100142950)→ 譜面へ登録 → 音の更新 → 後処理(標準の追加の完了手順 sub_100140BD0 と同じ)
        void *snd = *(void **)(e + 1080);
        ((void (*)(void *, void *))VA(0x100142950ULL))(ed, n);
        ((void (*)(void *, void *))VA(0x1001C6834ULL))(*(void **)(e + 2064), n);
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, (long)*(int32_t *)(n + 80));
        ((void (*)(void *))VA(0x10006C288ULL))(snd);
        VT(ed, 1648, void (*)(void *, void *))(ed, n);
        return;
    }
    BOOL slide = lay_of(ed)->slide ? YES : NO;
    void *chart = *(void **)(e + (slide ? 1592 : 1624));
    void *lay = *(void **)(e + 1112), *snd = *(void **)(e + 1080);
    BOOL path = slide && (n[113] & 8);
    BOOL hold = !slide && (n[112] & 0x80);
    int snum = *(int32_t *)(n + 80);
    if (!slide) {
        if (hold) ((void (*)(void *, void *))VA(0x1001B395CULL))(ed, n);
        ((void (*)(void *, void *))VA(0x1001C6834ULL))(chart, n);
        if (!hold) ((void (*)(void *, void *))VA(0x1001B395CULL))(ed, n);
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, snum);
        ((void (*)(void *))VA(0x10006C288ULL))(snd);
        if (hold) ((void (*)(void *, void *))VA(0x1001A32D8ULL))(lay, n);
        ((void (*)(void *, void *, long))VA(0x1001A3174ULL))(lay, n, 0);
    } else {
        ((void (*)(void *, void *))VA(0x1001C6834ULL))(chart, n);
        if (path) ((void *(*)(void *, void *))VA(0x1000C88C8ULL))(ed, n);
        else ((void (*)(void *, void *))VA(0x1000C8A68ULL))(ed, n);
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, snum);
        ((void (*)(void *))VA(0x10006C288ULL))(snd);
    }
    VT(ed, 1648, void (*)(void *, void *))(ed, n);
}
static void un_remove(void *ed, uint8_t *n) {
    if (is_taiko(ed)) { ((void (*)(void *, void *, int))VA(0x100141108ULL))(ed, n, 1); return; }   // Taiko: 標準の削除
    if (lay_of(ed)->slide) ((void (*)(void *, void *, int, int))VA(0x1000C79F8ULL))(ed, n, 1, 0);
    else ((void (*)(void *, void *, int))VA(0x1001B287CULL))(ed, n, 1);
}

// ---- 差分の記録 ----
static void un_checkpoint(void *ed) {
    if (gUnEd != ed || gUnBusy) return;
    BOOL ok;
    NSArray *live = un_live(ed, &ok);
    if (!ok) return;
    UnEntry *en = [UnEntry new];
    en->ops = [NSMutableArray array];
    NSMutableSet<NSNumber *> *liveSet = [NSMutableSet setWithCapacity:live.count];
    for (NSValue *v in live) {
        uint8_t *p = v.pointerValue;
        NSNumber *k = un_key(p);
        [liveSet addObject:k];
        UnNode *nd = gUnNodes[k];
        if (!nd) {
            nd = [UnNode new];
            nd->ptr = p; nd->shadow = un_clone(p);
            gUnNodes[k] = nd;
            UnOp *op = [UnOp new]; op->kind = 0; op->node = nd; op->newN = un_clone(p);
            [en->ops addObject:op];
        } else if (!un_same(p, nd->shadow)) {
            UnOp *op = [UnOp new]; op->kind = 2; op->node = nd; op->oldN = nd->shadow; op->newN = un_clone(p);
            nd->shadow = un_clone(p);
            [en->ops addObject:op];
        }
    }
    for (NSNumber *k in [gUnNodes allKeys]) {
        if ([liveSet containsObject:k]) continue;
        UnNode *nd = gUnNodes[k];
        UnOp *op = [UnOp new]; op->kind = 1; op->node = nd; op->oldN = nd->shadow;
        nd->shadow = NULL; nd->ptr = NULL;
        [gUnNodes removeObjectForKey:k];
        [en->ops addObject:op];
    }
    NSArray<NSData *> *fx = un_fx_read();
    if (gUnFx && ![fx isEqualToArray:gUnFx]) { en->fxOld = gUnFx; en->fxNew = fx; }
    gUnFx = fx;
    NSArray<NSData *> *bpm = un_bpm_read();
    if (gUnBpm && ![bpm isEqualToArray:gUnBpm]) { en->bpmOld = gUnBpm; en->bpmNew = bpm; }
    gUnBpm = bpm;
    NSDictionary *meta = is_taiko(ed) ? gUnMeta : un_meta_read(ed);   // Taiko は情報画面の項目の取り消しは対象外
    if (gUnMeta && ![meta isEqualToDictionary:gUnMeta]) {
        en->metaOld = gUnMeta; en->metaNew = meta;
        for (NSString *k in meta) if (![meta[k] isEqual:gUnMeta[k]]) os_log(L, "undo: meta changed %{public}@: %{public}@ -> %{public}@", k, gUnMeta[k], meta[k]);
    }
    gUnMeta = meta;
    if (!en->ops.count && !en->fxOld && !en->bpmOld && !en->metaOld) return;
    [gUnUndo addObject:en];
    if (gUnUndo.count > UN_MAX) [gUnUndo removeObjectAtIndex:0];
    [gUnRedo removeAllObjects];
    os_log(L, "undo: entry ops=%lu fx=%d bpm=%d meta=%d (stack %lu)", (unsigned long)en->ops.count, en->fxOld ? 1 : 0, en->bpmOld ? 1 : 0, en->metaOld ? 1 : 0, (unsigned long)gUnUndo.count);
}

// ---- 適用 ----
static void un_node_remove(void *ed, UnNode *nd) {
    if (!nd->ptr) return;
    [gUnNodes removeObjectForKey:un_key(nd->ptr)];
    un_remove(ed, nd->ptr);
    nd->ptr = NULL;
    if (nd->shadow) { VT(nd->shadow, 8, void (*)(void *))(nd->shadow); nd->shadow = NULL; }
}
static void un_node_insert(void *ed, UnNode *nd, const uint8_t *src) {
    uint8_t *n = (uint8_t *)note_clone((void *)src);
    if (!n) return;
    un_insert(ed, n);
    nd->ptr = n;
    nd->shadow = note_clone(n);
    gUnNodes[un_key(n)] = nd;
}
static void un_apply(void *ed, UnEntry *en, BOOL undo) {
    gUnBusy = YES;
    if (is_taiko(ed)) t_sel_clear(ed);
    else { sel_snapshot_drop(); sel_clear(ed); }
    NSArray<UnOp *> *ops = undo ? [[en->ops reverseObjectEnumerator] allObjects] : en->ops;
    for (UnOp *op in ops) {
        int k = op->kind;
        if (undo) {
            if (k == 0) un_node_remove(ed, op->node);                          // 追加の取り消し = 削除
            else if (k == 1) un_node_insert(ed, op->node, op->oldN);           // 削除の取り消し = 復元
            else { un_node_remove(ed, op->node); un_node_insert(ed, op->node, op->oldN); }
        } else {
            if (k == 0) un_node_insert(ed, op->node, op->newN);
            else if (k == 1) un_node_remove(ed, op->node);
            else { un_node_remove(ed, op->node); un_node_insert(ed, op->node, op->newN); }
        }
    }
    NSArray<NSData *> *fx = undo ? en->fxOld : en->fxNew;
    if (fx) {
        gWork = [NSMutableArray array];
        for (NSData *d in fx) [gWork addObject:[d mutableCopy]];
        work_commit();
        gUnFx = fx;
    } else gUnFx = un_fx_read();
    NSArray<NSData *> *bpm = undo ? en->bpmOld : en->bpmNew;
    if (bpm) { un_bpm_write(ed, bpm); gUnBpm = bpm; } else gUnBpm = un_bpm_read();
    NSDictionary *meta = undo ? en->metaOld : en->metaNew;
    if (meta && !is_taiko(ed)) { un_meta_write(ed, meta); gUnMeta = meta; } else if (!is_taiko(ed)) gUnMeta = un_meta_read(ed);
    if ((en->metaOld || en->bpmOld) && !is_taiko(ed)) un_info_boxes_refresh(ed);
    *((uint8_t *)ed + 883) = 1;
    if (gPreview && ed == gEditor) ed_relayout(ed);
    gUnBusy = NO;
    BOOL ok; gUnBaseCount = un_live(ed, &ok).count;
}
static void un_do(void *ed, BOOL undo) {
    if (*((uint8_t *)ed + 903)) { ed_toast_lv(ed, 2036, 2); return; }
    if (gUnEd != ed) { un_baseline(ed); }
    gEditor = ed;
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    un_checkpoint(ed);                          // 落ち着く前の編集も先に記録
    NSMutableArray *from = undo ? gUnUndo : gUnRedo, *to = undo ? gUnRedo : gUnUndo;
    if (!from.count) { ed_toast_text(ed, undo ? "Nothing to undo" : "Nothing to redo", 2); return; }
    UnEntry *en = from.lastObject;
    [from removeLastObject];
    un_apply(ed, en, undo);
    [to addObject:en];
    char b[48]; snprintf(b, sizeof b, "%s (%lu left)", undo ? "Undo" : "Redo", (unsigned long)from.count);
    ed_toast_text(ed, b, 4);
    os_log(L, "undo: %{public}s ops=%lu", undo ? "undo" : "redo", (unsigned long)en->ops.count);
}
// 毎フレーム: 初期状態の取り込み / 編集が落ち着いたら差分を記録
static void un_tick(void *ed) {
    if (!is_key_editor(ed) || gUnBusy) return;
    if (gUnEd != ed) {
        BOOL ok; NSArray *live = un_live(ed, &ok);
        if (ok) un_baseline(ed);
        return;
    }
    if (!gUnTouched) {                          // 読み込み中(最初の操作より前): 譜面の件数が変わったら取り直す
        static int cnt;
        if (++cnt % 20 == 0) { BOOL ok; NSArray *live = un_live(ed, &ok); if (ok && live.count != gUnBaseCount) un_baseline(ed); }
        return;
    }
    if (gUnDirty && CFAbsoluteTimeGetCurrent() - gUnDirtyT > 0.35) {
        gUnDirty = NO;
        gEditor = ed;
        gOuter = *(void **)((uint8_t *)ed + 0x2d8);
        un_checkpoint(ed);
    }
}
