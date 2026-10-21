// ---- Lua プラグイン(Malody V エディタプラグイン互換) ----
// Documents/export_plugins/*.lua(ファイルアプリから追加できる)を一覧し、BPM ボタンの選択メニュー(BPM / Effect / Plugins)のパネルから実行する。
// パネルは Effect リストと同じ作り(スキンの _plpanel / _pllist を UI 初期化で結び付け、行は同じ生成処理)。
// Lua 本体と Editor/Review テーブルは lua_host.c。ここはエディタの内部へつなぐコールバック。
#import <ImageIO/ImageIO.h>
#include "zipx.h"
#define BM_TAG_PL_RUN    330
#define BM_TAG_PL_RELOAD 331
#define BM_TAG_PL_IMPORT 332
#define BM_TAG_PL_INOK   333   // GetUserInput: 入力確定
#define BM_TAG_PL_INBOX  334   // GetUserInput: 入力欄
#define BM_TAG_PL_CH0    335   // GetUserSelect / GetUserOption の選択肢 1〜4(335..338)

static NSArray<NSData *> *un_bpm_read(void);
static void un_bpm_write(void *ed, NSArray<NSData *> *list);
static void un_do(void *ed, BOOL undo);
static int mod_add(int kind, const char *name, const char *arg);
static void mod_set_num(const char *name, const char *key, double v);
static void mod_set_str(const char *name, const char *key, const char *v);
static void mod_set_beat(const char *name, const char *key, LHBeat b);
static void mod_set_color(const char *name, int r, int g, int b);
static void mod_set_parent(const char *name, const char *parent);
static void mod_remove(const char *name);
static int mod_exists(const char *name);
static int mod_get_num(const char *name, const char *key, double *out);
static NSString *pl_chart_dir(void);
static void pl_refresh_list(void);
static void pl_info_text(void);
static struct { void *panel, *list, *infoLbl, *editor, *inTb, *inLbl, *inBox, *okBtn, *chBtn[4], *chLbl[4]; int sel; } gPl;
static NSMutableArray<NSString *> *gPlFiles;
static NSString *gPlCurPath;                   // 実行中(有効中)のプラグインのファイル。素材画像をその隣から探す
static NSMutableArray<NSData *> *gPlInfos;       // LHInfo
static BOOL gPlClickRegistered;
static void *gPlRList;                 // skin の _right-pl(右ツールメニューのプラグイン一覧, ListSprite)
static void *gPlRBox;                  // その入れ物 _plr(一覧と同じ領域だけ。見えない間は非表示にしてタッチを邪魔しない)
static BOOL gPlRDirty, gPlRClickReg;
static BOOL gPlROn;                      // 右メニューのプラグイン一覧の表示 / 非表示(ボタンで切替)
static struct { void *n[5]; void *t[24]; float px[24], sx[24]; int cnt; BOOL applied, contScaled; float s, cx, listLeft; } gPlShrink;   // n[4] = _right-3 / t[] = 実際に縮めたノード

static NSString *pl_dir(void) {
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    return [docs stringByAppendingPathComponent:@"export_plugins"];   // 曲スキャンが先頭 "export" の名前を飛ばす(strncmp)ため、この名前にする
}
static void pl_install_samples(NSString *dir) {
    NSFileManager *fm = NSFileManager.defaultManager;
    if ([fm fileExistsAtPath:dir]) return;
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSDictionary *s = @{
      @"sample_mirror.lua":
        @"PluginName = 'Mirror selection (sample)'\nPluginMode = 0\nPluginType = 0\nPluginRequire = '5.0.1'\n"
        "function Run()\n  local keys = tonumber(Editor:ChartInfo('key')) or 4\n  local notes = Editor:GetSelectNotes()\n"
        "  if notes.Length == 0 then Editor:ShowMessage('no notes selected') return end\n"
        "  Editor:StartBatch()\n  for i = 0, notes.Length - 1 do\n    Editor:SetNoteX(notes[i], keys - 1 - Editor:GetNoteX(notes[i]))\n  end\n"
        "  Editor:FinishBatch()\nend\n",
      @"sample_shift_later.lua":
        @"PluginName = 'Shift selection +1 beat (sample)'\nPluginMode = 0\nPluginType = 0\nPluginRequire = '5.0.1'\n"
        "function Run()\n  local notes = Editor:GetSelectNotes()\n  Editor:StartBatch()\n  for i = 0, notes.Length - 1 do\n"
        "    local id = notes[i]\n    local b = Editor:GetNoteBeat(id, true)\n    b.beat = b.beat + 1\n    Editor:SetNoteBeat(id, b, true)\n  end\n"
        "  Editor:FinishBatch()\nend\n",
      @"sample_info.lua":
        @"PluginName = 'Chart info (sample)'\nPluginMode = 0\nPluginType = 0\nPluginRequire = '5.0.1'\n"
        "function Run()\n  Editor:ShowMessage(Editor:ChartInfo('title') .. ' / notes=' .. Editor:GetNoteCount() .. ' / bpm=' .. Editor:GetTimeCount())\nend\n",
    };
    for (NSString *k in s) [s[k] writeToFile:[dir stringByAppendingPathComponent:k] atomically:YES encoding:NSUTF8StringEncoding error:nil];
}
static void pl_scan(void) {
    NSString *dir = pl_dir();
    pl_install_samples(dir);
    gPlFiles = [NSMutableArray array]; gPlInfos = [NSMutableArray array];
    NSFileManager *fm = NSFileManager.defaultManager;
    NSMutableArray *paths = [NSMutableArray array];
    NSDirectoryEnumerator *en = [fm enumeratorAtPath:dir];       // サブフォルダも(深さ 4 まで・隠しは除く)
    NSMutableArray<NSString *> *rels = [NSMutableArray array];
    for (NSString *rel in en) {
        if (en.level > 4) { [en skipDescendants]; continue; }
        if ([rel.lastPathComponent hasPrefix:@"."]) { [en skipDescendants]; continue; }
        if ([rel.pathExtension.lowercaseString isEqualToString:@"lua"] && rels.count < 400) [rels addObject:rel];
    }
    for (NSString *rel in [rels sortedArrayUsingSelector:@selector(compare:)]) [paths addObject:[dir stringByAppendingPathComponent:rel]];
    for (NSString *p in paths) {
        NSData *src = [NSData dataWithContentsOfFile:p];
        LHInfo in; memset(&in, 0, sizeof in);
        if (!src) { in.mode = in.type = -1; snprintf(in.err, sizeof in.err, "cannot read"); }
        else lh_inspect(src.bytes, src.length, p.lastPathComponent.UTF8String, &in);
        if (!in.name[0]) snprintf(in.name, sizeof in.name, "%s", p.lastPathComponent.stringByDeletingPathExtension.UTF8String);
        [gPlFiles addObject:p];
        [gPlInfos addObject:[NSData dataWithBytes:&in length:sizeof in]];
    }
    os_log(L, "plugins: %lu in %{public}@", (unsigned long)gPlFiles.count, dir);
    gPlRDirty = YES;                       // 右メニューの一覧も作り直す
}
static const LHInfo *pl_info(NSUInteger i) { return (const LHInfo *)gPlInfos[i].bytes; }
static const char *pl_mode_name(int m) {
    switch (m) { case 0: return "Key"; case 3: return "Catch"; case 4: return "Pad"; case 5: return "Taiko"; case 6: return "Ring";
                 case 7: return "Slide"; case 8: return "Live"; case 9: return "Cube"; }
    return "Any";
}
static const char *pl_type_name(int t) { return t == 0 ? "Once" : t == 1 ? "Review" : t == 2 ? "Interact" : "?"; }

// ---- 実行時の状態(1 回の Run の間だけ有効) ----
static void *gPlEd;
static NSMutableArray<NSValue *> *gPlNotes;    // id = 添字。削除済みは NULL
static NSMutableArray<NSNumber *> *gPlLive;    // 生きている ID(拍→x の順)
static BOOL gPlLiveDirty;
static CFMutableDictionaryRef gPlSprite;       // note → sprite(生ポインタのまま保持)
static CFMutableDictionaryRef gPlId;           // note → id+1
static NSMutableSet<NSValue *> *gPlPending;    // AddNote で作った未挿入ノート
typedef struct { LHBeat b; const char *type; double v; } PlFx;
static NSMutableData *gPlFxData;
static int gPlEdits;
static BOOL gPlDeleted;
static NSMutableSet<NSValue *> *gPlSegDirty;     // 節を変更した(スプライトの作り直しが必要な)既存ノート
static char gPlInfoBuf[512];

static int16_t seg_x16(const uint8_t *s);
static BOOL pl_slide(void) { const Lay *l = lay_of(gPlEd); return l && l->slide ? YES : NO; }
// ---- Taiko 対応: ノート = HitObjectTaiko(スプライト +728 → NoteData → +16)。種類 hit+148 = 0 don / 1 大don / 2 ka / 3 大ka / 4 連打 / 5 大連打 / 6 風船 / 7 大風船
//   GetNoteType = 短いノート 1 / 長いノート 128、GetNoteFlag = 種類(0〜7)、SetNoteFlag で種類を変える。X = 0、Width = 1。
//   既存ノートの拍 / 種類の変更は「複製して差し替える」(標準の削除 + 追加)。差し替えたノートは Run の終了時に挿入する(pl_commit_pending)。
static BOOL pl_taiko(void) { return gPlEd && is_taiko(gPlEd); }
static void un_mark(void);
static NSArray<NSValue *> *t_sel_nodes(void *a1);
static void t_sel_set(void *a1, void *sp, BOOL on);
static void t_sel_clear(void *a1);
static uint8_t *pl_t_hit(void *sp) {
    void *ref = VT(sp, 728, void *(*)(void *))(sp);
    return ref ? *(uint8_t **)((uint8_t *)ref + 16) : NULL;
}
static inline BOOL pl_t_long(const uint8_t *n) { return (*(int32_t *)(n + 148) & ~3) == 4; }
static BOOL gPlCollected;          // ノート一覧の収集は最初に必要になったときだけ(Interact は毎フレーム呼ばれるため)
static void pl_need(void);
static uint8_t *pl_note(int id) {
    pl_need();
    if (id < 0 || id >= (int)gPlNotes.count) return NULL;
    return (uint8_t *)gPlNotes[id].pointerValue;
}
static void *pl_sprite(uint8_t *n) { return (n && gPlSprite) ? (void *)CFDictionaryGetValue(gPlSprite, n) : NULL; }
typedef struct { double key; int x; void *n; } PlSortEnt;
static int pl_cmp(const void *a, const void *b) {
    const PlSortEnt *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->x - y->x;
}
static void pl_rebuild_live(void) {
    NSUInteger n = gPlNotes.count;
    PlSortEnt *a = calloc(n ? n : 1, sizeof *a);
    NSUInteger m = 0;
    for (NSUInteger i = 0; i < n; i++) {
        uint8_t *nt = (uint8_t *)gPlNotes[i].pointerValue;
        if (!nt) continue;
        a[m].key = note_beat(nt, 64, 72, 76);
        a[m].x = pl_taiko() ? *(int32_t *)(nt + 148) : (pl_slide() ? nt[128] : nt[129]);
        a[m].n = (void *)(intptr_t)i;
        m++;
    }
    qsort(a, m, sizeof *a, pl_cmp);
    gPlLive = [NSMutableArray arrayWithCapacity:m];
    for (NSUInteger i = 0; i < m; i++) [gPlLive addObject:@((int)(intptr_t)a[i].n)];
    free(a);
    gPlLiveDirty = NO;
}
static void pl_do_collect(void *ed) {
    if (gPlSprite) CFRelease(gPlSprite);
    if (gPlId) CFRelease(gPlId);
    gPlSprite = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    gPlId = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    BOOL tk = is_taiko(ed);
    nvec *ch = node_children(*(void **)((uint8_t *)ed + (tk ? 1792 : 1472)));
    for (void **p = ch->begin; p != ch->end; p++) {
        void *note = tk ? (void *)pl_t_hit(*p) : VT(*p, 704, void *(*)(void *))(*p);
        if (note) CFDictionarySetValue(gPlSprite, note, *p);
    }
    // ノートは容器の子スプライト(userData = ノート)から集める。chart+184 のベクタは Slide では空、Key では別物(件数が合わない)
    os_log(L, "plugins: sprites=%lu", (unsigned long)(ch->end - ch->begin));
    for (void **p = ch->begin; p != ch->end; p++) {
        void *note = tk ? (void *)pl_t_hit(*p) : VT(*p, 704, void *(*)(void *))(*p);
        if (note) [gPlNotes addObject:[NSValue valueWithPointer:note]];
    }
    for (NSUInteger i = 0; i < gPlNotes.count; i++) CFDictionarySetValue(gPlId, gPlNotes[i].pointerValue, (const void *)(intptr_t)(i + 1));
    gPlLiveDirty = YES;
    // 効果(種類ごとに 1 件へ平坦化)
    gPlFxData = [NSMutableData data];
    deq *q = EFFQ();
    if (gOuter && deq_sane(q, 112)) {
        static const struct { int flag, val; const char *name; } K[3] = { { 32, 40, "sv" }, { 48, 56, "hs" }, { 88, 96, "jump" } };
        for (size_t i = 0; i < q->size; i++) {
            uint8_t *e = deq_at(q, i);
            if (!e) break;
            for (int k = 0; k < 3; k++) {
                if (e[K[k].flag]) continue;
                PlFx f = { { *(int32_t *)(e + 8), *(int32_t *)(e + 16), *(int32_t *)(e + 20) }, K[k].name, *(double *)(e + K[k].val) };
                [gPlFxData appendBytes:&f length:sizeof f];
            }
        }
    }
}

static void pl_need(void) {
    if (gPlCollected || !gPlEd) return;
    gPlCollected = YES;
    pl_do_collect(gPlEd);
}

// ---- 1 ノートの変更の前後処理(移動 / クオンタイズと同じ手順) ----
static void pl_pre(uint8_t *n) {
    void *snd = *(void **)((uint8_t *)gPlEd + 1080);
    ((void (*)(void *, long))VA(0x10006C244ULL))(snd, *(int32_t *)(n + 80));      // 旧位置の音を外す
}
static void pl_post(uint8_t *n) {
    uint8_t *e = (uint8_t *)gPlEd;
    void *snd = *(void **)(e + 1080), *lay = *(void **)(e + 1112);
    void *sp = pl_sprite(n);
    int now = (int)((double (*)(void *))VA(0x100021B8CULL))(e + 744);
    BOOL slide = pl_slide();
    if (sp) {
        if (!slide) ((void (*)(float *, void *, void *))VA(0x1001B2B84ULL))((float *)gPlEd, n, sp);
        else ((void (*)(void *, void *, void *))VA(0x1000C7CE8ULL))(gPlEd, n, sp);
    }
    (void)now;
    int ms0 = (int)time_of(n + 56);                 // 拍から求める(プラグインは再生位置と無関係な拍へ動かす)
    *(int32_t *)(n + 80) = ms0;
    if (!slide) *(int32_t *)(n + 104) = (int)time_of(n + 80);
    ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, ms0);
    if (!slide && sp) ((void (*)(void *, void *))VA(0x1001A331CULL))(lay, n);
    if (sp) VT(gPlEd, 1664, void (*)(void *, void *))(gPlEd, n);
    gPlEdits++;
}

// ---- ホストのコールバック ----
static int h_beat(int id, int head, LHBeat *o);
static int h_x(int id);
static int h_sel(int *ids, int max) {
    pl_need();
    int m = 0;
    if (pl_taiko()) {
        for (NSValue *sv in t_sel_nodes(gPlEd)) {
            if (m >= max) break;
            uint8_t *hit = pl_t_hit(sv.pointerValue);
            intptr_t id = hit ? (intptr_t)CFDictionaryGetValue(gPlId, hit) : 0;
            if (id > 0) ids[m++] = (int)id - 1;
        }
        return m;
    }
    for (NSValue *sv in sel_effective(gPlEd)) {   // 一覧のタップなどで標準の選択が解除されていたら、直前の選択(控え)を使う
        if (m >= max) break;
        void *sp = sv.pointerValue;
        void *note = VT(sp, 704, void *(*)(void *))(sp);
        intptr_t id = (intptr_t)CFDictionaryGetValue(gPlId, note);
        if (id > 0) ids[m++] = (int)id - 1;
    }
    // 選んだ順(タップ順・投げ縄の内部順)ではなく、時系列(始点の拍 → x)に並べて渡す。Malody V の GetSelectNotes と同じ(wipe を slide にまとめるプラグインなどが、seg を時系列で作れる)
    if (m > 1) {
        NSMutableArray<NSNumber *> *arr = [NSMutableArray arrayWithCapacity:(NSUInteger)m];
        for (int i = 0; i < m; i++) [arr addObject:@(ids[i])];
        [arr sortWithOptions:NSSortStable usingComparator:^NSComparisonResult(NSNumber *x, NSNumber *y) {
            LHBeat bx = {0, 0, 1}, by = {0, 0, 1};
            h_beat(x.intValue, 1, &bx); h_beat(y.intValue, 1, &by);
            double vx = bx.m + (double)bx.num / bx.den, vy = by.m + (double)by.num / by.den;
            if (vx != vy) return vx < vy ? NSOrderedAscending : NSOrderedDescending;
            int xx = h_x(x.intValue), xy = h_x(y.intValue);
            return xx == xy ? NSOrderedSame : (xx < xy ? NSOrderedAscending : NSOrderedDescending);
        }];
        for (int i = 0; i < m; i++) ids[i] = arr[(NSUInteger)i].intValue;
    }
    return m;
}
// 「順番に選択」: Run で待機状態に入り、タップした順にノートを控える(bm.m の ord_track)。もう一度 Run で、この順(GetOrderNotes)を渡して実行する
static NSMutableArray<NSValue *> *gOrdNotes;     // 控えたノート(ポインタ)をタップ順に
static BOOL gOrdMode; static NSUInteger gOrdIdx; static void *gOrdEd;
static void ord_marks_refresh(void *ed);
static void ord_tint_sync(void *ed);
static void ord_normal_reset(void *ed);
static int h_order(int *ids, int max) {
    pl_need();
    int m = 0;
    for (NSValue *nv in gOrdNotes) {
        if (m >= max) break;
        intptr_t id = (intptr_t)CFDictionaryGetValue(gPlId, nv.pointerValue);
        if (id > 0) ids[m++] = (int)id - 1;
    }
    return m;
}
static void h_order_clear(void) { [gOrdNotes removeAllObjects]; if (gPlEd) { ord_tint_sync(gPlEd); ord_marks_refresh(gPlEd); } }
static int h_count(void) { pl_need(); if (gPlLiveDirty) pl_rebuild_live(); return (int)gPlLive.count; }
static int h_at(int i) { pl_need(); if (gPlLiveDirty) pl_rebuild_live(); return (i >= 0 && i < (int)gPlLive.count) ? gPlLive[i].intValue : -1; }
static int h_type(int id) {
    uint8_t *n = pl_note(id);
    if (!n) return -1;
    if (pl_taiko()) return pl_t_long(n) ? 128 : 1;
    if (pl_slide()) return (n[113] & 8) ? 2048 : 1024;
    return (n[112] & 0x80) ? 128 : 1;
}
static int h_x(int id) { uint8_t *n = pl_note(id); return n ? (pl_taiko() ? 0 : (pl_slide() ? n[128] : n[129])) : -1; }
static int h_width(int id) { uint8_t *n = pl_note(id); return n ? ((pl_slide() && !pl_taiko()) ? n[129] : 1) : -1; }
static int h_flag(int id) { uint8_t *n = pl_note(id); return n ? (pl_taiko() ? *(int32_t *)(n + 148) : 0) : -1; }
static int h_beat(int id, int head, LHBeat *o) {
    uint8_t *n = pl_note(id);
    if (!n) return 0;
    const uint8_t *p = n + (head ? 64 : 88);
    o->m = *(int32_t *)p; o->num = *(int32_t *)(p + 8); o->den = *(int32_t *)(p + 12);
    if (o->den <= 0) o->den = 1;
    return 1;
}
static double h_time(int id, int head) {
    uint8_t *n = pl_note(id);
    if (!n || !gOuter) return -1;
    return time_of(n + (head ? 56 : 80));
}
static int h_del(int id);
static void pl_t_times(uint8_t *n) {      // 拍から時間(ms)を求め直す(開始 +80 / 終了 +104。短いノートは開始と同じ)
    *(int32_t *)(n + 80) = (int)time_of(n + 56);
    *(int32_t *)(n + 104) = (int)time_of(pl_t_long(n) ? n + 80 : n + 56);
}
static uint8_t *pl_t_replace(int id, uint8_t *n) {    // 既存ノートを複製して差し替える。元のノートは標準の削除、複製は未挿入(Run の終了時に挿入)
    uint8_t *c = note_clone(n);
    if (!c) return NULL;
    h_del(id);
    gPlNotes[(NSUInteger)id] = [NSValue valueWithPointer:c];
    CFDictionarySetValue(gPlId, c, (const void *)(intptr_t)(id + 1));
    [gPlPending addObject:[NSValue valueWithPointer:c]];
    gPlLiveDirty = YES;
    return c;
}
// Key のノートを直接生成する(配置処理 sub_1001B1FBC と同じ初期化。未挿入のまま返す)
static uint8_t *pl_key_new(int hold, int lane, Rat s, Rat e) {
    typedef void *(*new_fn)(size_t);
    static new_fn nw;
    if (!nw) nw = (new_fn)dlsym(RTLD_DEFAULT, "_Znwm");
    if (!nw || pl_slide() || pl_taiko()) return NULL;
    uint8_t *c = nw(0x88);
    if (!c) return NULL;
    memset(c, 0, 0x88);
    ((void (*)(void *))VA(0x1001EB7ECULL))(c);                  // HitObjectKey の初期化
    c[128] = c[129] = (uint8_t)lane;
    *(int32_t *)(c + 112) = hold ? 128 : 0;
    beat_write_rat(c + 64, s);
    beat_write_rat(c + 88, e);
    *(int32_t *)(c + 80) = (int)time_of(c + 56);
    *(int32_t *)(c + 104) = (int)time_of(c + 80);
    return c;
}
static int h_set_beat(int id, int head, LHBeat b) {
    uint8_t *n = pl_note(id);
    if (!n || b.den <= 0 || b.m < 0) return 0;
    BOOL pend = [gPlPending containsObject:[NSValue valueWithPointer:n]];
    Rat r = { (int64_t)b.m * b.den + b.num, b.den };
    if (pl_taiko()) {
        BOOL lng = pl_t_long(n);
        if (!head && !lng) return 0;                       // 短いノートに終点は無い
        Rat s0 = rat_of(n + 64), e0 = rat_of(n + 88);
        if (fabs(rat_dbl(r) - rat_dbl(head ? s0 : e0)) < 1e-12) return 1;
        if (!pend) { n = pl_t_replace(id, n); if (!n) return 0; }
        if (head) {
            beat_write_rat(n + 64, r);
            if (!lng || rat_dbl(rat_of(n + 88)) < rat_dbl(r)) beat_write_rat(n + 88, r);
        } else {
            if (rat_dbl(r) < rat_dbl(s0)) r = s0;
            beat_write_rat(n + 88, r);
        }
        pl_t_times(n);
        gPlLiveDirty = YES;
        return 1;
    }
    Rat s0 = rat_of(n + 64), e0 = rat_of(n + 88);
    BOOL held = rat_dbl(e0) > rat_dbl(s0) + 1e-9;
    if (!head && !pl_slide()) {
        // 標準の配置と同じ規則: 同じレーンの次のノーツへ重ならない(終端はそのノーツの開始より手前)
        int lane = n[129];
        BOOL found = NO; Rat nx = { 0, 1 };
        for (NSValue *v in gPlNotes) {
            uint8_t *o = (uint8_t *)v.pointerValue;
            if (!o || o == n || o[129] != lane) continue;
            Rat os = rat_of(o + 64);
            if (rat_dbl(os) <= rat_dbl(s0) + 1e-9) continue;
            if (!found || rat_dbl(os) < rat_dbl(nx)) { nx = os; found = YES; }
        }
        if (found && rat_dbl(r) >= rat_dbl(nx) - 1e-9) {
            r = (Rat){ nx.n * 16 - 1, nx.d * 16 };           // 次のノーツの 1/16(その分母)手前
            if (rat_dbl(r) <= rat_dbl(s0) + 1e-9) return 0;   // 間に入らない
        }
    }
    if (!head && !pend && !pl_slide() && !held && rat_dbl(r) > rat_dbl(s0) + 1e-9) {
        // Tap → Hold: 見た目(スプライト/レイヤー)が Hold 用に作られるので、Tap を消して Hold を新しく入れる
        int lane = n[129];
        uint8_t *c = pl_key_new(1, lane, s0, r);
        if (!c) return 0;
        h_del(id);
        gPlNotes[(NSUInteger)id] = [NSValue valueWithPointer:c];
        CFDictionarySetValue(gPlId, c, (const void *)(intptr_t)(id + 1));
        [gPlPending addObject:[NSValue valueWithPointer:c]];
        gPlLiveDirty = YES;
        return 1;
    }
    if (!pend) pl_pre(n);
    if (head) {
        beat_write_rat(n + 64, r);
        if (!held || rat_dbl(rat_of(n + 88)) < rat_dbl(r)) beat_write_rat(n + 88, r);
    } else {
        beat_write_rat(n + 88, r);
    }
    gPlLiveDirty = YES;
    if (!pend) pl_post(n);
    else if (!pl_slide()) { *(int32_t *)(n + 80) = (int)time_of(n + 56); *(int32_t *)(n + 104) = (int)time_of(n + 80); }
    return 1;
}
static int h_set_flag(int id, int kind) {      // Taiko: ノートの種類(0 don / 1 大don / 2 ka / 3 大ka / 4 連打 / 5 大連打 / 6 風船 / 7 大風船)
    uint8_t *n = pl_note(id);
    if (!n || !pl_taiko() || kind < 0 || kind > 7) return 0;
    if (*(int32_t *)(n + 148) == kind) return 1;
    if (![gPlPending containsObject:[NSValue valueWithPointer:n]]) { n = pl_t_replace(id, n); if (!n) return 0; }
    typedef void (*kind_fn)(void *, long, long);
    ((kind_fn)VA(0x1001EBAF4ULL))(n, kind, 0);       // HitObjectTaiko の種類設定(+148 / 打数 / 音の種類)
    pl_t_times(n);
    if (pl_t_long(n)) ((kind_fn)VA(0x1001EBAF4ULL))(n, kind, 0);   // 長いノートは時間から打数を求めるので、時間が決まってからもう一度
    gPlLiveDirty = YES;
    return 1;
}
static int h_set_x(int id, int x) {
    uint8_t *n = pl_note(id);
    if (!n || pl_taiko()) return 0;
    BOOL pend = [gPlPending containsObject:[NSValue valueWithPointer:n]];
    if (!pend) pl_pre(n);
    if (!pl_slide()) {
        int lanes = *((uint8_t *)gPlEd + 1700);
        if (x < 0) x = 0;
        if (x >= lanes) x = lanes - 1;
        n[129] = n[128] = (uint8_t)x;
    } else {
        unsigned w2 = (unsigned)n[129] >> 1;
        if (x < (int)w2) x = (int)w2;
        if (x > 255 - (int)w2) x = 255 - (int)w2;
        n[128] = (uint8_t)x;
    }
    gPlLiveDirty = YES;
    if (!pend) pl_post(n);
    return 1;
}
static int h_set_width(int id, int w) {
    uint8_t *n = pl_note(id);
    if (!n || !pl_slide()) return 0;
    if (w < 1) w = 1;
    if (w > 255) w = 255;
    if ([gPlPending containsObject:[NSValue valueWithPointer:n]]) { n[129] = (uint8_t)w; return 1; }
    void *sp = pl_sprite(n);
    if (!sp) { n[129] = (uint8_t)w; return 1; }
    pl_pre(n);
    width_set_one(gPlEd, sp, w);
    gPlEdits++;
    return 1;
}
static int h_del(int id) {
    uint8_t *n = pl_note(id);
    if (!n) return 0;
    NSValue *key = [NSValue valueWithPointer:n];
    if ([gPlPending containsObject:key]) {          // まだ挿入していない: 破棄するだけ
        [gPlPending removeObject:key];
        VT(n, 8, void (*)(void *))(n);
    } else {
        if (!gPlDeleted) { if (pl_taiko()) t_sel_clear(gPlEd); else { sel_snapshot_drop(); sel_clear(gPlEd); } }   // 削除するノートが選択に残らないように
        if (pl_taiko()) ((void (*)(void *, void *, int))VA(0x100141108ULL))(gPlEd, n, 0);
        else if (pl_slide()) ((void (*)(void *, void *, int, int))VA(0x1000C79F8ULL))(gPlEd, n, 0, 0);
        else ((void (*)(void *, void *, int))VA(0x1001B287CULL))(gPlEd, n, 0);
        gPlDeleted = YES;
        gPlEdits++;
    }
    CFDictionaryRemoveValue(gPlSprite, n);
    gPlNotes[id] = [NSValue valueWithPointer:NULL];
    gPlLiveDirty = YES;
    return 1;
}
static int h_add(int type) {
    pl_need();
    if (pl_taiko()) {       // Taiko: 配置処理(sub_100140F48)と同じ手順で直接生成。1 = don / 128 = 連打(種類は SetNoteFlag で変える)
        typedef void *(*new_fn)(size_t);
        static new_fn nw;
        if (!nw) nw = (new_fn)dlsym(RTLD_DEFAULT, "_Znwm");
        if (!nw || (type != 1 && type != 128)) return -1;
        uint8_t *c = nw(0x98);
        if (!c) return -1;
        memset(c, 0, 0x98);
        ((void *(*)(void *))VA(0x1001EBAA8ULL))(c);                         // HitObjectTaiko の初期化(基底 + vtable)
        ((void (*)(void *, long))VA(0x1001EBC00ULL))(c, 0);
        ((void (*)(void *, long, long))VA(0x1001EBAF4ULL))(c, type == 128 ? 4 : 0, 0);
        LHBeat cb = { 0, 0, 1 };
        BeatV bv;
        gEditor = gPlEd;
        if (cur_beat(&bv)) cb = (LHBeat){ bv.m, bv.num, bv.den };
        Rat r = { (int64_t)cb.m * cb.den + cb.num, cb.den };
        beat_write_rat(c + 64, r);
        beat_write_rat(c + 88, r);
        pl_t_times(c);
        [gPlNotes addObject:[NSValue valueWithPointer:c]];
        [gPlPending addObject:[NSValue valueWithPointer:c]];
        gPlLiveDirty = YES;
        return (int)gPlNotes.count - 1;
    }
    // 未挿入のまま保持し、Run の終了時にエディタへ挿入する
    if (pl_slide() && (type == 2048 || type == 1024 || type == 1)) {   // Slide: 読み込み処理と同じ手順で直接生成(雛形不要)
        typedef void *(*new_fn)(size_t);
        static new_fn nw;
        if (!nw) nw = (new_fn)dlsym(RTLD_DEFAULT, "_Znwm");
        if (!nw) return -1;
        uint8_t *c = nw(type == 2048 ? 0xA0 : 0x88);
        if (!c) return -1;
        memset(c, 0, type == 2048 ? 0xA0 : 0x88);
        ((void (*)(void *))VA(type == 2048 ? 0x1001EBCACULL : 0x1001EBC08ULL))(c);   // HitObjectSlide / HitObjectWipe の初期化
        *(int32_t *)(c + 112) = type;
        c[128] = 127; c[129] = (type == 2048) ? 16 : 24;
        LHBeat cb = { 0, 0, 1 };
        BeatV bv;
        gEditor = gPlEd;
        if (cur_beat(&bv)) cb = (LHBeat){ bv.m, bv.num, bv.den };
        Rat r = { (int64_t)cb.m * cb.den + cb.num, cb.den };
        beat_write_rat(c + 64, r);
        beat_write_rat(c + 88, r);
        *(int32_t *)(c + 80) = (int)((double (*)(void *))VA(0x100021B8CULL))((uint8_t *)gPlEd + 744);
        [gPlNotes addObject:[NSValue valueWithPointer:c]];
        [gPlPending addObject:[NSValue valueWithPointer:c]];
        gPlLiveDirty = YES;
        return (int)gPlNotes.count - 1;
    }
    // Key: 配置処理(sub_1001B1FBC)と同じ手順で直接生成する(雛形不要)。hold は +112 のフラグ 0x80
    LHBeat cb = { 0, 0, 1 };
    BeatV bv;
    gEditor = gPlEd;
    if (cur_beat(&bv)) cb = (LHBeat){ bv.m, bv.num, bv.den };
    Rat r = { (int64_t)cb.m * cb.den + cb.num, cb.den };
    uint8_t *c = pl_key_new(type == 128, 0, r, r);
    if (!c) return -1;
    [gPlNotes addObject:[NSValue valueWithPointer:c]];
    [gPlPending addObject:[NSValue valueWithPointer:c]];
    gPlLiveDirty = YES;
    return (int)gPlNotes.count - 1;
}
static int h_select(int id) {
    uint8_t *n = pl_note(id);
    void *sp = pl_sprite(n);
    if (!sp) return 0;
    if (pl_taiko()) { t_sel_set(gPlEd, sp, YES); return 1; }
    sel_add(gPlEd, sp);
    return 1;
}
static int h_seg_count(int id) {
    uint8_t *n = pl_note(id);
    if (!n || !pl_slide()) return 0;
    uint8_t *b = *(uint8_t **)(n + 136), *e = *(uint8_t **)(n + 144);
    return (b && e >= b) ? (int)((e - b) / 28) : 0;
}
static int h_seg_beat(int id, int i, LHBeat *o) {
    uint8_t *n = pl_note(id);
    if (!n || i < 0 || i >= h_seg_count(id)) return 0;
    uint8_t *p = *(uint8_t **)(n + 136) + 28 * i;
    o->m = *(int32_t *)p; o->num = *(int32_t *)(p + 8); o->den = *(int32_t *)(p + 12);
    if (o->den <= 0) o->den = 1;
    return 1;
}
static int h_seg_x(int id, int i) {
    uint8_t *n = pl_note(id);
    if (!n || i < 0 || i >= h_seg_count(id)) return 0;
    return seg_x16(*(uint8_t **)(n + 136) + 28 * i);
}
static void h_cur_beat(LHBeat *o) {
    BeatV b = { 0, 0, 1 };
    gEditor = gPlEd;
    if (!cur_beat(&b)) { b = (BeatV){ 0, 0, 1 }; }
    *o = (LHBeat){ b.m, b.num, b.den };
}
static double h_cur_time(void) { return ((double (*)(void *))VA(0x100021B8CULL))((uint8_t *)gPlEd + 744); }
static int h_cur_div(void) { return cur_div(gPlEd); }
// ---- 開発者向けのログ: print / エラー(ファイル名:行番号つき)/ 未対応 API の呼び出し / ShowMessage を控える。
// 「その他 > Plugin Dev」で見る・コピーする・Lua を 1 行実行する。Documents/plugin_log.txt にも書く(ファイルアプリで開ける)
static NSMutableArray<NSString *> *gPlLogBuf;
static void pl_log_add(NSString *line) {
    if (!line.length) return;
    if (!gPlLogBuf) gPlLogBuf = [NSMutableArray array];
    NSString *name = gPlCurPath.lastPathComponent ?: @"-";
    NSString *body = [NSString stringWithFormat:@"[%@] %@", name, line];
    if ([gPlLogBuf.lastObject hasSuffix:body]) return;                    // 連続する同じ行は 1 回だけ
    static NSDateFormatter *df;
    if (!df) { df = [NSDateFormatter new]; df.dateFormat = @"HH:mm:ss"; }
    NSString *full = [NSString stringWithFormat:@"%@ %@", [df stringFromDate:[NSDate date]], body];
    [gPlLogBuf addObject:full];
    if (gPlLogBuf.count > 400) [gPlLogBuf removeObjectsInRange:NSMakeRange(0, gPlLogBuf.count - 400)];
    static NSString *path;
    if (!path) path = [[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject ?: @"" stringByAppendingPathComponent:@"plugin_log.txt"] copy];
    @try {
        NSFileManager *fm = [NSFileManager defaultManager];
        if (![fm fileExistsAtPath:path]) [fm createFileAtPath:path contents:nil attributes:nil];
        else if ([[fm attributesOfItemAtPath:path error:nil] fileSize] > 512 * 1024) [@"" writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:nil];
        NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
        [fh seekToEndOfFile];
        [fh writeData:[[full stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding]];
        [fh closeFile];
    } @catch (NSException *ex) {}
}
static void h_toast(const char *m) {      // 同じ文面の連発(OnDragMove ごとの ShowMessage など)は 1 秒間は出さない
    static NSString *last; static CFAbsoluteTime at;
    NSString *t = [NSString stringWithUTF8String:m] ?: @"";
    pl_log_add([@"msg: " stringByAppendingString:t]);
    
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (last && [last isEqualToString:t] && now - at < 1.0) return;
    last = t; at = now;
    ed_toast_text(gPlEd, m, 4);
}
static void tip_set(const char *t);
static void h_tip(const char *t) { tip_set(t); }
static const char *h_chart_info(const char *key) {
    uint8_t *outer = *(uint8_t **)((uint8_t *)gPlEd + 728);
    if (!outer) return "";
    uint8_t *chart = outer + 8;
    NSString *k = [NSString stringWithUTF8String:key];
    gPlInfoBuf[0] = 0;
    uint8_t *s = NULL;
    if ([k isEqualToString:@"version"]) s = *(uint8_t **)(chart + 280);
    else if ([k isEqualToString:@"creator"]) s = *(uint8_t **)(chart + 312);
    else if ([k isEqualToString:@"title"] || [k isEqualToString:@"artist"]) {
        uint8_t *song = *(uint8_t **)(chart + 408);
        if (song) s = *(uint8_t **)(song + ([k isEqualToString:@"title"] ? 160 : 168));
    } else if ([k isEqualToString:@"key"]) { snprintf(gPlInfoBuf, sizeof gPlInfoBuf, "%d", *(int *)(chart + 372)); return gPlInfoBuf; }
    else if ([k isEqualToString:@"bpm"]) {
        deq *q = BPMQ();
        if (deq_sane(q, 48) && q->size) {
            double bl = *(double *)(deq_at(q, 0) + 40);
            if (bl > 0) snprintf(gPlInfoBuf, sizeof gPlInfoBuf, "%g", 60000.0 / bl);
        }
        return gPlInfoBuf;
    }
    if (s && (uintptr_t)s > 0x100000) snprintf(gPlInfoBuf, sizeof gPlInfoBuf, "%s", stdstr_c(s));
    return gPlInfoBuf;
}
static int h_bpm_count(void) { deq *q = BPMQ(); return (gOuter && deq_sane(q, 48)) ? (int)q->size : 0; }
static int h_bpm_at(int i, LHBeat *b, double *bpm, double *delay) {
    deq *q = BPMQ();
    if (!gOuter || !deq_sane(q, 48) || i < 0 || i >= (int)q->size) return 0;
    uint8_t *e = deq_at(q, i);
    b->m = *(int32_t *)(e + 8); b->num = *(int32_t *)(e + 16); b->den = *(int32_t *)(e + 20);
    double bl = *(double *)(e + 40);
    *bpm = bl > 0 ? 60000.0 / bl : 0;
    *delay = *(double *)(e + 32);
    return 1;
}
static int h_fx_count(void) { pl_need(); return (int)(gPlFxData.length / sizeof(PlFx)); }
static int h_fx_at(int i, LHBeat *b, const char **type, double *v) {
    if (i < 0 || i >= h_fx_count()) return 0;
    const PlFx *f = (const PlFx *)gPlFxData.bytes + i;
    *b = f->b; *type = f->type; *v = f->v;
    return 1;
}
static int16_t seg_x16(const uint8_t *s) { return *(const int16_t *)(s + 24); }
static void pl_seg_touch(uint8_t *n) {
    if (!gPlSegDirty) gPlSegDirty = [NSMutableSet set];
    if (![gPlPending containsObject:[NSValue valueWithPointer:n]]) [gPlSegDirty addObject:[NSValue valueWithPointer:n]];
}
static int h_seg_add(int id, LHBeat rel) {
    uint8_t *n = pl_note(id);
    if (!n || !pl_slide() || rel.den <= 0) return 0;
    uint8_t seg[28];
    memset(seg, 0, sizeof seg);
    ((void (*)(void *))VA(0x1001EB3C8ULL))(seg);                 // 節の初期化(読み込み処理と同じ)
    Rat r = { (int64_t)rel.m * rel.den + rel.num, rel.den };
    beat_write_rat(seg, r);
    *(int16_t *)(seg + 24) = 0;
    uint8_t **vb = (uint8_t **)(n + 136), **ve = (uint8_t **)(n + 144), **vc = (uint8_t **)(n + 152);
    if (*ve >= *vc) ((void (*)(void *, void *, float))VA(0x100066080ULL))(n + 136, seg, 0.0f);   // 領域拡張つき追加
    else { memcpy(*ve, seg, 28); *ve += 28; }
    (void)vb;
    ((void (*)(void *, void *))VA(0x1001EB3E4ULL))(n + 88, seg);   // 終端の更新(読み込み処理と同じ)
    pl_seg_touch(n);
    return 1;
}
static int h_seg_set_x(int id, int i, int x) {
    uint8_t *n = pl_note(id);
    if (!n || i < 0 || i >= h_seg_count(id)) return 0;
    *(int16_t *)(*(uint8_t **)(n + 136) + 28 * i + 24) = (int16_t)x;
    pl_seg_touch(n);
    return 1;
}
static int h_seg_clear(int id) {
    uint8_t *n = pl_note(id);
    if (!n || !pl_slide()) return 0;
    *(uint8_t **)(n + 144) = *(uint8_t **)(n + 136);
    beat_write_rat(n + 88, rat_of(n + 64));                          // 終端 = 頭
    pl_seg_touch(n);
    return 1;
}
static void pl_rebuild_dirty(void) {     // 節を変えたノートのスプライトを作り直す(標準の削除処理の前半 + 生成処理)
    void *ed = gPlEd;
    uint8_t *e = (uint8_t *)ed;
    for (NSValue *v in gPlSegDirty) {
        uint8_t *n = (uint8_t *)v.pointerValue;
        void **vb = *(void ***)(e + 1536), **ve = *(void ***)(e + 1544);
        BOOL wasSel = NO;
        for (void **p = vb; p && p != ve; p++) {
            if (VT(*p, 704, void *(*)(void *))(*p) != n) continue;
            if (*p == *(void **)(e + 1584)) *(void **)(e + 1584) = NULL;
            {   // 選択リスト(+1560/+1568)に古いスプライトを残さない(解放後に標準の選択解除が触ってクラッシュする)
                void **sv = sel_vec(ed);
                for (void **q = (void **)sv[0]; q != (void **)sv[1]; q++) {
                    if (*q != *p) continue;
                    memmove(q, q + 1, (size_t)((uint8_t *)sv[1] - (uint8_t *)(q + 1)));
                    sv[1] = (uint8_t *)sv[1] - 8;
                    wasSel = YES;
                    break;
                }
            }
            VT(*(void **)(e + 1472), 616, void (*)(void *, void *, int))(*(void **)(e + 1472), *p, 1);
            memmove(p, p + 1, (size_t)((uint8_t *)ve - (uint8_t *)(p + 1)));
            *(void ***)(e + 1544) = ve - 1;
            break;
        }
        uint8_t *fl = n;
        if (fl[113] & 8) ((void *(*)(void *, void *))VA(0x1000C88C8ULL))(ed, n);
        else ((void (*)(void *, void *))VA(0x1000C8A68ULL))(ed, n);
        void *sp = sprite_for_note(ed, n);
        if (sp && gPlSprite) CFDictionarySetValue(gPlSprite, n, sp);
        if (sp) VT(ed, 1664, void (*)(void *, void *))(ed, n);
        if (sp && wasSel) sel_add(ed, sp);         // 選択中だったノートは作り直した後も選択したままにする
        gPlEdits++;
    }
    os_log(L, "plugin seg rebuild: %lu notes", (unsigned long)gPlSegDirty.count);
    [gPlSegDirty removeAllObjects];
}
// プラグイン間の共有データ(NSUserDefaults)と譜面フォルダのファイル
static const char *h_data_get(const char *key, const char *psw) {
    NSDictionary *d = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"bm.plugin.data"];
    NSString *v = d[[NSString stringWithFormat:@"%s|%s", psw, key]];
    if (!v) return NULL;
    snprintf(gPlInfoBuf, sizeof gPlInfoBuf, "%s", v.UTF8String);
    return gPlInfoBuf;
}
static void h_data_set(const char *key, const char *psw, const char *val) {
    NSMutableDictionary *d = [[NSUserDefaults.standardUserDefaults dictionaryForKey:@"bm.plugin.data"] mutableCopy] ?: [NSMutableDictionary dictionary];
    d[[NSString stringWithFormat:@"%s|%s", psw, key]] = [NSString stringWithUTF8String:val] ?: @"";
    [NSUserDefaults.standardUserDefaults setObject:d forKey:@"bm.plugin.data"];
}
static NSString *pl_chart_dir(void) {
    uint8_t *outer = *(uint8_t **)((uint8_t *)gPlEd + 728);
    NSString *mc = outer ? spec_find_chart_path(outer, 0x800) : nil;
    return mc ? mc.stringByDeletingLastPathComponent : nil;
}
static NSData *gPlFileBuf;
static const char *h_file_read(const char *name, size_t *len) {
    NSString *dir = pl_chart_dir();
    if (!dir || strstr(name, "..")) return NULL;
    gPlFileBuf = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:[NSString stringWithUTF8String:name]]];
    if (!gPlFileBuf) return NULL;
    *len = gPlFileBuf.length;
    return gPlFileBuf.bytes;
}
static int h_file_write(const char *name, const char *data, size_t len) {
    NSString *dir = pl_chart_dir();
    if (!dir || strstr(name, "..") || name[0] == '/') return 0;
    return [[NSData dataWithBytes:data length:len] writeToFile:[dir stringByAppendingPathComponent:[NSString stringWithUTF8String:name]] atomically:YES];
}
static float gPlClick[2];                         // 現在のイベントのタッチ位置(エディタ空間)
static BOOL taiko_snap_beat_div(void *a1, const float *pos, int div, double *beat);
static int h_click_x(void) {
    if (pl_taiko()) return 0;                 // Taiko の帯は 1 レーン
    uint8_t *e = (uint8_t *)gPlEd;
    const Lay *l = lay_of(gPlEd);
    void *cont = *(void **)(e + 1472);
    float cx = VT(cont, 192, float (*)(void *))(cont);
    float px = gPlClick[0];
    if (!l->slide) {
        int cnt = e[1700]; if (cnt < 1) cnt = 1;
        float first = cx + LF(gPlEd, 1632);
        if (px < first) return -1;
        for (int i = 0; i < cnt; i++) {
            float lx = cx + LF(gPlEd, 1632 + 4 * i);
            if (px >= lx && px < lx + LF(gPlEd, 1672)) return i;
        }
        return cnt;
    }
    float xw = LF(gPlEd, 1696);
    if (xw <= 0) return 0;
    int u = (int)lroundf((px - cx) / xw * 255.0f);
    return u < 0 ? 0 : (u > 255 ? 255 : u);
}
static void h_click_beat(LHBeat *o, int fine) {
    if (pl_taiko()) {                         // Taiko: 押した位置のコンテナ座標 x → 最寄りの分割線
        int d = fine ? 32 : cur_div(gPlEd);
        if (d < 1) d = 1;
        double b = 0;
        if (!taiko_snap_beat_div(gPlEd, gPlClick, d, &b)) b = 0;
        long q = lround(b * d);
        if (q < 0) q = 0;
        *o = (LHBeat){ (int)(q / d), (int)(q % d), d };
        return;
    }
    const Lay *l = lay_of(gPlEd);
    void *cont = *(void **)((uint8_t *)gPlEd + 1472);
    float cy = VT(cont, 208, float (*)(void *))(cont);
    float K = LF(gPlEd, l->zoom) * LF(gPlEd, l->base);
    double beat = K != 0 ? (gPlClick[1] - cy) / K : 0;
    int den = fine ? 32 : cur_div(gPlEd);
    if (den < 1) den = 1;
    // 標準のタップ配置(sub_1001B1D9C)と同じく、分割幅の切り捨て(指の下のマスの下側の線)。GetClickBeatFree(1/32)は最寄りの線
    // 最寄りの線へ丸める(Giza 系のつまみ判定 |クリック − つまみ| ≤ 1/8 拍 は、分割幅 1/4 の切り捨てだと指がつまみの上でも外れる)
    long q = lround(beat * den);
    if (q < 0) q = 0;
    *o = (LHBeat){ (int)(q / den), (int)(q % den), den };
}
// ---- effect / BPM の編集 ----
static BOOL gPlFxDirty;
static NSMutableArray<NSNumber *> *gPlShortcuts;
static int pl_fx_kind(const char *t, int *flag, int *val) {
    if (!strcmp(t, "sv") || !strcmp(t, "scroll")) { *flag = 32; *val = 40; return 1; }
    if (!strcmp(t, "hs")) { *flag = 48; *val = 56; return 1; }
    if (!strcmp(t, "jump")) { *flag = 88; *val = 96; return 1; }
    if (!strcmp(t, "sign")) { *flag = 64; *val = 72; return 1; }
    return 0;
}
static BOOL pl_fx_any(const uint8_t *e) { return !e[32] || !e[48] || !e[64] || !e[80] || !e[88] || !e[104]; }
static void h_fx_add(LHBeat b, const char *type, double v) {
    int flag, val;
    if (!pl_fx_kind(type, &flag, &val) || b.den <= 0 || b.m < 0) return;
    if (!gPlFxDirty) { if (!work_load()) return; gPlFxDirty = YES; }
    double key = b.m + (double)b.num / b.den;
    for (size_t i = 0; i < eff_count(); i++) {
        uint8_t *e = eff_at(i);
        if (fabs(beat_key(e) - key) < 1e-9) { e[flag] = 0; *(double *)(e + val) = v; gPlEdits++; return; }
    }
    NSMutableData *d = [NSMutableData dataWithLength:EFF_SZ];
    uint8_t *tmp = d.mutableBytes;
    eff_init(tmp);
    beat_set(tmp, (BeatV){ b.m, b.num, b.den });
    tmp[flag] = 0; *(double *)(tmp + val) = v;
    eff_insert(tmp);
    gPlEdits++;
}
static void h_fx_del(LHBeat b, const char *type) {
    int flag, val;
    if (!pl_fx_kind(type, &flag, &val) || b.den <= 0) return;
    if (!gPlFxDirty) { if (!work_load()) return; gPlFxDirty = YES; }
    double key = b.m + (double)b.num / b.den;
    for (size_t i = 0; i < eff_count(); i++) {
        uint8_t *e = eff_at(i);
        if (fabs(beat_key(e) - key) >= 1e-9) continue;
        e[flag] = 1;
        if (!pl_fx_any(e)) eff_remove(i);
        gPlEdits++;
        return;
    }
}
static BOOL pl_same_beat(const uint8_t *e48, LHBeat b) {
    int m = *(int32_t *)(e48 + 8), num = *(int32_t *)(e48 + 16), den = *(int32_t *)(e48 + 20);
    if (den <= 0 || b.den <= 0) return NO;
    return fabs((m + (double)num / den) - (b.m + (double)b.num / b.den)) < 1e-9;
}
static void h_bpm_add(LHBeat b, double bpm, double delay) {
    if (bpm <= 0 || bpm > 100000 || b.den <= 0 || b.m < 0) return;
    NSMutableArray<NSData *> *list = [[un_bpm_read() mutableCopy] ?: [NSMutableArray array] mutableCopy];
    if (!list.count) return;
    BOOL done = NO;
    for (NSUInteger i = 0; i < list.count; i++) {
        NSMutableData *d = [list[i] mutableCopy];
        if (!pl_same_beat(d.bytes, b)) continue;
        *(double *)((uint8_t *)d.mutableBytes + 40) = 60000.0 / bpm;
        *(double *)((uint8_t *)d.mutableBytes + 32) = delay;
        list[i] = d; done = YES; break;
    }
    if (!done) {
        NSMutableData *d = [list.firstObject mutableCopy];
        uint8_t *p = d.mutableBytes;
        Rat r = { (int64_t)b.m * b.den + b.num, b.den };
        beat_write_rat(p + 8, r);
        *(double *)(p + 40) = 60000.0 / bpm;
        *(double *)(p + 32) = delay;
        [list addObject:d];
    }
    un_bpm_write(gPlEd, list);
    gPlEdits++;
}
static void h_bpm_del(LHBeat b) {
    NSArray<NSData *> *cur = un_bpm_read();
    if (cur.count < 2) return;
    NSMutableArray *list = [NSMutableArray array];
    for (NSData *d in cur) if (!pl_same_beat(d.bytes, b)) [list addObject:d];
    if (list.count == cur.count || !list.count) return;
    un_bpm_write(gPlEd, list);
    gPlEdits++;
}
static void h_shortcut(int id) {
    if (!gPlShortcuts) gPlShortcuts = [NSMutableArray array];
    [gPlShortcuts addObject:@(id)];
}
static void pl_run_shortcuts(void *ed) {      // セッションの後始末のあとに、キューした CallShortcut を順に実行する
    NSArray *q = gPlShortcuts;
    gPlShortcuts = nil;
    for (NSNumber *n in q) {
        switch (n.intValue) {
            case 7:  speed_set(ed, speed_pct(ed) + 5); break;
            case 8:  speed_set(ed, speed_pct(ed) - 5); break;
            case 10: div_step(ed, +1); break;
            case 11: div_step(ed, -1); break;
            case 12: clip_copy(ed); break;
            case 13: clip_paste(ed, NO); break;
            case 16: sel_select_all(ed); break;
            case 18: un_do(ed, YES); break;
            case 19: un_do(ed, NO); break;
            default: os_log(L, "plugin: shortcut %d unsupported", n.intValue);
        }
    }
}
static void h_seek_beat(LHBeat b) {
    if (b.den <= 0 || b.m < 0) return;
    *(double *)((uint8_t *)gPlEd + 872) = b.m + (double)b.num / b.den;
    *((uint8_t *)gPlEd + 883) = 1;
}
static void h_seek_ms(double ms) {
    double lo = 0, hi = 200000;
    uint8_t e[40] = {0};
    for (int i = 0; i < 60; i++) {
        double mid = (lo + hi) / 2;
        int m = (int)floor(mid);
        beat_set(e, (BeatV){ m, (int)lround((mid - m) * 960), 960 });
        if (time_of(e) < ms) lo = mid; else hi = mid;
    }
    *(double *)((uint8_t *)gPlEd + 872) = lo;
    *((uint8_t *)gPlEd + 883) = 1;
}
static void h_batch(int begin) { (void)begin; }   // 変更ごとに個別の Undo(まとめは未対応)

// 未挿入のノートをエディタへ挿入する(元の貼り付けと同じ手順)
// ---- print / ReadChartCustom / WriteChartCustom / DoRequest ----
static void h_log(const char *t) { os_log(L, "plugin: %{public}s", t); pl_log_add([NSString stringWithUTF8String:t] ?: @""); }
static NSString *gPlCustomVal;
static NSString *pl_custom_path(void) {
    NSString *dir = pl_chart_dir();
    return dir ? [dir stringByAppendingPathComponent:@"bm_chart_custom.json"] : nil;
}
static const char *h_custom_get(const char *name) {
    NSString *p = pl_custom_path();
    NSData *d = p ? [NSData dataWithContentsOfFile:p] : nil;
    id o = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    id v = [o isKindOfClass:NSDictionary.class] ? o[[NSString stringWithUTF8String:name]] : nil;
    gPlCustomVal = [v isKindOfClass:NSString.class] ? v : nil;
    return gPlCustomVal.UTF8String;
}
static void h_custom_set(const char *name, const char *val) {
    NSString *p = pl_custom_path();
    if (!p) return;
    NSData *d = [NSData dataWithContentsOfFile:p];
    id o = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    NSMutableDictionary *m = [o isKindOfClass:NSDictionary.class] ? [o mutableCopy] : [NSMutableDictionary dictionary];
    m[[NSString stringWithUTF8String:name]] = [NSString stringWithUTF8String:val] ?: @"";
    NSData *out = [NSJSONSerialization dataWithJSONObject:m options:0 error:nil];
    if (out) [out writeToFile:p atomically:YES];
}
static NSMutableSet<NSValue *> *gPlLiveSess;     // 有効なセッション(DoRequest の完了時に生存確認する)
static void pl_sess_add(LHSession *s) { if (!gPlLiveSess) gPlLiveSess = [NSMutableSet set]; if (s) [gPlLiveSess addObject:[NSValue valueWithPointer:s]]; }
static void pl_sess_close(LHSession *s) { if (!s) return; [gPlLiveSess removeObject:[NSValue valueWithPointer:s]]; lh_close(s); }
static void pl_session_begin(void *ed);
static void pl_session_end(void *ed, int rc, const char *err, BOOL quiet);
static void pl_request_done(LHSession *s, NSString *tag, NSString *content);
static LHSession *gPlPickSess; static int gPlPickId;
static void h_pick_bytes(void *sess, int id, const char *prompt) {
    (void)prompt;
    gPlPickSess = sess; gPlPickId = id;
    info_pick(gPlEd, 102);           // ファイル選択シート(結果は BMPickDelegate → pl_bytes_done)
}
static void h_request(void *sess, const char *tag, const char *url, const char *method, const char *data) {
    NSString *tg = [NSString stringWithUTF8String:tag] ?: @"";
    NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url] ?: @""];
    NSString *scheme = u.scheme.lowercaseString;
    LHSession *s = sess;
    if (!u || !([scheme isEqualToString:@"https"] || [scheme isEqualToString:@"http"])) {
        dispatch_async(dispatch_get_main_queue(), ^{ pl_request_done(s, tg, @"{\"code\":-1,\"message\":\"invalid url\"}"); });
        return;
    }
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:u];
    rq.timeoutInterval = 15;
    NSString *m = [[NSString stringWithUTF8String:method] uppercaseString];
    NSString *body = [NSString stringWithUTF8String:data] ?: @"";
    if ([m isEqualToString:@"POST"]) {
        rq.HTTPMethod = @"POST";
        rq.HTTPBody = [body dataUsingEncoding:NSUTF8StringEncoding];
        BOOL json = [body hasPrefix:@"{"] || [body hasPrefix:@"["];
        [rq setValue:json ? @"application/json" : @"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"];
    } else if (body.length) {
        NSString *sep = u.query ? @"&" : @"?";
        rq.URL = [NSURL URLWithString:[u.absoluteString stringByAppendingFormat:@"%@%@", sep, body]] ?: u;
    }
    if (gPl.editor) {
        char b[120]; snprintf(b, sizeof b, "Plugin request: %.80s", u.host.UTF8String ?: "?");
        ed_toast_text(gPl.editor, b, 4);
    }
    os_log(L, "plugin request: %{public}@ %{public}@", m.length ? m : @"GET", u.host);
    [[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        NSString *out;
        if (e) out = [NSString stringWithFormat:@"{\"code\":%ld,\"message\":%@}", (long)e.code,
                      [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:@[e.localizedDescription ?: @""] options:0 error:nil] encoding:NSUTF8StringEncoding] ?: @"\"\""];
        else {
            NSInteger st = [r isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)r).statusCode : 200;
            NSString *txt = d ? [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] : @"";
            out = (st >= 200 && st < 300) ? (txt ?: @"") : [NSString stringWithFormat:@"{\"code\":%ld,\"message\":\"HTTP %ld\"}", (long)st, (long)st];
        }
        if (out.length > 4 * 1024 * 1024) out = [out substringToIndex:4 * 1024 * 1024];
        dispatch_async(dispatch_get_main_queue(), ^{ pl_request_done(s, tg, out); });
    }] resume];
}
static void pl_commit_pending(void) {
    void *ed = gPlEd;
    uint8_t *e = (uint8_t *)ed;
    BOOL slide = pl_slide();
    void *chart = *(void **)(e + (slide ? 1592 : 1624));
    void *lay = *(void **)(e + 1112), *snd = *(void **)(e + 1080);
    int added = 0;
    for (NSValue *v in gPlPending) {
        uint8_t *n = (uint8_t *)v.pointerValue;
        if (pl_taiko()) {     // 標準の貼り付け(sub_100142EF8)と同じ手順: 譜面へ追加 → スプライト → 音 → 追加の完了処理
            pl_t_times(n);
            ((void (*)(void *, void *))VA(0x1001C6834ULL))(*(void **)(e + 2064), n);
            ((void (*)(void *, void *))VA(0x100142950ULL))(ed, n);
            ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, *(int32_t *)(n + 80));
            ((void (*)(void *))VA(0x10006C288ULL))(snd);
            VT(ed, 1648, void (*)(void *, void *))(ed, n);
            added++;
            gPlEdits++;
            continue;
        }
        BOOL path = slide && (n[113] & 8);
        if (path && *(uint8_t **)(n + 136) == *(uint8_t **)(n + 144)) { VT(n, 8, void (*)(void *))(n); continue; }
        if (!path) {
            double beat = rat_dbl(rat_of(n + 64));
            int lane = slide ? n[128] : n[129];
            BOOL hit = slide ? ((int (*)(void *, long, double))VA(0x1001C8F60ULL))(chart, lane, beat)
                             : VT(chart, 40, int (*)(void *, long, double))(chart, lane, beat);
            if (hit) { VT(n, 8, void (*)(void *))(n); continue; }   // 同じ位置に既にある
        }
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
        added++;
        gPlEdits++;
    }
    [gPlPending removeAllObjects];
    if (added) os_log(L, "plugins: inserted %d notes", added);
}

// ---- GetUserInput: プラグインパネルの入力欄(ゲーム標準 Textbox)で受け、確定でコールバックを呼ぶ ----
static NSString *gPlKbPreset;               // 入力欄へ最初に入れる文字列(数値入力で「Keyboard...」を選んだとき)
static NSMutableArray<NSDictionary *> *gPlInQ;   // {s: NSValue(LHSession*), id, t(title), d(default)}
static LHSession *gPlRunSess;                    // 入力待ちのため保持している Run 型の状態
static void pl_input_show(void);
static void h_input(void *sess, int id, const char *title, const char *def) {
    if (!gPlInQ) gPlInQ = [NSMutableArray array];
    NSString *t = [NSString stringWithUTF8String:title ?: ""] ?: @"";
    NSString *d = [NSString stringWithUTF8String:def ?: ""] ?: @"";
    [gPlInQ addObject:@{ @"s": [NSValue valueWithPointer:sess], @"id": @(id), @"t": t, @"d": d, @"k": @0 }];
    dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
}
static void h_choice(void *sess, int id, int kind, const char *title, const char *const *os, int n) {
    if (!gPlInQ) gPlInQ = [NSMutableArray array];
    NSMutableArray *opts = [NSMutableArray array];
    for (int i = 0; i < n; i++) if (os[i] && os[i][0]) [opts addObject:[NSString stringWithUTF8String:os[i]] ?: @""];
    if (!opts.count) [opts addObject:@"OK"];
    [gPlInQ addObject:@{ @"s": [NSValue valueWithPointer:sess], @"id": @(id), @"k": @(kind), @"o": opts,
                         @"t": [NSString stringWithUTF8String:title ?: ""] ?: @"", @"d": @"" }];
    dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
}
static void bm_slider_dialog(void *ed, NSArray<NSDictionary *> *rows, void (^done)(NSArray<NSNumber *> *), void (^cancel)(void));
static void h_slider(void *sess, int id, int n, const char *const *titles, const double *mins, const double *maxs, const double *defs, const double *steps) {
    if (!gPlInQ) gPlInQ = [NSMutableArray array];
    NSMutableArray *rows = [NSMutableArray array];
    for (int i = 0; i < n && i < 2; i++)
        [rows addObject:@{ @"t": [NSString stringWithUTF8String:titles[i] ?: ""] ?: @"", @"min": @(mins[i]), @"max": @(maxs[i]), @"val": @(defs[i]), @"step": @(steps[i]) }];
    [gPlInQ addObject:@{ @"s": [NSValue valueWithPointer:sess], @"id": @(id), @"k": @3, @"rows": rows,
                         @"t": rows.count ? rows[0][@"t"] : @"", @"d": @"" }];
    dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
}
// プラグインを介さない文字入力(パネルの入力欄を使う)。done は確定のときだけ呼ばれる(取消は呼ばれない)
static void bm_input_dialog(void *ed, NSString *title, NSString *def, void (^done)(NSString *)) {
    if (!gPlInQ) gPlInQ = [NSMutableArray array];
    static int seq;
    [gPlInQ addObject:@{ @"s": [NSValue valueWithPointer:NULL], @"id": @(900000 + (seq++)), @"k": @0, @"t": title ?: @"", @"d": def ?: @"", @"kb": @YES, @"cb": [done copy] }];
    gEditor = ed;
    dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
}
static void pl_native_drop(void) {   // パネルが閉じられたとき、確定されなかった自前の要求を捨てる
    for (NSInteger i = (NSInteger)gPlInQ.count - 1; i >= 0; i--) if (gPlInQ[(NSUInteger)i][@"cb"]) [gPlInQ removeObjectAtIndex:(NSUInteger)i];
}
static void pl_input_drop(LHSession *s) {
    for (NSInteger i = (NSInteger)gPlInQ.count - 1; i >= 0; i--)
        if ([gPlInQ[(NSUInteger)i][@"s"] pointerValue] == s) [gPlInQ removeObjectAtIndex:(NSUInteger)i];
}
static void pl_tb_end(void) {   // 入力中の Textbox を確定して編集状態を終える(残っていると次の tb_set が表示に出ず、打鍵が追記される)
    if (gPl.inTb) ((void (*)(void *))VA(0x10006D2E0ULL))(gPl.inTb);
}
static void pl_input_fill(BOOL keepText) {
    if (!gPl.inLbl) return;
    NSDictionary *head = gPlInQ.count ? gPlInQ[0] : nil;
    int kind = [head[@"k"] intValue];
    if (gPl.inBox) VT(gPl.inBox, 336, void (*)(void *, int))(gPl.inBox, kind == 0);          // 入力欄 / OK は文字入力のときだけ
    if (gPl.okBtn) VT(gPl.okBtn, 336, void (*)(void *, int))(gPl.okBtn, kind == 0);
    NSArray *opts = head[@"o"];
    for (int i = 0; i < 4; i++) {
        BOOL on = kind != 0 && i < (int)opts.count;
        if (gPl.chBtn[i]) VT(gPl.chBtn[i], 336, void (*)(void *, int))(gPl.chBtn[i], on);
        if (on && gPl.chLbl[i]) set_label_text(gPl.chLbl[i], [opts[(NSUInteger)i] UTF8String]);
    }
    if (kind != 0) {
        NSString *t = [head[@"t"] stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
        if (t.length > 36) t = [t substringToIndex:36];
        if (gPlInQ.count > 1) t = [t stringByAppendingFormat:@" (+%d)", (int)gPlInQ.count - 1];
        set_label_text(gPl.inLbl, t.UTF8String);
        return;
    }
    if (!keepText) pl_tb_end();
    if (!gPlInQ.count) {
        set_label_text(gPl.inLbl, "Input: (none)");
        if (!keepText) tb_set(gPl.inTb, @"");
        return;
    }
    NSString *t = [gPlInQ[0][@"t"] stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
    if (t.length > 36) t = [t substringToIndex:36];
    if (gPlInQ.count > 1) t = [t stringByAppendingFormat:@" (+%d)", (int)gPlInQ.count - 1];
    set_label_text(gPl.inLbl, t.UTF8String);
    if (!keepText) { tb_set(gPl.inTb, gPlKbPreset ?: gPlInQ[0][@"d"]); gPlKbPreset = nil; }
}
static void pl_open(void *self);
static void pl_close(void);
// ---- ゲーム標準のダイアログ(DialogOptions: 曲選択の「何をしますか?」と同じ部品)で選択肢 / 数値入力を出す ----
// 一覧 UI の項目数の制限を受けず、画面の大きさ(スクロール)に任せられる。文字入力は DialogOptions に無いのでパネルの入力欄のまま。
static BOOL gPlDlgOpen;                     // ダイアログ表示中
static int gPlStepId = -1;                  // 数値入力の対象(要求 id)
static double gPlStepVal;
static BOOL gPlStepKb;                      // 「Keyboard…」を選んだ(パネルの入力欄で入力する)
static void pl_dlg_cb(void *owner, void *dlg, int id);
static void pl_str_new(uint8_t *buf48, const char *s) { ((void (*)(void *, const char *))VA(0x10007309CULL))(buf48, s); }
static void pl_str_free(uint8_t *b) {
    static void (*del)(void *);
    if (!del) del = (void (*)(void *))dlsym(RTLD_DEFAULT, "_ZdlPv");
    if ((b[23] & 0x80) && del) del(*(void **)b);
}
static void pl_dlg_open(void *scene, NSString *title, NSArray<NSString *> *items, int firstId, void (*cb)(void *, void *, int)) {
    void *dlg = ((void *(*)(void))VA(0x10001BAB0ULL))();                      // DialogOptions
    uint8_t ts[48] = {0};
    pl_str_new(ts, title.UTF8String ?: "");
    VT(dlg, 1200, void (*)(void *, void *))(dlg, ts);
    pl_str_free(ts);
    for (NSUInteger i = 0; i < items.count; i++) {
        uint8_t t[48] = {0}, e[48] = {0};
        pl_str_new(t, items[i].UTF8String ?: "");
        pl_str_new(e, "");
        ((intptr_t (*)(void *, void *, void *, long))VA(0x10017E3D8ULL))(dlg, t, e, firstId + (int)i);
        pl_str_free(t); pl_str_free(e);
    }
    ((void (*)(void *, int))VA(0x10017D34CULL))(dlg, 4);                       // 「戻る」
    VT(dlg, 680, void (*)(void *, int))(dlg, 190);
    uint8_t *f = malloc(0x20);                                                  // std::function 用の関数オブジェクト(元と同じ型)
    *(uintptr_t *)f = VA(0x10091A088ULL);
    *(uintptr_t *)(f + 8) = (uintptr_t)cb;
    *(uintptr_t *)(f + 16) = 0;
    *(uintptr_t *)(f + 24) = (uintptr_t)scene;
    uint8_t tmp[32] = {0};
    *(void **)(tmp + 24) = f;
    ((void (*)(void *, void *))VA(0x10001CBECULL))((uint8_t *)dlg + 648, tmp);
    ((void (*)(void *))(*(void ***)f)[5])(f);                                  // 一時オブジェクトの破棄(元の処理と同じ)
    VT(scene, 504, void (*)(void *, void *, long))(scene, dlg, 100000);
}
static BOOL pl_numeric(NSString *s, double *out) {
    if (!s.length) return NO;
    NSScanner *sc = [NSScanner scannerWithString:s];
    double v;
    if (![sc scanDouble:&v] || !sc.isAtEnd) return NO;
    if (out) *out = v;
    return YES;
}
static NSString *pl_numstr(double v) {
    return fabs(v - llround(v)) < 1e-9 ? [NSString stringWithFormat:@"%lld", (long long)llround(v)] : [NSString stringWithFormat:@"%g", v];
}
static void pl_dialog_show(void) {
    if (gPlDlgOpen || !gPlInQ.count) return;
    void *scene = gPl.editor ?: gEditor;
    if (!scene) return;
    NSDictionary *h = gPlInQ[0];
    int kind = [h[@"k"] intValue];
    NSString *title = h[@"t"];
    if (title.length > 60) title = [title substringToIndex:60];
    gPlDlgOpen = YES;
    if (kind == 3) {                                         // GetUserSlider: ゲーム標準の設定ダイアログと同じゲージ
        NSArray *rows = h[@"rows"];
        void *ed = scene;
        bm_slider_dialog(ed, rows,
            ^(NSArray<NSNumber *> *vals) {
                gPlDlgOpen = NO;
                if (!gPlInQ.count) return;
                NSDictionary *e = gPlInQ[0];
                [gPlInQ removeObjectAtIndex:0];
                LHSession *s = [e[@"s"] pointerValue];
                double v[2] = { 0, 0 };
                for (NSUInteger i = 0; i < vals.count && i < 2; i++) v[i] = vals[i].doubleValue;
                pl_session_begin(ed);
                char err[300] = {0};
                int rc = lh_slider_deliver(s, [e[@"id"] intValue], (int)MIN(vals.count, (NSUInteger)2), v, err, sizeof err);
                pl_session_end(ed, rc, err, NO);
                if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
                dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
            },
            ^{
                gPlDlgOpen = NO;
                if (!gPlInQ.count) return;
                NSDictionary *e = gPlInQ[0];
                [gPlInQ removeObjectAtIndex:0];
                LHSession *s = [e[@"s"] pointerValue];
                char err[64] = {0};
                lh_slider_deliver(s, [e[@"id"] intValue], 0, NULL, err, sizeof err);   // 取消: コールバックを呼ばずに破棄
                if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
                dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
            });
        return;
    }
    if (kind != 0) {
        pl_dlg_open(scene, title, h[@"o"], 300, pl_dlg_cb);
        return;
    }
    NSString *v = pl_numstr(gPlStepVal);
    NSArray *items = @[ [NSString stringWithFormat:@"OK  (%@)", v], @"+1", @"-1", @"+10", @"-10", @"+100", @"-100", @"Keyboard..." ];
    pl_dlg_open(scene, [NSString stringWithFormat:@"%@: %@", title, v], items, 300, pl_dlg_cb);
}
// 文字入力(GetUserInput): iOS 標準の入力ウィンドウ(UIAlertController + テキスト欄、標準キーボード)で受ける。Malody V と同じ作法。
// 確定 = 先頭の要求のコールバックへ文字列 / キャンセル = nil。パネルは開かない
static void pl_input_commit(void *ed, NSString *txt);
static BOOL gPlNativeOn, gPlNativeFail;     // Fail = 標準の入力ウィンドウを出せなかった(以降はパネルの入力欄)
static void pl_native_input(NSDictionary *h) {
    if (gPlNativeOn) return;
    @try {
        Class cAC = NSClassFromString(@"UIAlertController"), cAA = NSClassFromString(@"UIAlertAction");
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
        if (!cAC || !cAA || !vc) { gPlNativeFail = YES; dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); }); return; }
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        NSString *title = [h[@"t"] length] ? h[@"t"] : @"Input";
        NSString *def = gPlKbPreset ?: h[@"d"] ?: @"";
        gPlKbPreset = nil;
        id ac = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)((id)cAC, NSSelectorFromString(@"alertControllerWithTitle:message:preferredStyle:"), title, nil, 1);
        __block id field = nil;
        void (^cfg)(id) = ^(id tf) {
            field = tf;
            [tf setValue:def forKey:@"text"];
            ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setClearButtonMode:"), 1);      // 編集中だけ消去ボタン
            ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setAutocorrectionType:"), 1);   // 1 = 無効
            ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setAutocapitalizationType:"), 0);
        };
        ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addTextFieldWithConfigurationHandler:"), cfg);
        SEL mk = NSSelectorFromString(@"actionWithTitle:style:handler:");
        void (^onCancel)(id) = ^(id a) { gPlNativeOn = NO; pl_input_commit(gPl.editor ?: gEditor, nil); if (gPlInQ.count) dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); }); };
        void (^onOk)(id) = ^(id a) {
            NSString *t = [field valueForKey:@"text"] ?: @"";
            gPlNativeOn = NO;
            pl_input_commit(gPl.editor ?: gEditor, t);
            if (gPlInQ.count) dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
        };
        id aCancel = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)((id)cAA, mk, @"キャンセル", 1, onCancel);
        id aOk = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)((id)cAA, mk, @"確認", 0, onOk);
        ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addAction:"), aCancel);
        ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addAction:"), aOk);
        [ac setValue:aOk forKey:@"preferredAction"];
        gPlNativeOn = YES;
        ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), ac, YES, nil);
    } @catch (NSException *ex) {
        gPlNativeOn = NO; gPlNativeFail = YES;
        os_log(L, "plugin: native input exception %{public}@", ex.reason);
        dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
    }
}
static void pl_input_show(void) {
    if (gPlInQ.count) {                                      // 選択肢と数値入力はダイアログ
        NSDictionary *h = gPlInQ[0];
        int kind = [h[@"k"] intValue];
        if (kind == 0) {
            int hid = [h[@"id"] intValue];
            double v;
            if (hid != gPlStepId) { gPlStepId = hid; gPlStepKb = [h[@"kb"] boolValue]; gPlStepVal = 0; if (!gPlStepKb) { if (pl_numeric(h[@"d"], &v)) gPlStepVal = v; else gPlStepKb = YES; } }
            if (!gPlStepKb) { pl_dialog_show(); return; }
            if (!h[@"cb"] && !gPlNativeFail) { pl_native_input(h); return; }      // プラグインの文字入力は標準の入力ウィンドウ(BetterMalody 自身の入力はパネルのまま)
        } else { pl_dialog_show(); return; }
    }
    if (!gPl.panel || !gPl.editor || !gPl.inTb) return;
    if (!gPlInQ.count) { pl_input_fill(NO); return; }
    int vis = VT(gPl.panel, 344, int (*)(void *))(gPl.panel);
    if (!vis) pl_open(gPl.editor);                       // 開いたときに先頭の要求を表示する
    else pl_input_fill(gPlInQ.count > 1);                // 入力途中の欄は、先頭が変わったときだけ書き換える
}
static void pl_input_edit(void) {
    if (gPl.inTb) ((void (*)(void *))VA(0x10006DA24ULL))(gPl.inTb);
}

static const LHHost *pl_host(void) {
    static LHHost h;
    h = (LHHost){ 0 };
    h.mode = pl_taiko() ? 5 : (pl_slide() ? 7 : 0);
    h.set_flag = h_set_flag;
    h.order = h_order; h.order_clear = h_order_clear;
    h.sel = h_sel; h.count = h_count; h.at = h_at; h.type = h_type; h.x = h_x; h.width = h_width; h.flag = h_flag;
    h.beat = h_beat; h.time_ms = h_time; h.set_beat = h_set_beat; h.set_x = h_set_x; h.set_width = h_set_width;
    h.del = h_del; h.add = h_add; h.select = h_select; h.seg_count = h_seg_count; h.seg_beat = h_seg_beat; h.seg_x = h_seg_x;
    h.cur_beat = h_cur_beat; h.cur_time = h_cur_time; h.cur_div = h_cur_div; h.toast = h_toast; h.chart_info = h_chart_info;
    h.seg_add = h_seg_add; h.seg_set_x = h_seg_set_x; h.seg_clear = h_seg_clear; h.data_get = h_data_get; h.data_set = h_data_set;
    h.file_read = h_file_read; h.file_write = h_file_write;
    h.click_x = h_click_x; h.click_beat = h_click_beat;
    h.fx_add = h_fx_add; h.fx_del = h_fx_del; h.bpm_add = h_bpm_add; h.bpm_del = h_bpm_del; h.shortcut = h_shortcut;
    h.seek_beat = h_seek_beat; h.seek_ms = h_seek_ms; h.input = h_input; h.tip = h_tip;
    h.choice = h_choice; h.slider = h_slider; h.mod_set_parent = mod_set_parent; h.log = h_log; h.custom_get = h_custom_get; h.custom_set = h_custom_set; h.request = h_request;
    h.mod_add = mod_add; h.mod_set_num = mod_set_num; h.mod_set_str = mod_set_str; h.mod_set_beat = mod_set_beat;
    h.mod_remove = mod_remove; h.pick_bytes = h_pick_bytes; h.mod_exists = mod_exists; h.mod_get_num = mod_get_num; h.mod_set_color = mod_set_color;
    h.bpm_count = h_bpm_count; h.bpm_at = h_bpm_at; h.fx_count = h_fx_count; h.fx_at = h_fx_at; h.batch = h_batch;
    return &h;
}

static void pl_session_begin(void *ed) {
    gEditor = ed;
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    gPlEd = ed;
    gPlNotes = [NSMutableArray array];
    gPlPending = [NSMutableSet set];
    gPlEdits = 0; gPlDeleted = NO; gPlSegDirty = nil;
    gPlCollected = NO;
    gPlLiveDirty = YES;
}
static void pl_session_end(void *ed, int rc, const char *err, BOOL quiet) {
    if (gPlFxDirty) { work_commit(); gPlFxDirty = NO; }
    pl_rebuild_dirty();
    pl_commit_pending();
    if (gPlDeleted) ((void (*)(void *))VA(0x10006C288ULL))(*(void **)((uint8_t *)ed + 1080));
    if (gPlEdits) *((uint8_t *)ed + 883) = 1;
    if (gPlEdits && is_taiko(ed)) un_mark();
    if (gPreview && ed == gEditor && gPlEdits) ed_relayout(ed);
    if (rc != 0) {
        pl_log_add([NSString stringWithFormat:@"ERROR %s", err]);
        char b[140]; snprintf(b, sizeof b, "Plugin error: %.110s", err);
        ed_toast_text(ed, b, 2);
        os_log(L, "plugin error: %{public}s", err);
    } else if (!quiet) {
        char b[80]; snprintf(b, sizeof b, "Plugin done: %d edit(s)", gPlEdits);
        ed_toast_text(ed, b, 4);
    }
    gPlNotes = nil; gPlPending = nil; gPlFxData = nil; gPlEd = NULL;
    if (gPlSprite) { CFRelease(gPlSprite); gPlSprite = NULL; }
    if (gPlId) { CFRelease(gPlId); gPlId = NULL; }
    pl_run_shortcuts(ed);
}

// ---- Interact 型: 有効化中はレーン上のタッチを OnClick / OnDrag* へ転送する ----
static LHSession *gPlSess;
static void *gPlSessEd;
static NSInteger gPlSessIdx = -1;
static BOOL gPlDown, gPlDragging;
static float gPlStart[2];
static void pl_event(const char *fn) {
    void *ed = gPlSessEd;
    if (!gPlSess || !ed) return;
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_call(gPlSess, fn, NULL, 0, err, sizeof err);
    pl_session_end(ed, rc, err, YES);
}
// OnKey: 外付けキーボード(Mac / iPad)のキーを GameController 経由で受ける。キー値は ASCII / Unity の KeyCode 風(a〜z=97〜122, 0〜9=48〜57, 矢印=273〜276)
static void pl_key_event(int key, BOOL down) {
    void *ed = gPlSessEd;
    if (!gPlSess || !ed || ![gPlLiveSess containsObject:[NSValue valueWithPointer:gPlSess]]) return;
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_call_key(gPlSess, key, down, err, sizeof err);
    pl_session_end(ed, rc, err, YES);
}
static BOOL kb_key_event(long code, BOOL pressed);
static int pl_hid_to_key(long c) {
    if (c >= 4 && c <= 29) return 97 + (int)(c - 4);
    if (c >= 30 && c <= 38) return 49 + (int)(c - 30);
    switch (c) { case 39: return 48; case 40: return 13; case 41: return 27; case 42: return 8; case 43: return 9; case 44: return 32;
                 case 79: return 275; case 80: return 276; case 81: return 274; case 82: return 273; }
    return -1;
}
static void pl_kb_register(void) {
    static BOOL done;
    if (done) return;
    done = YES;
    dlopen("/System/Library/Frameworks/GameController.framework/GameController", RTLD_LAZY);
    Class kc = NSClassFromString(@"GCKeyboard");
    id kb = kc ? [(id)kc valueForKey:@"coalescedKeyboard"] : nil;
    id inp = kb ? [kb valueForKey:@"keyboardInput"] : nil;
    if (!inp) { os_log(L, "plugins: OnKey: no keyboard"); done = NO; return; }
    void (^h)(id, id, long, BOOL) = ^(id input, id key, long code, BOOL pressed) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (kb_key_event(code, pressed)) return;                    // ショートカット(bm.m の kbd.inc)として処理したキーはプラグインへ渡さない
            int k = pl_hid_to_key(code);
            if (k >= 0) pl_key_event(k, pressed);
        });
    };
    [inp setValue:h forKey:@"keyChangedHandler"];
    os_log(L, "plugins: OnKey registered");
}
static void pl_deactivate(void) {
    if (gPlSess) { pl_event("OnDeactive"); pl_input_drop(gPlSess); pl_sess_close(gPlSess); }
    gPlSess = NULL; gPlSessEd = NULL; gPlSessIdx = -1; gPlDown = gPlDragging = NO;
    gPlRDirty = YES;
}
static BOOL pl_touch(void *ed, int ev, void *pos) {
    BOOL tk = is_taiko(ed);
    if (!gPlSess || ed != gPlSessEd || !(lay_of(ed) || tk) || gPreview) return NO;
    if (*((uint8_t *)ed + 903) || *((uint8_t *)ed + 901)) return NO;
    const float *pt = pos;
    if (ev == 0 && tk) {      // Taiko: 帯の中の押下だけをプラグインへ
        uint8_t *s = (uint8_t *)ed;
        if (fabsf(pt[1] - *(float *)(s + 2072)) * 2.0f >= *(float *)(s + 2080)) return NO;
        gPlDown = YES; gPlDragging = NO;
        gPlStart[0] = pt[0]; gPlStart[1] = pt[1];
        return YES;
    }
    if (ev == 0) {
        const Lay *l = lay_of(ed);
        void *cont = *(void **)((uint8_t *)ed + 1472);
        float cx = VT(cont, 192, float (*)(void *))(cont);
        float x0, x1;
        if (!l->slide) {
            int cnt = ((uint8_t *)ed)[1700]; if (cnt < 1) cnt = 1;
            x0 = cx + LF(ed, 1632); x1 = cx + LF(ed, 1632 + 4 * (cnt - 1)) + LF(ed, 1672);
        } else { x0 = cx; x1 = cx + LF(ed, 1696); }
        if (pt[0] < x0 || pt[0] > x1) return NO;
        gPlDown = YES; gPlDragging = NO;
        gPlStart[0] = pt[0]; gPlStart[1] = pt[1];
        sel_snapshot_take(ed);
        return YES;
    }
    if (!gPlDown) return NO;
    if (ev == 1) {
        if (!gPlDragging && fabsf(pt[0] - gPlStart[0]) + fabsf(pt[1] - gPlStart[1]) > 8.0f) {
            gPlDragging = YES;
            gPlClick[0] = gPlStart[0]; gPlClick[1] = gPlStart[1];
            pl_event("OnDragStart");
        }
        if (gPlDragging) { gPlClick[0] = pt[0]; gPlClick[1] = pt[1]; pl_event("OnDragMove"); }
    } else if (ev == 2) {
        gPlDown = NO;
        if (gPlDragging) { gPlClick[0] = pt[0]; gPlClick[1] = pt[1]; pl_event("OnDragEnd"); }
        else { gPlClick[0] = gPlStart[0]; gPlClick[1] = gPlStart[1]; pl_event("OnClick"); }
        gPlDragging = NO;
    } else if (ev == 6) {
        gPlDown = NO;
        if (gPlDragging) { gPlClick[0] = pt[0]; gPlClick[1] = pt[1]; pl_event("OnDragEnd"); }
        gPlDragging = NO;
    }
    return YES;
}
// ---- ゲーム内インポート: ファイル選択シート → Documents/export_plugins へコピー(.lua のみ。フォルダは直下の .lua) ----
static void pl_import_urls(NSArray<NSURL *> *urls, void *ed) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *dir = pl_dir();
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    int ok = 0, skip = 0;
    for (NSURL *u in urls) {
        BOOL scoped = [u startAccessingSecurityScopedResource];
        NSMutableArray<NSURL *> *files = [NSMutableArray array];
        BOOL isDir = NO;
        [fm fileExistsAtPath:u.path isDirectory:&isDir];
        if (isDir) {                       // フォルダ: 構成ごと export_plugins/<フォルダ名>/ へコピー(lua と素材のみ)
            NSSet *allow = [NSSet setWithObjects:@"lua", @"png", @"jpg", @"jpeg", @"gif", @"txt", @"json", @"md", @"ex", @"csv", nil];
            NSString *base = [dir stringByAppendingPathComponent:u.lastPathComponent];
            NSDirectoryEnumerator *en = [fm enumeratorAtPath:u.path];
            int cnt = 0;
            for (NSString *rel in en) {
                if (en.level > 4 || [rel.lastPathComponent hasPrefix:@"."]) { [en skipDescendants]; continue; }
                if (![allow containsObject:rel.pathExtension.lowercaseString] || cnt >= 400) continue;
                NSData *d = [NSData dataWithContentsOfFile:[u.path stringByAppendingPathComponent:rel]];
                if (!d || d.length > 16 * 1024 * 1024) { skip++; continue; }
                NSString *to = [base stringByAppendingPathComponent:rel];
                [fm createDirectoryAtPath:to.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
                if ([d writeToFile:to atomically:YES]) { ok++; cnt++; } else skip++;
            }
            if (scoped) [u stopAccessingSecurityScopedResource];
            continue;
        } else [files addObject:u];
        for (NSURL *f in files) {
            if ([f.pathExtension.lowercaseString isEqualToString:@"zip"]) {    // zip: フォルダ構成ごと export_plugins/<zip 名>/ へ展開
                NSData *zd = [NSData dataWithContentsOfURL:f];
                NSString *dst = [dir stringByAppendingPathComponent:f.lastPathComponent.stringByDeletingPathExtension];
                ZipxStats st;
                if (!zd || zd.length > 128 * 1024 * 1024 ||
                    zipx_extract(zd.bytes, zd.length, dst.UTF8String, "lua,png,jpg,jpeg,gif,txt,json,md,ex,csv", &st) != 0) { skip++; continue; }
                os_log(L, "plugin import zip: %{public}@ -> %d files (%d skipped)", f.lastPathComponent, st.files, st.skipped);
                ok += st.files; skip += st.skipped;
                continue;
            }
            if (![f.pathExtension.lowercaseString isEqualToString:@"lua"]) { skip++; continue; }
            NSData *d = [NSData dataWithContentsOfURL:f];
            if (!d || d.length > 8 * 1024 * 1024) { skip++; continue; }
            NSString *dst = [dir stringByAppendingPathComponent:f.lastPathComponent];
            if ([d writeToFile:dst atomically:YES]) ok++; else skip++;
            os_log(L, "plugin import: %{public}@ -> %{public}@", f.lastPathComponent, dst);
        }
        if (scoped) [u stopAccessingSecurityScopedResource];
    }
    if (ed) {
        char b[96];
        snprintf(b, sizeof b, skip ? "Imported %d file(s), skipped %d" : "Imported %d file(s)", ok, skip);
        ed_toast_text(ed, b, ok ? 4 : 2);
        if (gPl.panel) { pl_scan(); gPl.sel = -1; pl_refresh_list(); pl_info_text(); }
    }
}
static void pl_import_pick(void *ed) {
    gInfoEd = ed; gPickKind = 100;
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            Class pc = NSClassFromString(@"UIDocumentPickerViewController");
            if (!pc) { ed_toast_text(ed, "Import: file picker unavailable", 2); return; }
            id p = ((id (*)(id, SEL))objc_msgSend)((id)pc, @selector(alloc));
            p = ((id (*)(id, SEL, id, NSInteger))objc_msgSend)(p, NSSelectorFromString(@"initWithDocumentTypes:inMode:"), @[@"public.item"], 0);
            [p setValue:@YES forKey:@"allowsMultipleSelection"];
            if (!gPickDelegate) gPickDelegate = [BMPickDelegate new];
            [p setValue:gPickDelegate forKey:@"delegate"];
            id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
            id win = [app valueForKey:@"keyWindow"];
            id vc = [win valueForKey:@"rootViewController"];
            while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
            ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), p, YES, nil);
        } @catch (NSException *ex) {
            os_log(L, "plugin import: picker exception %{public}@", ex.reason);
            ed_toast_text(ed, "Import: file picker failed", 2);
        }
    });
}
static void pl_run(void *ed, NSUInteger idx) {
    if (idx >= gPlFiles.count) return;
    const LHInfo *in = pl_info(idx);
    gPlCurPath = gPlFiles[idx];
    if (in->err[0]) { pl_log_add([NSString stringWithFormat:@"LOAD ERROR %s", in->err]); ed_toast_text(ed, "Plugin: load error (Plugin Dev > log)", 2); os_log(L, "plugin error: %{public}s", in->err); return; }
    int emode = info_mode(ed);
    if (in->mode >= 0 && in->mode != emode) {
        char b[80]; snprintf(b, sizeof b, "Plugin: for %s mode only", pl_mode_name(in->mode));
        ed_toast_text(ed, b, 2);
        return;
    }
    if (*((uint8_t *)ed + 903)) { ed_toast_lv(ed, 2036, 2); return; }
    NSData *src = [NSData dataWithContentsOfFile:gPlFiles[idx]];
    if (!src) { ed_toast_text(ed, "Plugin: cannot read file", 2); return; }
    BOOL interact = in->type == 2 || (!in->has_run && in->has_tool);
    if (!is_taiko(ed) && !(in->type == 2 || (!in->has_run && in->has_tool)) && [src rangeOfData:[@"GetOrderNotes" dataUsingEncoding:NSUTF8StringEncoding] options:0 range:NSMakeRange(0, src.length)].location != NSNotFound) {
        // 順番に選択に対応したプラグイン: 順番が空なら 1 回目の Run=順番に選択を ON(タップ順に控える)/ もう一度 Run=その順で実行。
        // その他メニューの「Order select」で先に順番を作っておいた場合は、Run で即実行(editor.html / Slide Editor の Order と同じ)
        if (!gOrdNotes) gOrdNotes = [NSMutableArray array];
        if (!gOrdNotes.count && !(gOrdMode && gOrdEd == ed)) {
            gOrdMode = YES; gOrdEd = ed; ord_normal_reset(ed);
            ed_toast_text(ed, "Order select ON: tap slides in order, then Run again", 4);
            return;
        }
        gOrdMode = NO;
    }
    if (interact) {                      // 有効 / 無効の切り替え
        if (gPlSess && gPlSessIdx == (NSInteger)idx && gPlSessEd == ed) {
            pl_deactivate();
            ed_toast_text(ed, "Plugin: OFF", 4);
            return;
        }
        pl_deactivate();
        pl_session_begin(ed);
        char err[300] = {0};
        gPlSess = lh_open(src.bytes, src.length, gPlFiles[idx].lastPathComponent.UTF8String, pl_host(), err, sizeof err);
        if (gPlSess) pl_sess_add(gPlSess);
        int rc = gPlSess ? 0 : -1;
        if (gPlSess) { gPlSessEd = ed; gPlSessIdx = (NSInteger)idx; }
        pl_session_end(ed, rc, err, YES);
        if (gPlSess) {
            pl_event("OnActive");
            pl_kb_register();
            char b[120]; snprintf(b, sizeof b, "Plugin ON: %s (tap lanes)", in->name);
            ed_toast_text(ed, b, 4);
        }
        return;
    }
    if (gPlRunSess) { pl_input_drop(gPlRunSess); pl_sess_close(gPlRunSess); gPlRunSess = NULL; }
    pl_session_begin(ed);
    char err[300] = {0};
    LHSession *keep = NULL;
    int rc = lh_run(src.bytes, src.length, gPlFiles[idx].lastPathComponent.UTF8String, pl_host(), err, sizeof err, &keep);
    if (keep) { if (rc != 0) { pl_input_drop(keep); lh_close(keep); } else { gPlRunSess = keep; pl_sess_add(keep); } }
    pl_session_end(ed, rc, err, NO);    [gOrdNotes removeAllObjects];
    ord_marks_refresh(ed);
}
// ---- Plugin Dev(その他メニュー): ログの表示 / コピー / 消去 / Lua を 1 行実行(iOS 標準のウィンドウ)----
static void pl_dev_menu(void);
// アラートの提示: 直前のアラートが閉じ終わっていない間(Mac は閉じるアニメーションが長い)の提示は黙って無視されるので、閉じ終わるまで待ってから出す(最大 5 秒)。
// ★外すと、3 つ目以降のアラート(Add shortcut など)が出なくなる(2026-10-21 実機確認)
static void pl_present(id ac, int tries) {
    @try {
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
        if (!vc) return;
        id root = vc;
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        BOOL busy = [[vc valueForKey:@"isBeingDismissed"] boolValue] || [[vc valueForKey:@"isBeingPresented"] boolValue] || (vc != root && [vc isKindOfClass:NSClassFromString(@"UIAlertController")]);
        if (busy && tries < 33) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ pl_present(ac, tries + 1); });
            return;
        }
        ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), ac, YES, nil);
    } @catch (NSException *ex) { os_log(L, "plugin dev: present exception %{public}@", ex.reason); }
}
static id gPlAlertVC;                                   // 表示中のアラート(キーボードでも選べるようにする)
static NSMutableArray<void (^)(void)> *gPlAlertKeys;     // 項目ごとの「選んだときの処理」(閉じる + 次へ)
static void pl_alert(NSString *title, NSString *msg, NSString *fieldDef, NSArray<NSString *> *titles, NSArray<NSNumber *> *styles, void (^pick)(NSInteger idx, NSString *text)) {
    @try {
        Class cAC = NSClassFromString(@"UIAlertController"), cAA = NSClassFromString(@"UIAlertAction");
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
        if (!cAC || !cAA || !vc) return;
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        id ac = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)((id)cAC, NSSelectorFromString(@"alertControllerWithTitle:message:preferredStyle:"), title, msg, 1);
        __block id field = nil;
        if (fieldDef) {
            void (^cfg)(id) = ^(id tf) {
                field = tf;
                [tf setValue:fieldDef forKey:@"text"];
                ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setClearButtonMode:"), 1);
                ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setAutocorrectionType:"), 1);
                ((void (*)(id, SEL, NSInteger))objc_msgSend)(tf, NSSelectorFromString(@"setAutocapitalizationType:"), 0);
            };
            ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addTextFieldWithConfigurationHandler:"), cfg);
        }
        SEL mk = NSSelectorFromString(@"actionWithTitle:style:handler:");
        NSMutableArray<void (^)(void)> *keys = [NSMutableArray array];
        for (NSUInteger i = 0; i < titles.count; i++) {
            NSInteger idx = (NSInteger)i;
            void (^run)(void) = ^{
                gPlAlertVC = nil; gPlAlertKeys = nil;
                NSString *tx = field ? ([field valueForKey:@"text"] ?: @"") : nil;
                os_log(L, "plugin dev: pick %ld", (long)idx);
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ pick(idx, tx); });   // 閉じ終わってから次を出す(閉じている途中の提示は無視される)
            };
            [keys addObject:[run copy]];
            void (^h)(id) = ^(id a) { run(); };
            NSString *label = i < 9 ? [NSString stringWithFormat:@"%lu. %@", (unsigned long)i + 1, titles[i]] : (i == 9 ? [@"0. " stringByAppendingString:titles[i]] : titles[i]);   // 数字キーで選べる(Mac はアラートの項目をクリックできないことがある)
            id act = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)((id)cAA, mk, label, styles[i].integerValue, h);
            ((void (*)(id, SEL, id))objc_msgSend)(ac, NSSelectorFromString(@"addAction:"), act);
        }
        gPlAlertVC = ac; gPlAlertKeys = keys;
        pl_present(ac, 0);
    } @catch (NSException *ex) { os_log(L, "plugin dev: alert exception %{public}@", ex.reason); }
}
static NSString *gPlDevLast = @"=Editor:GetCurrentBeat().beat";
static void pl_dev_console(void) {
    pl_alert(@"Lua console", @"1 行の Lua を実行します(出力は print)。= で始めると値を表示します。", gPlDevLast, @[ @"Run", @"Cancel" ], @[ @0, @1 ], ^(NSInteger idx, NSString *text) {
        if (idx != 0 || !text.length) { pl_dev_menu(); return; }
        gPlDevLast = text;
        void *ed = gPl.editor ?: gEditor;
        if (!ed) { pl_log_add(@"console: no editor"); pl_dev_menu(); return; }
        NSString *code = [text hasPrefix:@"="] ? [NSString stringWithFormat:@"print(%@)", [text substringFromIndex:1]] : text;
        NSString *src = [NSString stringWithFormat:@"PluginName='console'\nfunction Run()\n%@\nend\n", code];
        NSData *d = [src dataUsingEncoding:NSUTF8StringEncoding];
        gPlCurPath = @"console";
        pl_log_add([@"> " stringByAppendingString:text]);
        if (gPlRunSess) { pl_input_drop(gPlRunSess); pl_sess_close(gPlRunSess); gPlRunSess = NULL; }
        pl_session_begin(ed);
        char err[300] = {0};
        LHSession *keep = NULL;
        int rc = lh_run(d.bytes, d.length, "console", pl_host(), err, sizeof err, &keep);
        if (keep) { if (rc != 0) { pl_input_drop(keep); lh_close(keep); } else { gPlRunSess = keep; pl_sess_add(keep); } }
        pl_session_end(ed, rc, err, YES);
        dispatch_async(dispatch_get_main_queue(), ^{ pl_dev_menu(); });
    });
}
static void pl_dev_menu(void) {
    NSMutableString *m = [NSMutableString string];
    NSUInteger n = gPlLogBuf.count, from = n > 14 ? n - 14 : 0;
    for (NSUInteger i = from; i < n; i++) [m appendFormat:@"%@\n", gPlLogBuf[i]];
    if (!n) [m appendString:@"(ログはまだありません)\n"];
    NSString *msg = m.length > 1800 ? [m substringFromIndex:m.length - 1800] : m;
    pl_alert(@"Plugin Dev", [msg stringByAppendingString:@"\nDocuments/plugin_log.txt にも書き出します"], nil,
             @[ @"Lua console...", @"Copy log", @"Clear log", @"Close" ], @[ @0, @0, @2, @1 ], ^(NSInteger idx, NSString *t) {
        if (idx == 0) pl_dev_console();
        else if (idx == 1) {
            @try {
                id pb = [(id)NSClassFromString(@"UIPasteboard") valueForKey:@"generalPasteboard"];
                [pb setValue:[gPlLogBuf componentsJoinedByString:@"\n"] forKey:@"string"];
                ed_toast_text(gPl.editor ?: gEditor, "Plugin log copied", 4);
            } @catch (NSException *ex) {}
        } else if (idx == 2) { [gPlLogBuf removeAllObjects]; ed_toast_text(gPl.editor ?: gEditor, "Plugin log cleared", 4); }
    });
}
static void pl_bytes_done(NSData *d) {
    LHSession *s = gPlPickSess; int id = gPlPickId;
    gPlPickSess = NULL;
    if (!s || ![gPlLiveSess containsObject:[NSValue valueWithPointer:s]]) return;
    void *ed = gPl.editor ?: gEditor;
    if (!ed) return;
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_bytes_deliver(s, id, d ? d.bytes : NULL, d ? d.length : 0, err, sizeof err);
    pl_session_end(ed, rc, err, YES);
    if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
}
// DoRequest の完了: OnResponse(tag, content) を呼ぶ(セッションが残っているときだけ)
static void pl_request_done(LHSession *s, NSString *tag, NSString *content) {
    if (!s || ![gPlLiveSess containsObject:[NSValue valueWithPointer:s]]) return;
    void *ed = gPl.editor ?: gEditor;
    if (!ed) return;
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_response(s, tag.UTF8String, content.UTF8String, err, sizeof err);
    pl_session_end(ed, rc, err, YES);
    if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
}
// 入力確定: 先頭の要求のコールバックへ文字列を渡す(txt=nil はキャンセル)。残りが無ければパネルを閉じる
static void pl_input_commit(void *ed, NSString *txt) {
    if (!gPlInQ.count) return;
    NSDictionary *e = gPlInQ[0];
    [gPlInQ removeObjectAtIndex:0];
    void (^nativeCb)(NSString *) = e[@"cb"];
    if (nativeCb) {   // BetterMalody 自身の入力要求(プラグインを介さない): パネルを閉じてからコールバックを呼ぶ
        pl_tb_end();
        gPlStepId = -1;
        pl_input_fill(NO);
        if (!gPlInQ.count) pl_close();
        if (txt) nativeCb(txt);
        return;
    }
    LHSession *s = [e[@"s"] pointerValue];
    int id = [e[@"id"] intValue];
    pl_tb_end();
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_input_deliver(s, id, txt ? txt.UTF8String : NULL, err, sizeof err);
    pl_session_end(ed, rc, err, txt == nil);
    if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
    gPlStepId = -1;
    pl_input_fill(NO);
    if (!gPlInQ.count) pl_close();
}
static void pl_input_ok(void *ed) {
    if (!gPlInQ.count) { ed_toast_text(ed, "No input requested", 4); return; }
    if ([gPlInQ[0][@"k"] intValue] != 0) return;      // 選択肢の要求には OK ではなく選択肢で答える
    pl_input_commit(ed, tb_text(gPl.inTb));
}
// 選択肢(GetUserSelect: 0 = はい / GetUserOption: 0 始まりの番号)
static void pl_choice(void *ed, int idx) {
    if (!gPlInQ.count || [gPlInQ[0][@"k"] intValue] == 0) return;
    NSDictionary *e = gPlInQ[0];
    [gPlInQ removeObjectAtIndex:0];
    LHSession *s = [e[@"s"] pointerValue];
    int kind = [e[@"k"] intValue];
    int value = kind == 1 ? (idx == 0) : idx;
    pl_session_begin(ed);
    char err[300] = {0};
    int rc = lh_choice_deliver(s, [e[@"id"] intValue], kind, value, err, sizeof err);
    pl_session_end(ed, rc, err, NO);
    if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
    pl_input_fill(NO);
    if (!gPlInQ.count) pl_close();
}

// ---- パネル(Effect パネルと同じ作り) ----
static void pl_info_text(void) {
    if (!gPl.infoLbl) return;
    NSString *t;
    if (gPl.sel >= 0 && gPl.sel < (int)gPlFiles.count) {
        const LHInfo *in = pl_info(gPl.sel);
        if (in->err[0]) t = [NSString stringWithFormat:@"Error: %.60s", in->err];
        else t = [NSString stringWithFormat:@"%s / %s / %@", pl_mode_name(in->mode), pl_type_name(in->type), gPlFiles[gPl.sel].lastPathComponent];
    } else t = @"Documents/export_plugins/*.lua";
    set_label_text(gPl.infoLbl, t.UTF8String);
}
static void pl_highlight(void) {
    for (int i = 0; i < 4096; i++) {
        void *row = ((void *(*)(void *, int))VA(0x100048F14ULL))(gPl.list, i);
        if (!row) break;
        void *bg = VT(row, 528, void *(*)(void *, int))(row, 0);
        if (bg) VT(bg, 1032, void (*)(void *, int))(bg, i == gPl.sel ? 100 : 0);
    }
}
static void pl_row_click(void *ed, void *a2, void *row) {
    (void)ed; (void)a2;
    if (!row) return;
    int idx = (int)(intptr_t)VT(row, 704, void *(*)(void *))(row) - 1;
    gPl.sel = (gPl.sel == idx) ? -1 : idx;
    pl_highlight();
    pl_info_text();
}
static void pl_refresh_list(void) {
    void *list = gPl.list, *ed = gPl.editor;
    if (!list || !ed) return;
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)((uint8_t *)ed + 672), "NormalFont_H");
    float *sz = VT(list, 328, float *(*)(void *))(list);
    float listW = sz[0];
    ((void (*)(void *))VA(0x100048BF4ULL))(list);
    int emode = info_mode(ed);
    for (NSUInteger i = 0; i < gPlFiles.count; i++) {
        const LHInfo *in = pl_info(i);
        void *row = ((void *(*)(void))VA(0x100544A64ULL))();
        float rs[2] = { listW, fontH * 2 };
        VT(row, 320, void (*)(void *, float *))(row, rs);
        float a00[2] = { 0, 0 }, ahalf[2] = { 0, 0.5f };
        VT(row, 296, void (*)(void *, float *))(row, a00);
        VT(list, 496, void (*)(void *, void *))(list, row);
        void *bg = ((void *(*)(void *))VA(0x1001F38B0ULL))((void *)VA(0x1016CC9F8ULL));
        VT(bg, 1032, void (*)(void *, int))(bg, 0);
        float scx = ((float (*)(void *, float, float))VA(0x10054471CULL))(bg, listW, fontH * 2);
        VT(bg, 1056, void (*)(void *, int, float))(bg, 0, scx);
        VT(bg, 136, void (*)(void *, void *))(bg, (void *)VA(0x1016CB970ULL));
        VT(bg, 680, void (*)(void *, int))(bg, 0);
        VT(bg, 296, void (*)(void *, float *))(bg, a00);
        VT(row, 504, void (*)(void *, void *, int))(row, bg, 1);
        void *l1 = ((void *(*)(float))VA(0x1001F3608ULL))(fontH);
        float p1[2] = { fontH * 0.6f, fontH };
        VT(l1, 136, void (*)(void *, float *))(l1, p1);
        VT(l1, 296, void (*)(void *, float *))(l1, ahalf);
        set_label_text(l1, in->name);
        VT(l1, 680, void (*)(void *, int))(l1, 1);
        VT(row, 504, void (*)(void *, void *, int))(row, l1, 2);
        void *l2 = ((void *(*)(float))VA(0x1001F3608ULL))(fontH);
        float p2[2] = { listW * 0.72f, fontH };
        VT(l2, 136, void (*)(void *, float *))(l2, p2);
        VT(l2, 296, void (*)(void *, float *))(l2, ahalf);
        BOOL usable = !in->err[0] && (in->mode < 0 || in->mode == emode);
        NSString *tag = in->err[0] ? @"error" : [NSString stringWithFormat:@"%s%s", pl_mode_name(in->mode), usable ? "" : " -"];
        if (gPlSess && gPlSessIdx == (NSInteger)i && gPlSessEd == ed) tag = @"ON";
        set_label_text(l2, tag.UTF8String);
        VT(l2, 680, void (*)(void *, int))(l2, 2);
        VT(row, 504, void (*)(void *, void *, int))(row, l2, 2);
        VT(row, 720, void (*)(void *, void *))(row, (void *)(intptr_t)(i + 1));
    }
    ((void (*)(void *, int))VA(0x100048FF8ULL))(list, 0);
    pl_highlight();
}
static void pl_register_click(void) {
    if (!gPl.list) return;
    uint8_t src[40] = {0};
    uint8_t *f = malloc(0x20);
    *(uintptr_t *)f = VA(0x1008F1BD8ULL);
    *(uintptr_t *)(f + 8) = (uintptr_t)pl_row_click;
    *(uintptr_t *)(f + 16) = 0;
    *(uintptr_t *)(f + 24) = (uintptr_t)gPl.editor;
    *(void **)(src + 0x18) = f;
    ((void (*)(void *, void *))VA(0x100034944ULL))((uint8_t *)gPl.list + 0x648, src);
    void **vt = *(void ***)f;
    ((void (*)(void *))vt[5])(f);
}
static void pl_bind(void *self, void *layout) {
    memset(&gPl, 0, sizeof gPl);
    gPl.sel = -1;
    gPl.panel = fx_find(layout, "_plpanel");
    gPl.list = cast_to(fx_find(layout, "_pllist"), 0x1008EADF0ULL);
    gPl.infoLbl = fx_find(layout, "_pltext-2");
    gPl.inLbl = fx_find(layout, "_pltext-3");
    gPl.inBox = fx_find(layout, "_plie-1");
    gPl.okBtn = fx_find(layout, "_plb-3");
    for (int i = 0; i < 4; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "_plc-%d", i + 1);   gPl.chBtn[i] = fx_find(layout, nm);
        snprintf(nm, sizeof nm, "_plcicon-%d", i + 1); gPl.chLbl[i] = fx_find(layout, nm);
    }
    gPl.inTb = cast_to(fx_find(layout, "_plit-1"), 0x1008F0B00ULL);
    gPl.editor = self;
    gPlClickRegistered = NO;
    gPlRList = cast_to(fx_find(layout, "_right-pl"), 0x1008EADF0ULL);
    gPlRBox = fx_find(layout, "_plr");
    gPlRClickReg = NO; gPlRDirty = YES; gPlROn = NO;
    gPlShrink.applied = NO;
    {
        static const char *const nm[5] = { "_edit", "_grids", "_gridbg", "_judge", "_right-3" };
        for (int i = 0; i < 5; i++) gPlShrink.n[i] = fx_find(layout, nm[i]);
    }
}
static void pl_open(void *self) {
    if (!gPl.panel || !gPl.list) { ed_toast_text(self, "Plugins: panel not available", 2); return; }
    gEditor = self; gOuter = *(void **)((uint8_t *)self + 0x2d8); gPl.editor = self;
    if (!gPlClickRegistered) { pl_register_click(); gPlClickRegistered = YES; }
    pl_scan();
    gPl.sel = -1;
    pl_refresh_list();
    pl_info_text();
    pl_input_fill(NO);
    int vis = VT(gPl.panel, 344, int (*)(void *))(gPl.panel);
    if (!vis) {
        void *act = ((void *(*)(void *, int))VA(0x100547054ULL))(gPl.panel, 1);
        float from[2] = { LF(self, 664) * 0.5f, 0.0f };
        int64_t to = *(int64_t *)VA(0x1016CB970ULL);
        ((void *(*)(void *, int, float *, void *, int64_t))VA(0x1005921E8ULL))(act, 200, from, &to, 2097156);
        VT(gPl.panel, 336, void (*)(void *, int))(gPl.panel, 1);
        *((uint8_t *)self + 902) = 0;
    }
}
static void pl_close(void) {
    void *ed = gPl.editor;
    if (!gPl.panel || !ed) return;
    int vis = VT(gPl.panel, 344, int (*)(void *))(gPl.panel);
    if (!vis) return;
    void *act = ((void *(*)(void *, int))VA(0x100547054ULL))(gPl.panel, 1);
    int64_t to = *(int64_t *)VA(0x1016CB970ULL);
    float out[2] = { LF(ed, 664) * 0.5f, 0.0f };
    void *r = ((void *(*)(void *, int, void *, float *, int64_t))VA(0x1005921E8ULL))(act, 200, &to, out, 6291458);
    ((void (*)(void *, int, int))VA(0x100592284ULL))(r, 201, 1);
    void *menu = *(void **)((uint8_t *)ed + 992);
    void *a2 = ((void *(*)(void *, int))VA(0x1005470B4ULL))(menu, 1);
    ((void (*)(void *, int))VA(0x10053CF04ULL))(a2, 0);
    *((uint8_t *)ed + 902) = 1;
    pl_native_drop();
}

// ---- 曲フォルダを開く: Mac(PlayCover / Catalyst)は Finder、iOS 実機はファイル App(shareddocuments://) ----
// Mac で Finder が開けない環境のために、最後の手段としてフォルダ選択シート(開始位置=そのフォルダ)を出す。
static void bm_open_picker(NSURL *dir, void *ed) {
    @try {
        Class pc = NSClassFromString(@"UIDocumentPickerViewController");
        if (!pc) return;
        id p = ((id (*)(id, SEL))objc_msgSend)((id)pc, @selector(alloc));
        p = ((id (*)(id, SEL, id, NSInteger))objc_msgSend)(p, NSSelectorFromString(@"initWithDocumentTypes:inMode:"), @[@"public.folder"], 1);
        if ([p respondsToSelector:NSSelectorFromString(@"setDirectoryURL:")]) [p setValue:dir forKey:@"directoryURL"];
        gInfoEd = ed; gPickKind = 101;
        if (!gPickDelegate) gPickDelegate = [BMPickDelegate new];
        [p setValue:gPickDelegate forKey:@"delegate"];
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id win = [app valueForKey:@"keyWindow"];
        id vc = [win valueForKey:@"rootViewController"];
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), p, YES, nil);
    } @catch (NSException *ex) {
        os_log(L, "open folder: picker exception %{public}@", ex.reason);
    }
}
static void bm_open_path(NSString *path, void *ed) {
    NSURL *url = [NSURL fileURLWithPath:path isDirectory:YES];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSProcessInfo *pi = NSProcessInfo.processInfo;
        BOOL mac = ([pi respondsToSelector:@selector(isMacCatalystApp)] && pi.isMacCatalystApp) ||
                   ([pi respondsToSelector:@selector(isiOSAppOnMac)] && pi.isiOSAppOnMac);
        Class ws = NSClassFromString(@"NSWorkspace");
        os_log(L, "open folder: mac=%d NSWorkspace=%d %{public}@", mac, ws != nil, path);
        @try {
            if (mac && ws) {                                 // AppKit が載っていれば Finder で開く
                id w = ((id (*)(id, SEL))objc_msgSend)((id)ws, NSSelectorFromString(@"sharedWorkspace"));
                BOOL ok = ((BOOL (*)(id, SEL, id))objc_msgSend)(w, NSSelectorFromString(@"openURL:"), url);
                os_log(L, "open folder: NSWorkspace openURL -> %d", ok);
                if (ok) return;
            }
            NSURL *open = url;
            if (!mac) {
                NSString *esc = [path stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLPathAllowedCharacterSet];
                open = [NSURL URLWithString:[@"shareddocuments://" stringByAppendingString:esc ?: @""]] ?: url;
            }
            id app = ((id (*)(id, SEL))objc_msgSend)((id)NSClassFromString(@"UIApplication"), NSSelectorFromString(@"sharedApplication"));
            void (^done)(BOOL) = ^(BOOL ok) {
                os_log(L, "open folder: openURL -> %d", ok);
                if (!ok) dispatch_async(dispatch_get_main_queue(), ^{ bm_open_picker(url, ed); });   // 開けなければフォルダ選択シート
            };
            ((void (*)(id, SEL, id, id, id))objc_msgSend)(app, NSSelectorFromString(@"openURL:options:completionHandler:"), open, @{}, done);
        } @catch (NSException *ex) {
            os_log(L, "open folder: exception %{public}@", ex.reason);
            bm_open_picker(url, ed);
        }
    });
}
static void bm_open_song_folder(void *ed) {
    void *keep = gPlEd;
    gPlEd = ed;
    NSString *chart = pl_chart_dir();            // …/_song_XXXX/<譜面>/
    gPlEd = keep;
    if (!chart.length) { ed_toast_text(ed, "Folder: chart not found", 2); return; }
    NSString *parent = chart.stringByDeletingLastPathComponent;
    NSString *dir = [parent.lastPathComponent hasPrefix:@"_song_"] ? parent : chart;   // 曲のフォルダ(譜面フォルダの親)
    os_log(L, "open folder: %{public}@", dir);
    bm_open_path(dir, ed);
}

// ダイアログのボタン(owner = エディタ)。id: 300+ = 選択肢 / 数値入力の操作, -1 = 戻る
static void pl_dlg_cb(void *owner, void *dlg, int id) {
    (void)dlg;
    gPlDlgOpen = NO;
    if (!gPlInQ.count) return;
    void *ed = owner;
    int kind = [gPlInQ[0][@"k"] intValue];
    if (kind != 0) {                                   // GetUserSelect / GetUserOption
        if (id < 300) {                                // 戻る = 取り消し(コールバックは呼ばない)
            NSDictionary *e = gPlInQ[0];
            [gPlInQ removeObjectAtIndex:0];
            LHSession *s = [e[@"s"] pointerValue];
            char err[64] = {0};
            lh_input_deliver(s, [e[@"id"] intValue], NULL, err, sizeof err);
            if (s == gPlRunSess && !lh_pending(s)) { pl_sess_close(s); gPlRunSess = NULL; }
        } else pl_choice(ed, id - 300);
        dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });          // 続きの要求
        return;
    }
    // 数値入力: 0=OK, 1..6=±1/±10/±100, 7=キーボード
    static const double kStep[7] = { 0, 1, -1, 10, -10, 100, -100 };
    if (id < 300) {                                    // 戻る = 取り消し
        pl_input_commit(ed, nil);
        dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
    } else if (id == 300) {
        pl_input_commit(ed, pl_numstr(gPlStepVal));
        dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
    } else if (id >= 301 && id <= 306) {
        gPlStepVal += kStep[id - 300];
        dispatch_async(dispatch_get_main_queue(), ^{ pl_dialog_show(); });
    } else {                                           // キーボード: パネルの入力欄で入力する
        gPlStepKb = YES;
        gPlKbPreset = pl_numstr(gPlStepVal);
        dispatch_async(dispatch_get_main_queue(), ^{ pl_input_show(); });
    }
}

// ---- 右のツールメニュー(ノート配置と同列)にプラグインの実行項目を並べる(スクロール可)。管理(Import / 一覧)は従来のパネル ----
// 項目 = アイコン + 名前。アイコンは PluginIcon(プラグインの隣の画像)があればそれ、無ければコンセントの絵を描いて使う。
static NSMutableDictionary<NSString *, NSData *> *gPlIconCache;   // path -> [w,h,rgba...]
static void *bm_rgba_sprite(const uint8_t *rgba, int w, int h) {
    struct { const void *addr; size_t len; } mip = { rgba, (size_t)w * (size_t)h * 4 };
    void *tex = calloc(1, 0x30);
    ((void (*)(void *))VA(0x1005783E8ULL))(tex);
    long ok = ((long (*)(void *, void *, int, int, long, long))VA(0x100578504ULL))(tex, &mip, 1, 2, (long)w, (long)h);
    if (!ok) return NULL;
    void *sp = calloc(1, 0x3E0);
    ((void *(*)(void *))VA(0x10056ADCCULL))(sp);
    if (!VT(sp, 1344, long (*)(void *, void *))(sp, tex)) return NULL;
    ((void (*)(void *))VA(0x100588938ULL))(sp);
    return sp;
}
static NSData *pl_plug_icon(void) {     // コンセント(白)を 64x64 に描く
    static NSData *d;
    if (d) return d;
    const int S = 64;
    NSMutableData *m = [NSMutableData dataWithLength:8 + (size_t)S * S * 4];
    int32_t *hd = (int32_t *)m.mutableBytes; hd[0] = S; hd[1] = S;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cx = CGBitmapContextCreate((uint8_t *)m.mutableBytes + 8, S, S, 8, S * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextSetRGBFillColor(cx, 1, 1, 1, 1);
    CGContextFillRect(cx, CGRectMake(19, 6, 7, 22));          // 差し込み 2 本
    CGContextFillRect(cx, CGRectMake(38, 6, 7, 22));
    CGPathRef body = CGPathCreateWithRoundedRect(CGRectMake(12, 26, 40, 22), 7, 7, NULL);   // 本体
    CGContextAddPath(cx, body); CGContextFillPath(cx); CGPathRelease(body);
    CGContextFillRect(cx, CGRectMake(28, 46, 8, 14));         // コード
    CGContextRelease(cx); CGColorSpaceRelease(cs);
    d = m;
    return d;
}
static NSData *pl_icon_data(NSUInteger idx) {
    const LHInfo *in = pl_info(idx);
    if (in->icon[0] && !strstr(in->icon, "..") && in->icon[0] != '/') {
        NSString *rel = [NSString stringWithUTF8String:in->icon];
        NSMutableArray<NSString *> *c = [NSMutableArray array];
        [c addObject:[gPlFiles[idx].stringByDeletingLastPathComponent stringByAppendingPathComponent:rel]];
        [c addObject:[pl_dir() stringByAppendingPathComponent:rel]];
        for (NSString *p in c) {
            NSData *hit = gPlIconCache[p];
            if (hit) return hit;
            if (![NSFileManager.defaultManager fileExistsAtPath:p]) continue;
            CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:p], NULL);
            CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
            if (src) CFRelease(src);
            if (!img) continue;
            const int S = 64;
            NSMutableData *m = [NSMutableData dataWithLength:8 + (size_t)S * S * 4];
            int32_t *hd = (int32_t *)m.mutableBytes; hd[0] = S; hd[1] = S;
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef cx = CGBitmapContextCreate((uint8_t *)m.mutableBytes + 8, S, S, 8, S * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
            CGContextDrawImage(cx, CGRectMake(0, 0, S, S), img);
            CGContextRelease(cx); CGColorSpaceRelease(cs); CGImageRelease(img);
            if (!gPlIconCache) gPlIconCache = [NSMutableDictionary dictionary];
            gPlIconCache[p] = m;
            return m;
        }
    }
    return pl_plug_icon();
}
static void pl_right_click(void *ed, void *a2, void *row) {
    (void)a2;
    if (!row) return;
    int idx = (int)(intptr_t)VT(row, 704, void *(*)(void *))(row) - 1;
    os_log(L, "right list click: row=%p idx=%d", row, idx);
    if (idx < 0 || idx >= (int)gPlFiles.count) return;
    gEditor = ed;
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    gPlRDirty = YES;                                   // ON 表示の更新(行の作り直しはイベントの外で)
    pl_run(ed, (NSUInteger)idx);
}
static void pl_right_register(void) {
    if (!gPlRList || gPlRClickReg) return;
    gPlRClickReg = YES;
    uint8_t src[40] = {0};
    uint8_t *f = malloc(0x20);
    *(uintptr_t *)f = VA(0x1008F1BD8ULL);
    *(uintptr_t *)(f + 8) = (uintptr_t)pl_right_click;
    *(uintptr_t *)(f + 16) = 0;
    *(uintptr_t *)(f + 24) = (uintptr_t)gPl.editor;
    *(void **)(src + 0x18) = f;
    ((void (*)(void *, void *))VA(0x100034944ULL))((uint8_t *)gPlRList + 0x648, src);
    ((void (*)(void *))(*(void ***)f)[5])(f);
}
static void pl_right_fill(void) {
    void *list = gPlRList, *ed = gPl.editor;
    if (!list || !ed) return;
    if (!gPlFiles) pl_scan();
    pl_right_register();
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)((uint8_t *)ed + 672), "NormalFont_H");
    float *sz = VT(list, 328, float *(*)(void *))(list);
    float listW = sz[0];
    float rowH = fontH * 1.9f;
    ((void (*)(void *))VA(0x100048BF4ULL))(list);
    int emode = info_mode(ed);
    for (NSUInteger i = 0; i < gPlFiles.count; i++) {
        const LHInfo *in = pl_info(i);
        if (in->err[0] || (in->mode >= 0 && in->mode != emode)) continue;     // 使えないものは並べない(管理パネルには出る)
        void *row = ((void *(*)(void))VA(0x100544A64ULL))();
        float rs[2] = { listW, rowH };
        VT(row, 320, void (*)(void *, float *))(row, rs);
        float a00[2] = { 0, 0 }, ahalf[2] = { 0, 0.5f }, ac[2] = { 0.5f, 0.5f };
        VT(row, 296, void (*)(void *, float *))(row, a00);
        VT(list, 496, void (*)(void *, void *))(list, row);
        void *bg = ((void *(*)(void *))VA(0x1001F38B0ULL))((void *)VA(0x1016CC9F8ULL));
        VT(bg, 1032, void (*)(void *, int))(bg, 120);
        float scx = ((float (*)(void *, float, float))VA(0x10054471CULL))(bg, listW - 4, rowH - 4);
        VT(bg, 1056, void (*)(void *, int, float))(bg, 0, scx);
        VT(bg, 136, void (*)(void *, void *))(bg, (void *)VA(0x1016CB970ULL));
        VT(bg, 296, void (*)(void *, float *))(bg, a00);
        VT(row, 504, void (*)(void *, void *, int))(row, bg, 1);
        NSData *ic = pl_icon_data(i);
        void *sp = bm_rgba_sprite((const uint8_t *)ic.bytes + 8, 64, 64);
        float ix = rowH * 0.5f + 6;
        if (sp) {
            ((float (*)(void *, float, float))VA(0x10054471CULL))(sp, rowH * 0.62f, rowH * 0.62f);
            float p[2] = { ix, rowH * 0.5f };
            VT(sp, 136, void (*)(void *, float *))(sp, p);
            VT(sp, 296, void (*)(void *, float *))(sp, ac);
            VT(row, 504, void (*)(void *, void *, int))(row, sp, 3);
        }
        void *l1 = ((void *(*)(float))VA(0x1001F3608ULL))(fontH);
        BOOL on = gPlSess && gPlSessIdx == (NSInteger)i && gPlSessEd == ed;
        NSString *base = [NSString stringWithUTF8String:in->name] ?: @"";
        if (base.length > 9) base = [[base substringToIndex:8] stringByAppendingString:@"…"];
        NSString *nm = on ? [base stringByAppendingString:@" [ON]"] : base;
        set_label_text(l1, nm.UTF8String);
        float p1[2] = { ix + rowH * 0.5f, rowH * 0.5f };
        VT(l1, 136, void (*)(void *, float *))(l1, p1);
        VT(l1, 296, void (*)(void *, float *))(l1, ahalf);
        VT(l1, 680, void (*)(void *, int))(l1, 1);
        VT(row, 504, void (*)(void *, void *, int))(row, l1, 2);
        VT(row, 720, void (*)(void *, void *))(row, (void *)(intptr_t)(i + 1));
    }
    ((void (*)(void *, int))VA(0x100048FF8ULL))(list, 0);
}
// 一覧を開いている間、レーン(_edit / _grids / _gridbg / _judge)を左基準で横に縮めて、一覧と重ならないようにする。
// 見た目だけの変更なので、レーンをタッチしたら一覧を閉じて元へ戻す(その最初のタッチは消費する)。
static BOOL pl_node_has(void *parent, void *target, int depth) {   // target が parent の子孫か
    if (depth <= 0 || !parent) return NO;
    nvec *ch = node_children(parent);
    if (!ch || ch->begin > ch->end || (ch->end - ch->begin) > 100000) return NO;
    for (void **p = ch->begin; p != ch->end; p++) {
        if (*p == target) return YES;
        if (pl_node_has(*p, target, depth - 1)) return YES;
    }
    return NO;
}
static const char *pl_cls(void *o) {          // RTTI のクラス名(診断用)
    void **vt = *(void ***)o;
    void **ti = vt ? (void **)vt[-1] : NULL;
    const char *nm = ti ? (const char *)ti[1] : NULL;
    return nm ? nm : "?";
}
static void pl_dump_children(void *n, int depth, int maxDepth, const char *tag) {   // 診断: 子ノードの種類と位置・大きさ
    nvec *ch = node_children(n);
    if (!ch || ch->begin > ch->end) return;
    size_t cnt = (size_t)(ch->end - ch->begin);
    int shown = 0;
    for (void **p = ch->begin; p != ch->end && shown < 40; p++, shown++) {
        void *c = *p;
        float x = VT(c, 192, float (*)(void *))(c), y = VT(c, 208, float (*)(void *))(c);
        float *sz = VT(c, 328, float *(*)(void *))(c);
        nvec *cc = node_children(c);
        size_t k = cc ? (size_t)(cc->end - cc->begin) : 0;
        os_log(L, "dump %{public}s d=%d %p %{public}s pos=(%.1f,%.1f) size=(%.1f,%.1f) sx=%.3f vis=%d kids=%zu (of %zu)", tag, depth, c, pl_cls(c),
               x, y, sz ? sz[0] : 0, sz ? sz[1] : 0, *(float *)((uint8_t *)c + 64), ((uint8_t *)c)[577], k, cnt);
        if (depth < maxDepth && k && k < 60) pl_dump_children(c, depth + 1, maxDepth, tag);
    }
}
static void pl_lane_shrink(void *ed, BOOL on) {
    if (!on) {
        if (!gPlShrink.applied) return;
        gPlShrink.applied = NO;
        for (int i = 0; i < gPlShrink.cnt; i++) {
            void *n = gPlShrink.t[i];
            if (!n) continue;
            float p[2] = { gPlShrink.px[i], VT(n, 208, float (*)(void *))(n) };
            VT(n, 136, void (*)(void *, float *))(n, p);
            *(float *)((uint8_t *)n + 64) = gPlShrink.sx[i];
            uint8_t *b = (uint8_t *)n;
            b[168] = 1; b[368] = 1; b[300] = 1; b[437] = 1;
        }
        return;
    }
    if (gPlShrink.applied || !ed || !lay_of(ed) || !gPlRList || !gPlShrink.n[4]) return;
    uint8_t *e = (uint8_t *)ed;
    void *cont = *(void **)(e + 1472);
    float cx = VT(cont, 192, float (*)(void *))(cont);
    float W = LF(ed, 664), H0 = LF(ed, 128);
    if (H0 < 1) return;
    {   // 診断(1 回): エディタ直下のノードと、容器・グリッド層の内訳
        static void *dumped;
        if (dumped != ed) {
            dumped = ed;
            os_log(L, "dump: editor=%p cont=%p lay=%p W=%.0f H=%.0f cx=%.1f xw=%.1f", ed, cont, *(void **)(e + 1112), W, H0, cx, LF(ed, 1696));
            pl_dump_children(ed, 0, 1, "ed");
            void *ly = *(void **)(e + 1112);
            if (ly) pl_dump_children(ly, 0, 1, "lay");
        }
    }
    float u = H0 / 720.0f;
    float *szL = VT(gPlRList, 328, float *(*)(void *))(gPlRList);
    VT(gPlRBox, 184, void (*)(void *, float))(gPlRBox, cx);                               // 一覧はレーンの左端に置き(左の拍番号 / 波形の列に重ねない)、レーンを右へ縮めて場所を空ける
    float listRight = cx + szL[0] + 8 * u;
    float x1;
    if (!lay_of(ed)->slide) {
        int cnt = e[1700]; if (cnt < 1) cnt = 1;
        x1 = cx + LF(ed, 1632 + 4 * (cnt - 1)) + LF(ed, 1672);
    } else x1 = cx + LF(ed, 1696);
    float s = (x1 - listRight) / (x1 - cx);        // 縮小の基準点はレーンの右端(左の一覧側へ詰めるため、右端を固定して縮める)
    if (!(s < 0.98f)) return;                      // 重ならないなら何もしない
    if (s < 0.4f) s = 0.4f;
    // 縮める対象 = エディタ直下のノードのうち、レーン領域(左端 x = cx、幅 ≒ レーンの幅)を占めるもの
    // (判定線 / 横のグリッド線の DrawNode / 背景 / ノートの容器 / 幅バー など。実機の構成ダンプで確認)
    gPlShrink.cnt = 0;
    {
        nvec *ch = node_children(ed);
        float lw = x1 - cx;
        for (void **p = ch->begin; p != ch->end && gPlShrink.cnt < 24; p++) {
            void *c = *p;
            float px = VT(c, 192, float (*)(void *))(c);
            float *sz = VT(c, 328, float *(*)(void *))(c);
            float sxs = *(float *)((uint8_t *)c + 64);
            float w = sz ? sz[0] * (sxs > 0 ? sxs : 1) : 0;
            if (fabsf(px - cx) < 2.0f && w > lw * 0.9f && w < lw * 1.1f) gPlShrink.t[gPlShrink.cnt++] = c;
        }
    }
    gPlShrink.s = s; gPlShrink.cx = x1; gPlShrink.listLeft = listRight;   // cx = 縮小の基準点(右端)/ listLeft = 一覧の右端
    gPlShrink.contScaled = NO;
    for (int i = 0; i < gPlShrink.cnt; i++) if (gPlShrink.t[i] == cont) gPlShrink.contScaled = YES;
    os_log(L, "lane shrink: s=%.3f targets=%d cx=%.1f x1=%.1f listRight=%.1f", s, gPlShrink.cnt, cx, x1, listRight);
    for (int i = 0; i < gPlShrink.cnt; i++) {
        void *n = gPlShrink.t[i];
        float bx = VT(n, 192, float (*)(void *))(n), by = VT(n, 208, float (*)(void *))(n);
        gPlShrink.px[i] = bx;
        gPlShrink.sx[i] = *(float *)((uint8_t *)n + 64);
        float p[2] = { x1 + s * (bx - x1), by };
        VT(n, 136, void (*)(void *, float *))(n, p);
        *(float *)((uint8_t *)n + 64) = gPlShrink.sx[i] * s;
        uint8_t *b = (uint8_t *)n;
        b[168] = 1; b[368] = 1; b[300] = 1; b[437] = 1;
    }
    gPlShrink.applied = YES;
}
static float pl_node_px(void *n) {
    if (gPlShrink.applied) for (int i = 0; i < gPlShrink.cnt; i++) if (gPlShrink.t[i] == n) return gPlShrink.px[i];
    return VT(n, 192, float (*)(void *))(n);
}
static float pl_lasso_local_x(void *n, float vx) {
    float cur = VT(n, 192, float (*)(void *))(n);
    if (!gPlShrink.applied) return vx - cur;
    for (int i = 0; i < gPlShrink.cnt; i++) if (gPlShrink.t[i] == n) return vx - gPlShrink.px[i];   // 縮んだノード: ローカル座標に縮尺がかかるので、縮む前の位置からの差でよい
    return gPlShrink.cx + gPlShrink.s * (vx - gPlShrink.cx) - cur;                                  // 縮んでいないノード: 縮んだ画面上の位置へ戻す
}
static void pl_right_set(BOOL on) {
    if (!gPlRList) return;
    gPlROn = on;
    if (gPlRBox) VT(gPlRBox, 336, void (*)(void *, int))(gPlRBox, on);   // 入れ物は開いている間だけ表示(閉じている間はタッチを邪魔しない)
    VT(gPlRList, 336, void (*)(void *, int))(gPlRList, on);
    if (on) gPlRDirty = YES;               // 開くときに最新の状態へ
    if (on && gPl.editor && lay_of(gPl.editor)) {   // 開く時点の選択を控える(メニュー操作や一覧のタップで解除されても、プラグインは選択中のノートを受け取れる)
        void **sv = sel_vec(gPl.editor);
        if (sv[0] != sv[1]) sel_snapshot_take(gPl.editor);
    }
    pl_lane_shrink(gPl.editor, on);
}
static void pl_right_toggle(void) { pl_right_set(!gPlROn); }
// 縮小中のタッチ座標: 見た目が縮んでいるだけなので、標準の処理(選択・配置・投げ縄・プラグイン)へ渡す前に元の座標系へ戻す。
// 一覧の領域(縮んだレーンの右)へのタッチは標準の処理へ渡さない(一覧のスクロール・タップのため)。1 = 標準の処理を飛ばす
static float pl_list_left(void *ed) {      // 一覧の右端(エディタ座標。名前は旧仕様の名残)。縮小しない幅でも、一覧上のタッチを標準の処理(選択解除など)へ渡さないために使う
    if (gPlShrink.applied) return gPlShrink.listLeft;
    if (!gPlRList || !gPlRBox) return -1e9f;
    float u = LF(ed, 128) / 720.0f;
    float *szL = VT(gPlRList, 328, float *(*)(void *))(gPlRList);
    return VT(gPlRBox, 192, float (*)(void *))(gPlRBox) + szL[0] + 8 * u;
}
static float gPlMapOrigX; static BOOL gPlMapped;
static int pl_right_lane_map(void *ed, void *pos) {
    gPlMapped = NO;
    if (gPlROn && pos && ed == gPl.editor && !gPlShrink.applied && ((float *)pos)[0] <= pl_list_left(ed)) return 1;
    if (!pos || !gPlShrink.applied || ed != gPl.editor) return 0;
    float *pt = (float *)pos;
    if (pt[0] <= gPlShrink.listLeft) return 1;
    gPlMapOrigX = pt[0]; gPlMapped = YES;
    pt[0] = gPlShrink.cx + (pt[0] - gPlShrink.cx) / gPlShrink.s;
    return 0;
}
// 標準の処理へ渡す前に座標を戻す: ノートの容器(+1472)が縮んでいるときは、標準の処理が容器のローカル座標(縮尺込みの逆変換 sub_100546980)で
// レーンを決めるので、元の画面座標のままで正しい。ここで戻さないと縮尺が二重にかかり、タップと違う場所へノートが置かれる。
// 自前の処理(プラグイン / 投げ縄)は縮尺を含まない座標が必要なので、それらを通した後でこれを呼ぶ
static void pl_right_lane_unmap(void *pos) {
    if (gPlMapped && pos && gPlShrink.contScaled) ((float *)pos)[0] = gPlMapOrigX;
    gPlMapped = NO;
}
static void pl_right_tick(void *ed) {      // 毎フレーム呼ばれる: 要求があったときだけ一覧を作り直す
    if (ed != gPl.editor) return;
    if (gPlROn && gPlShrink.n[4] && !VT(gPlShrink.n[4], 344, int (*)(void *))(gPlShrink.n[4])) pl_right_set(NO);   // ツールメニューが閉じた
    if (!gPlRDirty || !gPlRList) return;
    gPlRDirty = NO;
    pl_right_fill();
}
