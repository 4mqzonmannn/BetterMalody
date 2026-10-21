// BetterMalody runtime (dylib). 静的パッチのケイブ経由で呼ばれる。
//  H1: touch handler(sub_100075E60) フック。tag 700 = effect マネージャを開く。
#include "lua_host.h"
#import <Foundation/Foundation.h>
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <os/log.h>
#import <Accelerate/Accelerate.h>
#import <mach/mach.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include "vorbis_api.h"
#import <mach-o/dyld.h>
#import <os/log.h>
#import <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BM_BASE_VA        0x100000000ULL
#define BM_SLOT_VA        0x1016CF800ULL   // __DATA 末尾の未使用領域(8B刻み)
#define BM_ORIG_TOUCH_VA  0x100005F70ULL   // 元の touch handler 入口の退避トランポリン
// フック(tools/make_hook_patch.py の HOOKS と一致): slot=BM_SLOT_VA+8*i, orig=cave+16
#define HK_REFRESH_SLOT 1
#define BM_ORIG_REFRESH_VA 0x100005E58ULL  // sub_1001B0AF0
#define BM_ORIG_ZOOM_VA    0x100005E70ULL  // sub_1001B10C0
#define BM_ORIG_KEYTOUCH_VA 0x100005EE0ULL // sub_1001B1804
#define BM_ORIG_GRID_VA    0x100005EF8ULL  // sub_1001B1544
#define BM_ORIG_LAYER_VA   0x100005F88ULL  // sub_1001B3E40
#define BM_VA_KEY_VT       0x10092B850ULL  // EditorKey vtable (this[0])
#define BM_VA_SLIDE_VT     0x100910E90ULL  // EditorSlide vtable (this[0])
#define BM_ORIG_S_REFRESH_VA 0x100005010ULL // sub_1000C5408 (EditorSlide update)
#define BM_ORIG_S_ZOOM_VA    0x100005028ULL // sub_1000C5AB0
#define BM_ORIG_S_TOUCH_VA   0x100005040ULL // sub_1000C6744
#define BM_VA_PATH_ADDPT   0x1001670B0ULL  // void(PathSpriteEditor*, float pt[2]) : 点追加
#define BM_ORIG_UIINIT_VA  0x100005058ULL  // sub_100073140 (EditorBase UI init)
#define BM_VA_SETSIZE      0x1000AA59CULL  // void(Node*, float size[2])
#define BM_VA_EFFECT_VT   0x10091B850ULL   // effect 構造体 vtable
#define BM_VA_BPM_TIME    0x100120A24ULL   // double sub(bpmPoint*, Beat*) : BPM点基準の拍→ms
#define BM_TAG_SPEC_TOGGLE 736   // スペクトログラム ON/OFF
#define BM_TAG_SPEC_MORE   737   // 濃く
#define BM_TAG_SPEC_LESS   738   // 薄く
#define BM_TAG_SPEC_OFFSET 739   // オフセット検証
#define BM_TAG_OPEN_FX    700   // 左のFXボタン = プレビュー ON/OFF
#define BM_TAG_MORE_FX    701   // 選択メニューの項目 = effect リスト
#define BM_TAG_MORE_BPM   702   // 選択メニューの項目 = BPM リスト(元の動作)
#define BM_TAG_PLUGINS    766   // 選択メニューの項目 = Lua プラグイン管理
#define BM_TAG_PDEV       774   // その他 > Plugin Dev(ログ / Lua コンソール)
#define BM_TAG_OPTS       775   // その他 > Editor options(オプトインの設定)
#define BM_TAG_CHOICE_BACK 773  // 選択メニュー(BPM / Effect / Plugins / Folder)の Back = メニューを閉じる
#define BM_TAG_FOLDER     767   // 選択メニューの項目 = 曲フォルダをファイル App / Finder で開く
#define BM_TAG_BPM_ICON   18    // 右のツールバーの2番目(メトロノーム)= 元はBPMリストを開く
#define BM_VA_POS2BEAT    0x100021A0CULL   // {x0,x1} sub(timeline*(editor+0x2e8), int snap, double pos): 現在位置→拍(m|num<<32, den)
#define EDITOR_OFF_TIMELINE 0x2e8
#define EDITOR_OFF_CURPOS   0x358          // double: 現在位置(BPM追加ハンドラが使うもの)

static os_log_t L;
static uintptr_t gSlide;
#define VA(x) ((uintptr_t)(x) + gSlide)

// ---- ゲーム内構造体 (IDA 解析: sub_1001C5BF0 / sub_1001C5ED8 / sub_100064540) ----
// editor が保持する outer オブジェクト(= *(editor+0x2d8))。編集中のデータは std::deque<T*> に有る:
//   deque{ +0 first, +8 begin, +16 end, +24 cap, +32 start, +40 size } (libc++, 1ブロック=512ポインタ)
//   outer+584 = BPM deque(48B要素), outer+632 = sound, outer+680 = effect deque(112B要素)
// effect(112B): +0 vptr, +8 Beat{int m; float frac; int num; int den}, +24 double time(ms),
//   +32 flag(1=未設定) +40 double scroll, +48 flag +56 hs, +64 flag +72 sign,
//   +80 flag +81 showbar, +88 flag +96 double jump, +104 flag +105 ggt
// BPM(48B): +8 Beat, +24 time ...
#define EFF_SZ 112
#define OFF_BPMQ 584
#define OFF_EFFQ 680
#define VA_DEQ_CLEAR 0x1001C94B4ULL   // void clear(deque*)  要素をvirtual deleteする
#define VA_DEQ_PUSH  0x1001CB4A4ULL   // void push_back(deque*, T** moved)

typedef struct { uint64_t first, begin, end, cap, start, size; } deq;
typedef struct { int m, num, den; } BeatV;
static inline const uint8_t *EL(const uint8_t *e) { return e; }

static double beat_key(const uint8_t *e) {  // e = effect/BPM 要素の先頭
    int m = *(int32_t *)(e + 8), num = *(int32_t *)(e + 16), den = *(int32_t *)(e + 20);
    return m + (den ? (double)num / den : 0.0);
}
static void beat_set(uint8_t *e, BeatV b) {
    if (b.den <= 0) b.den = 1;
    *(int32_t *)(e + 8) = b.m;
    *(float *)(e + 12) = (float)b.num / b.den;
    *(int32_t *)(e + 16) = b.num;
    *(int32_t *)(e + 20) = b.den;
}
static void *gEditor;
static BOOL cur_beat(BeatV *out) {
    if (!gEditor) return NO;
    typedef struct { uint64_t a, b; } P2;
    double pos = *(double *)((uint8_t *)gEditor + EDITOR_OFF_CURPOS);
    P2 r = ((P2 (*)(void *, int, double))VA(BM_VA_POS2BEAT))((uint8_t *)gEditor + EDITOR_OFF_TIMELINE, 1, pos);
    BeatV b = { (int32_t)(uint32_t)r.a, (int32_t)(uint32_t)(r.a >> 32), (int32_t)(uint32_t)r.b };
    if (b.den <= 0 || b.num < 0 || b.m < -100000 || b.m > 100000 || b.den > 4096) return NO;
    *out = b;
    return YES;
}
static void eff_init(uint8_t *e) {
    memset(e, 0, EFF_SZ);
    *(uintptr_t *)e = VA(BM_VA_EFFECT_VT);
    beat_set(e, (BeatV){0, 0, 1});
    e[32] = 1; *(double *)(e + 40) = 1.0;
    e[48] = 1; *(double *)(e + 56) = 1.0;
    e[64] = 1; *(double *)(e + 72) = 4.0;
    e[80] = 1; e[81] = 1;
    e[88] = 1; *(double *)(e + 96) = 0.0;
    e[104] = 1; e[105] = 0;
}

// ---- deque アクセス ----
static void *gOuter;
static NSMutableArray<NSMutableData *> *gWork;   // 編集用コピー(112B ブロブ)

static inline deq *EFFQ(void) { return (deq *)((uint8_t *)gOuter + OFF_EFFQ); }
static inline deq *BPMQ(void) { return (deq *)((uint8_t *)gOuter + OFF_BPMQ); }
static uint8_t *deq_at(deq *d, size_t i) {
    uint64_t **map = (uint64_t **)d->begin;
    size_t k = d->start + i;
    return (uint8_t *)map[k >> 9][k & 511];
}
static BOOL deq_sane(deq *d, size_t elemMin) {
    (void)elemMin;
    if (d->begin > d->end || (d->end - d->begin) % 8) return NO;
    uint64_t blocks = (d->end - d->begin) / 8;
    if (d->size > 200000 || d->size > blocks * 512) return NO;
    if (d->start > blocks * 512 + 512) return NO;
    if (d->first > d->begin || d->begin > d->cap) return NO;
    return YES;
}

static double time_of(const uint8_t *e) {
    deq *q = BPMQ();
    if (!q->size) return 0;
    uint8_t *pick = deq_at(q, 0);
    double k = beat_key(e);
    for (size_t i = 0; i < q->size; i++) {
        uint8_t *b = deq_at(q, i);
        if (beat_key(b) <= k) pick = b; else break;
    }
    return ((double (*)(void *, const void *))VA(BM_VA_BPM_TIME))(pick, e + 8);
}

// 作業コピー <-> ゲームの deque
static void g_build_if_needed(void);
static BOOL work_load(void) {
    deq *q = EFFQ();
    if (!deq_sane(q, 112) || !deq_sane(BPMQ(), 48)) return NO;
    NSMutableArray *w = [NSMutableArray array];
    for (size_t i = 0; i < q->size; i++) {
        uint8_t *o = deq_at(q, i);
        if (!o || *(uintptr_t *)o != VA(BM_VA_EFFECT_VT)) return NO;   // 型の裏取り
        [w addObject:[NSMutableData dataWithBytes:o length:EFF_SZ]];
    }
    gWork = w;
    return YES;
}
static void work_commit(void) {
    // 並べ替え + 時刻再計算
    [gWork sortUsingComparator:^NSComparisonResult(NSMutableData *a, NSMutableData *b) {
        double x = beat_key(a.bytes), y = beat_key(b.bytes);
        return x < y ? NSOrderedAscending : (x > y ? NSOrderedDescending : NSOrderedSame);
    }];
    for (NSMutableData *d in gWork) *(double *)((uint8_t *)d.mutableBytes + 24) = time_of(d.bytes);
    deq *q = EFFQ();
    ((void (*)(deq *))VA(VA_DEQ_CLEAR))(q);
    for (NSMutableData *d in gWork) {
        uint8_t *o = malloc(EFF_SZ);          // operator new(0x70) 相当
        memcpy(o, d.bytes, EFF_SZ);
        *(uintptr_t *)o = VA(BM_VA_EFFECT_VT);
        uint8_t *p = o;
        ((void (*)(deq *, uint8_t **))VA(VA_DEQ_PUSH))(q, &p);
        if (p) ((void (*)(void *))(*(void ***)p)[1])(p);
    }
    os_log(L, "commit: %zu effects", (size_t)gWork.count);
    g_build_if_needed();
}


// ---- Effect プレビュー(表示のみ) ----
// EditorKey(this): float[1712]=zoom, [1716]=base, [1684]=ofs, [1688]=container基準y, [1680]=note幅,
//   double@872=現在beat, byte@883=refresh要求, node@1472=notes, @1480=grid DrawNode, @1496=beat番号ラベル, @1720=beat行数
// y(beat)=K*G(beat)-ofs, K=zoom*base。G は effect の scroll/jump を積分した拍位置(Processorと同じ積分式)
static BOOL gPreview;
static void spec_offset_check(void *ed);
static NSString *spec_find_chart_path(void *obj, size_t span);
static int gSpecSuggest = -1;   // 直近のオフセット検証の提案値
static BOOL gSpecOn;             // スペクトログラム表示(定義/初期値は下)
static int gSpecAlpha;
static volatile int gSpecState;
static BOOL gMarkers;            // マーカー表示(メニューの Effect markers で切替。FX プレビュー ON で自動的に ON)
static double gG0;      // 原点(現在位置のG)。float 精度を保つため y = K*(G-gG0) で配置する
typedef struct { double beat, scroll, g0, jumpB, hs; } GSeg;
static BOOL gHsOn;   // hs(ノート個別の速度係数)が 1.0 以外の区間がある
static GSeg gSeg[4096]; static int gSegN;

static double bpm_beatlen_at(double b) {
    deq *q = BPMQ();
    double len = 500.0;
    if (!gOuter || !q->size) return len;
    for (size_t i = 0; i < q->size; i++) {
        uint8_t *e = deq_at(q, i);
        if (beat_key(e) <= b + 1e-9) len = *(double *)(e + 40); else break;
    }
    return len > 0 ? len : 500.0;
}
static void g_build(void) {
    gSegN = 0;
    if (!gOuter) return;
    deq *q = EFFQ();
    double pb = 0, ps = 1.0, pg = 0, ph = 1.0;   // 直前の区間の開始beat/scroll/G/hs
    gHsOn = NO;
    for (size_t i = 0; i < q->size && gSegN < 4096; i++) {
        uint8_t *e = deq_at(q, i);
        double b = beat_key(e);
        double s = e[32] ? ps : *(double *)(e + 40);
        double h = e[48] ? ph : *(double *)(e + 56);
        if (fabs(h - 1.0) > 1e-9) gHsOn = YES;
        double j = e[88] ? 0.0 : *(double *)(e + 96);
        double gb = pg + ps * (b - pb);                       // 区間終端(=このeffect点)のG
        double jb = j * s / bpm_beatlen_at(b);                // jump(ms) -> beat 換算(新scroll適用後)
        gSeg[gSegN++] = (GSeg){ b, s, gb + jb, jb, h };
        pb = b; ps = s; pg = gb + jb; ph = h;
    }
}
static double gScale = 1.0;   // プレビュー専用の縮尺(1=等倍)。大きな scroll を画面内に収めて確認する用
static double G(double b) {
    if (!gPreview || gSegN == 0) return b;
    if (b < gSeg[0].beat) return b * gScale;
    int lo = 0, hi = gSegN - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (gSeg[mid].beat <= b) lo = mid; else hi = mid - 1; }
    return gScale * (gSeg[lo].g0 + gSeg[lo].scroll * (b - gSeg[lo].beat));
}

static double HS(double b) {   // beat の hs(直前の区間の値。先頭より前 / 無効時は 1)
    if (!gPreview || gSegN == 0 || b < gSeg[0].beat) return 1.0;
    int lo = 0, hi = gSegN - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (gSeg[mid].beat <= b) lo = mid; else hi = mid - 1; }
    return gSeg[lo].hs;
}
#define VT(o, off, T) ((T)(*(void ***)(o))[(off) / 8])
static inline float *F(void *a, int i) { return (float *)a + i; }       // this を float 配列として
static inline double note_beat(const uint8_t *n, int mOff, int numOff, int denOff) {
    int m = *(int32_t *)(n + mOff), nu = *(int32_t *)(n + numOff), de = *(int32_t *)(n + denOff);
    return m + (de ? (double)nu / de : 0.0);
}
static void node_set_y(void *n, float y) { VT(n, 200, void (*)(void *, float))(n, y); }
static void node_visible(void *n, int v) { VT(n, 336, void (*)(void *, int))(n, v); }
typedef struct { void **begin, **end; } nvec;
static nvec *node_children(void *n) { return VT(n, 552, nvec *(*)(void *))(n); }

typedef struct { int zoom, base, baseY, off, width; int slide; } Lay;
static const Lay LAY_KEY = { 1712, 1716, 1688, 1684, 1680, 0 };
static const Lay LAY_SLIDE = { 1716, 1720, 1700, -1, -1, 1 };
static const Lay *lay_of(void *ed) {
    if (!ed) return NULL;
    uintptr_t vt = *(uintptr_t *)ed;
    if (vt == VA(BM_VA_KEY_VT)) return &LAY_KEY;
    if (vt == VA(BM_VA_SLIDE_VT)) return &LAY_SLIDE;
    return NULL;
}
static inline float LF(void *a, int off) { return *(float *)((uint8_t *)a + off); }
#define BM_VA_TAIKO_VT 0x10091E870ULL   // EditorTaiko vtable(横スクロール。taiko.inc)
static BOOL is_taiko(void *ed) { return ed && *(uintptr_t *)ed == VA(BM_VA_TAIKO_VT); }
static void t_relayout(void *a1);


// ---- 補助線(DrawNode @+1480)を G で歪めた位置に引き直す ----
#define VA_DN_CLEAR 0x10054C53CULL    // void(DrawNode*)
#define VA_DN_LINE  0x10054BD64ULL    // void(DrawNode*, float from[2], float to[2], float color[4])
#define VA_MK_COLOR 0x100554790ULL    // void(float out[4], r,g,b,a)
#define VA_SNAP_KEY   0x1001B0B80ULL  // sub_1001B0B80(EditorKey): DrawNode 位置のスナップ(元実装)
#define VA_SNAP_SLIDE 0x1000C5498ULL  // sub_1000C5498(EditorSlide)
#define VA_GRID_KEY   0x1001B1544ULL  // 元の線描画
#define VA_GRID_SLIDE 0x1000C63E8ULL
static float gFillStep = 0.5f;  // 走査線の刻み(論理座標)。1デバイスpxに相当
static double gGridB0;          // 描画原点(beat)
static long gGridC = -1000000;  // 描画窓の中心(整数beat)
static void mk(float *c, float r, float g, float b, float a) {
    ((void (*)(float *, float, float, float, float))VA(VA_MK_COLOR))(c, r, g, b, a);
}
static void dline(void *node, float x0, float y0, float x1, float y1, const float *col) {
    float a[2] = { x0, y0 }, b[2] = { x1, y1 };
    float c[4] = { col[0], col[1], col[2], col[3] };
    ((void (*)(void *, float *, float *, float *))VA(VA_DN_LINE))(node, a, b, c);
}
// ---- slide 経路: エディタ本来の PathSpriteEditor をそのまま使い、画面付近の区間だけを
//      現在位置基準の小さな座標で組み直す(全点だとスプライト座標が±100万pxで精度破綻するため) ----
typedef struct {
    void *sprite; uint8_t *note; int n; double *g; float *x; float hw;
    double gmin, gmax;      // 経路全体の G 範囲
    int built;              // 窓の組み直し済み
    double center;          // 組み直した窓の中心(G)
    double originG;         // ノードの基準点(先頭に残した点)の G
    int shown;
} PathC;
static PathC *gPaths; static int gPathN;
static void paths_free(void) {
    for (int i = 0; i < gPathN; i++) { free(gPaths[i].g); free(gPaths[i].x); }
    free(gPaths); gPaths = NULL; gPathN = 0;
}
static void paths_build(void *a1) {   // G が変わったとき(relayout時)だけ呼ぶ
    paths_free();
    const Lay *l = lay_of(a1);
    if (!l || !l->slide) return;
    float xw = LF(a1, 1696);
    nvec *ch = node_children(*(void **)((uint8_t *)a1 + 1472));
    int cap = 0;
    for (void **p = ch->begin; p != ch->end; p++) cap++;
    gPaths = calloc(cap ? cap : 1, sizeof(PathC));
    for (void **p = ch->begin; p != ch->end; p++) {
        uint8_t *note = VT(*p, 704, uint8_t *(*)(void *))(*p);
        if (!note || !(note[113] & 8)) continue;
        uint8_t *sb = *(uint8_t **)(note + 136), *se = *(uint8_t **)(note + 144);
        int ns = (int)((se - sb) / 28);
        PathC *pc = &gPaths[gPathN++];
        pc->sprite = *p; pc->note = note;
        pc->n = ns + 1;
        pc->g = malloc(sizeof(double) * pc->n);
        pc->x = malloc(sizeof(float) * pc->n);
        double head = note_beat(note, 64, 72, 76);
        pc->g[0] = G(head);
        pc->x[0] = xw * (float)note[128] / 255.0f;
        int i = 1;
        for (uint8_t *g = sb; g != se; g += 28, i++) {
            pc->g[i] = G(head + note_beat(g, 0, 8, 12));
            pc->x[i] = xw * (float)(uint8_t)(note[128] + g[24]) / 255.0f;
        }
        pc->gmin = pc->gmax = pc->g[0];
        for (int k = 1; k < pc->n; k++) { if (pc->g[k] < pc->gmin) pc->gmin = pc->g[k]; if (pc->g[k] > pc->gmax) pc->gmax = pc->g[k]; }
        float wsp = *(float *)((uint8_t *)*p + 600);        // PathSpriteEditor の線幅(実物のLN幅)
        pc->hw = (wsp > 1.0f ? wsp : xw * 0.125f) * 0.5f;
        pc->shown = 1;
    }
}
static void paths_update(void *a1, double pos) {   // プレビュー中は本物の経路(中継点含む)を隠し、自前の描画で近似する
    (void)pos;
    const Lay *l = lay_of(a1);
    if (!l || !l->slide) return;
    for (int i = 0; i < gPathN; i++) {
        PathC *pc = &gPaths[i];
        if (pc->shown) { node_visible(pc->sprite, 0); pc->shown = 0; }
    }
}
static void paths_restore(void *a1) {   // OFF: 経路スプライトを元の表示に戻す(座標は触っていない)
    (void)a1;
    for (int i = 0; i < gPathN; i++)
        if (!gPaths[i].shown) node_visible(gPaths[i].sprite, 1);
    paths_free();
}

static void grid_node_pos(void *a1, float contY, double originBeat) {
    const Lay *l = lay_of(a1);
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    node_set_y(*(void **)((uint8_t *)a1 + 1480), contY + (float)(K * (G(originBeat) - gG0)));
}
static void grid_redraw(void *a1, double originBeat) {
    const Lay *l = lay_of(a1);
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    double pos = *(double *)((uint8_t *)a1 + 872);
    float H = LF(a1, 128);
    void *node = *(void **)((uint8_t *)a1 + 1480);
    const int R = 96;
    long c = (long)floor(originBeat);
    gGridC = (long)floor(pos); gGridB0 = originBeat;
    double base = G(originBeat);
    ((void (*)(void *))VA(VA_DN_CLEAR))(node);
    int div = l->slide ? *(uint8_t *)((uint8_t *)a1 + 1713) : *(uint8_t *)((uint8_t *)a1 + 1709);
    if (div < 1) div = 1;
    float white[4], grey[4], half[4], quar[4], grn[4];
    mk(white, 1, 1, 1, 0.8f);
    mk(grey, 0.75f, 0.75f, 0.75f, 0.5f);          // E2 と同じ明るい灰色
    mk(half, 0.4f, 0.8039216f, 0.9372549f, 0.5f);
    mk(quar, 0.9372549f, 0.4f, 0.9333333f, 0.5f);
    mk(grn, 0.4f, 0.9372549f, 0.6666667f, 0.5f);
    double win = 2.5 * H + K * fabs(G(c + 1) - G(c));   // 画面高さの2.5倍 + 1拍内でのスクロール量
    double gp = G(pos);
    float lo = 1e30f, hi = -1e30f;
    int lanes = l->slide ? 1 : *(uint8_t *)((uint8_t *)a1 + 1700);
    float W = l->slide ? LF(a1, 1696) : 0.0f;
    for (int row = -R; row < R; row++) {
        double b = (double)c + row;
        double g0 = G(b), g1 = G(b + 1);
        double d0 = K * (g0 - gp), d1 = K * (g1 - gp);
        double mn = fmin(d0, d1), mx = fmax(d0, d1);
        if (mx < -win || mn > win) continue;
        float y = (float)(K * (g0 - base));
        if (y < lo) lo = y; if (y > hi) hi = y;
        float y1 = (float)(K * (g1 - base));
        double span = fabs(K * (g1 - g0));
        BOOL subs = div >= 2 && span / div >= 2.0;
        for (int ln = 0; ln < lanes; ln++) {
            float x0, x1, sx0, sx1;
            if (l->slide) { x0 = 0; x1 = W; sx0 = 2; sx1 = W - 4; }
            else {
                float lw = LF(a1, 1676);
                x0 = LF(a1, 1632 + 4 * ln) + LF(a1, 1672) * 0.5f - lw * 0.5f;
                x1 = x0 + lw; sx0 = x0 + 4; sx1 = x1 - 4;
            }
            dline(node, x0, y, x1, y, white);
            if (!subs) continue;
            for (int j = 1; j < div; j++) {
                const float *col = grey;
                if (2 * j == div) col = half;
                else if (4 * j == div || 4 * j == 3 * div) col = quar;
                else if (div == 6 || div == 3) col = grn;
                float ys = y + (y1 - y) * (float)j / div;
                dline(node, sx0, ys, sx1, ys, col);
            }
        }
    }
    if (l->slide) {
        if (lo <= hi) {   // 縦線(列分割 / 中央線)
            uint8_t cols = *(uint8_t *)((uint8_t *)a1 + 1733);
            float top = (float)(win), bot = (float)(-win);
            float colw[4]; mk(colw, 1, 1, 1, 0.6f);
            for (int i = 1; i < cols; i++) { float x = W / cols * i; dline(node, x, bot, x, top, colw); }
            float cc[4]; mk(cc, 0.9372549f, 0.4f, 0.4f, 0.8f);
            dline(node, W * 0.5f, bot, W * 0.5f, top, cc);
        }
        // slide 経路(ロングノート)の近似描画: 本物のLN本体(ui-edit-sliden6, 淡いピンク白 α0.4)に合わせた帯。中継点は描かない。
        float fill[4];
        mk(fill, 0.827f, 0.996f, 1.0f, 0.4f);   // 実物のLN本体(ui-edit-sliden3)と同じ色・透明度。縁/頭/中継点は描かない
        float baseY = LF(a1, l->baseY);
        float clipLo = -(baseY + 120.0f), clipHi = H - baseY + 120.0f;     // 画面(判定線基準)の縦範囲
        int budget = 90000;                                                // 1フレームあたりの線数上限
        int budget0 = budget; int segsDrawn = 0;
        for (int pi = 0; pi < gPathN && budget > 0; pi++) {
            PathC *pc = &gPaths[pi];
            float hw = pc->hw;
            for (int i = 0; i + 1 < pc->n && budget > 0; i++) {
                float ya = (float)(K * (pc->g[i] - base)), yb = (float)(K * (pc->g[i + 1] - base));
                float lo = ya < yb ? ya : yb, hi = ya < yb ? yb : ya;
                if (hi < clipLo || lo > clipHi) continue;
                segsDrawn++;
                float xa = pc->x[i], xb = pc->x[i + 1];
                float l0 = lo < clipLo ? clipLo : lo, h0 = hi > clipHi ? clipHi : hi;
                if (h0 - l0 < gFillStep) {
                    float ym = 0.5f * (l0 + h0), t = (hi - lo) > 1e-6f ? (ym - ya) / (yb - ya) : 0.5f;
                    float xm = xa + (xb - xa) * t;
                    dline(node, xm - hw, ym, xm + hw, ym, fill); budget--;
                } else {
                    // y を 0.25 ずらす: 整数 y ちょうどの水平線は GL の線描画規則で描かれないことがあるため
                    for (float y = floorf(l0 / gFillStep) * gFillStep + gFillStep * 0.5f; y <= h0; y += gFillStep) {
                        if (y < l0) continue;
                        float t = (y - ya) / (yb - ya), xm = xa + (xb - xa) * t;
                        dline(node, xm - hw, y, xm + hw, y, fill);
                        if (--budget <= 0) break;
                    }
                }
            }
        }
        {
            static int fr;
            if (++fr % 120 == 1) os_log(L, "fill: pos=%.2f paths=%d segsInView=%d linesUsed=%d K=%.1f clip=[%.0f,%.0f] hw0=%.1f", pos, gPathN, segsDrawn, budget0 - budget, K, clipLo, clipHi, gPathN ? gPaths[0].hw : 0.0f);
        }
    }
}
static void grid_restore(void *a1) {   // 元の線形描画に戻す
    const Lay *l = lay_of(a1);
    paths_restore(a1);
    ((void (*)(void *))VA(l->slide ? VA_GRID_SLIDE : VA_GRID_KEY))(a1);
    ((void (*)(void *))VA(l->slide ? VA_SNAP_SLIDE : VA_SNAP_KEY))(a1);
}

static void ed_rebase(void *a1);
static void ed_apply_scroll(void *a1) {   // container y = base - K*(G(pos)-G0)
    const Lay *l = lay_of(a1);
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    double pos = *(double *)((uint8_t *)a1 + 872);
    double d = K * (G(pos) - gG0);
    if (gPreview && fabs(d) > 3000.0) {      // 原点から離れすぎ → 原点を現在位置へ付替え(y のみ再設定)
        gG0 = G(pos);
        ed_rebase(a1);
        d = 0;
    }
    float y = LF(a1, l->baseY) - (float)d;
    node_set_y(*(void **)((uint8_t *)a1 + 1472), y);
    node_set_y(*(void **)((uint8_t *)a1 + 1496), y);
    if (gPreview) {
        if (l->slide) { paths_update(a1, pos); grid_redraw(a1, pos); grid_node_pos(a1, y, pos); }
        else {
            if ((long)floor(pos) != gGridC) grid_redraw(a1, floor(pos));
            grid_node_pos(a1, y, gGridB0);
        }
    }
}
// slide の経路スプライトを G で引き直す(PathSpriteEditor: points@+624(float{x,dy}), children@+648, flag@+608)
static void path_rebuild(void *a1, uint8_t *path, uint8_t *note, float K, float xw) {
    double head = note_beat(note, 64, 72, 76);
    uint8_t *sb = *(uint8_t **)(note + 136), *se = *(uint8_t **)(note + 144);
    uint8_t *sp = (uint8_t *)path;
    *(uint64_t *)(sp + 632) = *(uint64_t *)(sp + 624);                 // 点列を空に
    void ***cb = (void ***)(sp + 648), ***ce = (void ***)(sp + 656);
    for (void **p = *cb; p != *ce; p++) VT(*p, 608, void (*)(void *, int))(*p, 1);
    *ce = *cb;
    *(uint64_t *)(sp + 608) = 0;
    double prevG = G(head);
    for (uint8_t *g = sb; g != se; g += 28) {
        double ab = head + note_beat(g, 0, 8, 12);
        double gg = G(ab);
        float pt[2] = { xw * (float)(uint8_t)(note[128] + g[24]) / 255.0f, (float)(K * (gg - prevG)) };
        ((void (*)(void *, float *))VA(BM_VA_PATH_ADDPT))(path, pt);
        prevG = gg;
        if (pt[1] < 0) {   // 負方向: 元実装は scaleY<0(反転)+回転で逆向きに描くため、scaleY を正に戻す
            nvec *seg = (nvec *)(sp + 648);
            if (seg->end != seg->begin) {
                uint8_t *spr = (uint8_t *)*(seg->end - 1);
                float *sy = (float *)(spr + 68);
                if (*sy < 0) *sy = -*sy;
                spr[168] = spr[368] = spr[300] = spr[437] = 1;   // 変換/頂点の dirty フラグ(元実装 sub_10054471C と同じ)
            }
        }
    }
    VT(path, 1192, void (*)(void *))(path);
}
static void diag_visible(void *a1, const char *tag);
static void ed_relayout(void *a1) {
    if (is_taiko(a1)) { t_relayout(a1); return; }
    const Lay *l = lay_of(a1);
    if (!l) return;
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    float off = l->off >= 0 ? LF(a1, l->off) : 0.0f;
    float w = l->width >= 0 ? LF(a1, l->width) : 0.0f;
    float xw = l->slide ? LF(a1, 1696) : 0.0f;
    gG0 = gPreview ? G(*(double *)((uint8_t *)a1 + 872)) : 0.0;
    {   // 論理→デバイス解像度の比から走査線の刻み(=1デバイスpx)を決める(実行時KVCでウィンドウ高さを読むだけ。UIKit はリンクしない)
        float H0 = LF(a1, 128);
        @try {
            Class appc = NSClassFromString(@"UIApplication");
            id app = appc ? [(id)appc valueForKey:@"sharedApplication"] : nil;
            id win = app ? [app valueForKey:@"keyWindow"] : nil;
            if (win && H0 > 1) {
                double bb[4] = {0, 0, 0, 0};          // CGRect = {x, y, w, h}(CoreGraphics を使わず直接読む)
                [[win valueForKey:@"bounds"] getValue:bb];
                double sc = [[win valueForKeyPath:@"screen.scale"] doubleValue];
                if (bb[3] > 1 && sc >= 1) {
                    float r = (float)(bb[3] * sc) / H0;
                    if (r > 0.05f && r < 20.0f) gFillStep = fminf(1.0f, fmaxf(0.25f, 1.0f / r));
                    os_log(L, "fill step: windowH=%.0f scale=%.1f logicalH=%.0f ratio=%.3f step=%.3f", bb[3], sc, H0, r, gFillStep);
                }
            }
        } @catch (NSException *ex) { os_log(L, "fill step: unavailable"); }
    }
    void *cont = *(void **)((uint8_t *)a1 + 1472);
    nvec *ch = node_children(cont);
    {
        int total = 0, withNote = 0, paths = 0; size_t maxPts = 0;
        for (void **p = ch->begin; p != ch->end; p++) {
            total++;
            uint8_t *nt = VT(*p, 704, uint8_t *(*)(void *))(*p);
            if (!nt) continue;
            withNote++;
            if (l->slide && (nt[113] & 8)) { paths++; size_t n = (*(uint8_t **)(nt + 144) - *(uint8_t **)(nt + 136)) / 28; if (n > maxPts) maxPts = n; }
        }
        double pos0 = *(double *)((uint8_t *)a1 + 872);
        os_log(L, "relayout: preview=%d slide=%d K=%.2f scale=%.3g segs=%d pos=%.3f G(pos)=%.3f children=%d notes=%d paths=%d maxPts=%zu",
               (int)gPreview, l->slide, K, gScale, gSegN, pos0, G(pos0), total, withNote, paths, maxPts);
    }
    for (void **p = ch->begin; p != ch->end; p++) {
        void *c = *p;
        uint8_t *note = VT(c, 704, uint8_t *(*)(void *))(c);
        if (!note) continue;
        double bs = note_beat(note, 64, 72, 76);
        node_set_y(c, (float)(K * (G(bs) - gG0)) - off);
        if (l->slide) {
            if (note[113] & 8) { /* 経路は線描画に置換(スプライトは隠す) */ }
        } else if (*(uint16_t *)(note + 112) & 0x80) {
            double be = note_beat(note, 88, 96, 100);
            double dG = G(be) - G(bs);
            float h = (float)(K * fabs(dG)) + w / 2.97f;
            if (dG < 0) node_set_y(c, (float)(K * (G(be) - gG0)) - off);   // 後ろ向き: 終端(下側)を基準に上へ伸ばす
            float sz[2] = { w, h };
            ((void (*)(void *, float *))VA(BM_VA_SETSIZE))(c, sz);
        }
    }
    // beat 番号ラベル: i番目 = beat i..i+1 の中央
    nvec *lb = node_children(*(void **)((uint8_t *)a1 + 1496));
    int i = 0;
    for (void **p = lb->begin; p != lb->end; p++, i++)
        node_set_y(*p, (float)(K * (0.5 * (G(i) + G(i + 1)) - gG0)));
    if (gPreview) {
        if (l->slide) paths_build(a1);
        grid_redraw(a1, floor(*(double *)((uint8_t *)a1 + 872)));
        ed_apply_scroll(a1);
    } else { ed_apply_scroll(a1); grid_restore(a1); }
}
static void ed_rebase(void *a1) {   // y のみ再設定(経路/サイズは G0 に依存しないのでそのまま)
    const Lay *l = lay_of(a1);
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    float off = l->off >= 0 ? LF(a1, l->off) : 0.0f;
    nvec *ch = node_children(*(void **)((uint8_t *)a1 + 1472));
    for (void **p = ch->begin; p != ch->end; p++) {
        void *c = *p;
        uint8_t *note = VT(c, 704, uint8_t *(*)(void *))(c);
        if (!note) continue;
        node_set_y(c, (float)(K * (G(note_beat(note, 64, 72, 76)) - gG0)) - off);
    }
    for (int k = 0; k < gPathN; k++)
        (void)k;
    nvec *lb = node_children(*(void **)((uint8_t *)a1 + 1496));
    int i = 0;
    for (void **p = lb->begin; p != lb->end; p++, i++)
        node_set_y(*p, (float)(K * (0.5 * (G(i) + G(i + 1)) - gG0)));
}

// 診断: モデル上の可視点数 と 実スプライト上の可視数 を数える(slide のみ)
static void diag_visible(void *a1, const char *tag) {
    const Lay *l = lay_of(a1);
    if (!l || !l->slide) return;
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    float H = LF(a1, 128);
    double pos = *(double *)((uint8_t *)a1 + 872);
    void *cont = *(void **)((uint8_t *)a1 + 1472);
    float contY = VT(cont, 208, float (*)(void *))(cont);
    long modelVis = 0, spriteVis = 0, segTotal = 0, pathNodes = 0, hidden = 0;
    double firstVisNodeY = 0; int firstVisSegs = 0; float firstSegY = 0, firstSegSY = 0;
    nvec *ch = node_children(cont);
    for (void **p = ch->begin; p != ch->end; p++) {
        void *c = *p;
        uint8_t *note = VT(c, 704, uint8_t *(*)(void *))(c);
        if (!note || !(note[113] & 8)) continue;
        pathNodes++;
        double head = note_beat(note, 64, 72, 76);
        uint8_t *sb = *(uint8_t **)(note + 136), *se = *(uint8_t **)(note + 144);
        for (uint8_t *g = sb; g != se; g += 28) {
            double d = K * (G(head + note_beat(g, 0, 8, 12)) - G(pos));
            if (d > -50 && d < H) modelVis++;
        }
        nvec *seg = (nvec *)((uint8_t *)c + 648);
        float ny = VT(c, 208, float (*)(void *))(c);
        for (void **q = seg->begin; q != seg->end; q++) {
            segTotal++;
            float sy = VT(*q, 208, float (*)(void *))(*q);
            float wy = contY + ny + sy;
            if (wy > -50 && wy < H) {
                spriteVis++;
                if (!firstVisSegs) { firstVisSegs = 1; firstVisNodeY = ny; firstSegY = sy; firstSegSY = *(float *)((uint8_t *)*q + 68); }
            }
        }
        (void)hidden;
    }
    os_log(L, "diag[%{public}s]: pos=%.3f K=%.1f H=%.0f contY=%.1f pathNodes=%ld segSprites=%ld modelVisiblePts=%ld spriteVisible=%ld | firstVis nodeY=%.1f segY=%.1f scaleY=%.3f",
           tag, pos, K, H, contY, pathNodes, segTotal, modelVis, spriteVis, firstVisNodeY, firstSegY, firstSegSY);
}

static void g_build_if_needed(void) {
    if (gPreview) { g_build(); if (gEditor) ed_relayout(gEditor); }
    else if (gEditor && is_taiko(gEditor)) ((void (*)(void *, int))VA(0x10013FAE8ULL))(gEditor, 1);   // Taiko: sign effect が小節線を変えるので引き直す
}
static BOOL is_key_editor(void *ed) { return lay_of(ed) != NULL || is_taiko(ed); }   // Key / Slide / Taiko
static void **sel_vec(void *ed);
static void sel_add(void *ed, void *sp);
static void *sprite_for_note(void *ed, void *note);
// Slide: 全ノートのスプライトを作り直す(標準の削除処理の前半 + 生成処理。節を変えたときの pl_rebuild_dirty と同じ手順)。選択は維持する
static void preview_rebuild_slide(void *ed) {
    uint8_t *e = (uint8_t *)ed;
    nvec *ch = node_children(*(void **)(e + 1472));
    NSMutableArray<NSValue *> *notes = [NSMutableArray array];
    for (void **p = ch->begin; p != ch->end; p++) {
        void *note = VT(*p, 704, void *(*)(void *))(*p);
        if (note) [notes addObject:[NSValue valueWithPointer:note]];
    }
    NSMutableSet<NSValue *> *sel = [NSMutableSet set];
    {
        void **sv = sel_vec(ed);
        for (void **q = (void **)sv[0]; q != (void **)sv[1]; q++) {
            void *note = VT(*q, 704, void *(*)(void *))(*q);
            if (note) [sel addObject:[NSValue valueWithPointer:note]];
        }
    }
    int n = 0;
    for (NSValue *nv in notes) {
        uint8_t *note = nv.pointerValue;
        void **vb = *(void ***)(e + 1536), **ve = *(void ***)(e + 1544);
        for (void **p = vb; p && p != ve; p++) {
            if (VT(*p, 704, void *(*)(void *))(*p) != note) continue;
            if (*p == *(void **)(e + 1584)) *(void **)(e + 1584) = NULL;
            void **sv = sel_vec(ed);
            for (void **q = (void **)sv[0]; q != (void **)sv[1]; q++) {
                if (*q != *p) continue;
                memmove(q, q + 1, (size_t)((uint8_t *)sv[1] - (uint8_t *)(q + 1)));
                sv[1] = (uint8_t *)sv[1] - 8;
                break;
            }
            VT(*(void **)(e + 1472), 616, void (*)(void *, void *, int))(*(void **)(e + 1472), *p, 1);
            memmove(p, p + 1, (size_t)((uint8_t *)ve - (uint8_t *)(p + 1)));
            *(void ***)(e + 1544) = ve - 1;
            break;
        }
        if (note[113] & 8) ((void *(*)(void *, void *))VA(0x1000C88C8ULL))(ed, note);
        else ((void (*)(void *, void *))VA(0x1000C8A68ULL))(ed, note);
        void *sp = sprite_for_note(ed, note);
        if (sp) VT(ed, 1664, void (*)(void *, void *))(ed, note);
        if (sp && [sel containsObject:nv]) sel_add(ed, sp);
        n++;
    }
    *((uint8_t *)ed + 883) = 1;
    os_log(L, "preview off: rebuilt %d slide notes", n);
}
// FX プレビューを切るとき: 自前の配置(ノートの y / Hold の長さ / Slide の経路)を引きずらないよう、ゲーム標準のノート更新(移動・編集のときと同じ
//   Key = sub_1001B2B84 / Slide = sub_1000C7CE8)を全ノートに掛けて、表示を読み込み直す。
static void preview_reload_notes(void *ed) {
    const Lay *l = lay_of(ed);
    if (!l) return;
    nvec *ch = node_children(*(void **)((uint8_t *)ed + 1472));
    NSMutableArray<NSValue *> *sprites = [NSMutableArray array];
    for (void **p = ch->begin; p != ch->end; p++) [sprites addObject:[NSValue valueWithPointer:*p]];   // 更新中に子が増減しても安全なよう先に控える
    int n = 0;
    for (NSValue *sv in sprites) {
        void *sp = sv.pointerValue;
        void *note = VT(sp, 704, void *(*)(void *))(sp);
        if (!note) continue;
        if (!l->slide) ((void (*)(float *, void *, void *))VA(0x1001B2B84ULL))((float *)ed, note, sp);
        else if (!(((uint8_t *)note)[113] & 8)) ((void (*)(void *, void *, void *))VA(0x1000C7CE8ULL))(ed, note, sp);   // Slide の経路スプライトは作り直しが重いので、ウィップだけ
        n++;
    }
    *((uint8_t *)ed + 883) = 1;
    os_log(L, "preview off: reloaded %d notes", n);
}
static void preview_set(BOOL on) {
    if (!is_key_editor(gEditor)) return;
    gPreview = on;
    gMarkers = on;           // FX プレビュー ON でマーカーも表示 / OFF で消す(以後はメニューで個別に切替可能)
    if (on) g_build();
    ed_relayout(gEditor);
    if (!on) { if (lay_of(gEditor) && lay_of(gEditor)->slide) preview_rebuild_slide(gEditor); else preview_reload_notes(gEditor); }
}

static size_t eff_count(void) { return gWork.count; }
static uint8_t *eff_at(size_t i) { return (uint8_t *)gWork[i].mutableBytes; }
static void eff_insert(const uint8_t *e) { [gWork addObject:[NSMutableData dataWithBytes:e length:EFF_SZ]]; }
static void eff_remove(size_t i) { if (i < gWork.count) [gWork removeObjectAtIndex:i]; }
static void eff_resort_and_time(void) { work_commit(); }

// ---- 文字列 <-> 値 ----
static NSString *fmt_beat(const uint8_t *e) {
    return [NSString stringWithFormat:@"%d:%d/%d", *(int32_t *)(e + 8), *(int32_t *)(e + 16), *(int32_t *)(e + 20)];
}
static NSString *fmt_opt(const uint8_t *e, int flagOff, int valOff) {
    if (e[flagOff]) return @"";
    return [NSString stringWithFormat:@"%g", *(double *)(e + valOff)];
}
static BOOL parse_beat(NSString *s, BeatV *out) {
    s = [s stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    int m = 0, n = 0, d = 1;
    if (sscanf(s.UTF8String, "%d:%d/%d", &m, &n, &d) == 3 || sscanf(s.UTF8String, "%d:%d:%d", &m, &n, &d) == 3) {
    } else if (sscanf(s.UTF8String, "%d.%d/%d", &m, &n, &d) == 3) {
    } else if (sscanf(s.UTF8String, "%d", &m) == 1 && ![s containsString:@":"]) { n = 0; d = 1; }
    else return NO;
    if (d <= 0 || n < 0 || n >= d * 4 || m < -100000 || m > 100000) return NO;
    *out = (BeatV){m, n, d};
    return YES;
}
static void set_opt(uint8_t *e, int flagOff, int valOff, NSString *s) {
    s = [s stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (!s.length) { e[flagOff] = 1; return; }
    e[flagOff] = 0; *(double *)(e + valOff) = s.doubleValue;
}
static NSString *row_text(const uint8_t *e) {
    NSMutableString *r = [NSMutableString stringWithFormat:@"%@", fmt_beat(e)];
    if (!e[32]) [r appendFormat:@"   scroll %g", *(double *)(e + 40)];
    if (!e[48]) [r appendFormat:@"   hs %g", *(double *)(e + 56)];
    if (!e[88]) [r appendFormat:@"   jump %g", *(double *)(e + 96)];
    if (!e[64]) [r appendFormat:@"   sign %g", *(double *)(e + 72)];
    if (!e[80]) [r appendFormat:@"   showbar %d", e[81]];
    if (!e[104]) [r appendFormat:@"   ggt %d", e[105]];
    return r;
}

// ---- メッセージ(ゲーム内トースト。OS のダイアログは使わない) ----
// sub_1001D15D0(toastMgr, std::string*, level, ms): 文字列を直接表示するトースト(level: 2=エラー 3=警告 4=成功 …)
#define BM_TOAST_MS 4500   // トーストの表示時間(標準は 2500ms)
static void ed_toast_text(void *ed, const char *text, int level) {
    if (!ed) return;
    uint8_t str[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(str, text);
    void *m = ((void *(*)(void *))VA(0x100055C00ULL))(*(void **)((uint8_t *)ed + 632));
    ((void (*)(void *, void *, int, int))VA(0x1001D15D0ULL))(m, str, level, BM_TOAST_MS);
}
static void alert(NSString *t, NSString *m) {
    ed_toast_text(gEditor, [NSString stringWithFormat:@"%@: %@", t, m].UTF8String, 2);
}

static void set_label_text(void *label, const char *txt);
// ---- Effect パネル(既存の BPM リスト _bpmpanel を再現) ----
// スキンの _fxpanel/_fxlist/_fxet-1..4 をUI初期化フックで結び付け、行生成/選択/追加/削除は sub_1000775DC / sub_100074B08 と同じ作り。
static struct { void *panel, *list, *beatLbl, *kindLbl, *tb[4]; void *editor; int sel; int kind; } gFx;   // kind: 0=scroll 1=hs 2=jump 3=sign
static const char *const FX_KIND_NAME[4] = { "Scroll", "HS", "Jump(ms)", "Sign" };
static const int FX_FLAG[4] = { 32, 48, 88, 64 }, FX_VAL[4] = { 40, 56, 96, 72 };
static void *cast_to(void *p, uintptr_t typeinfoVA) {
    typedef void *(*dc_fn)(const void *, const void *, const void *, long);
    static dc_fn dc;
    if (!dc) dc = (dc_fn)dlsym(RTLD_DEFAULT, "__dynamic_cast");
    if (!p || !dc) return p;
    return dc(p, (const void *)VA(0x10095EF00ULL), (const void *)VA(typeinfoVA), 0);   // Node -> T
}
static void *fx_find(void *layout, const char *name) {
    uint8_t str[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(str, name);
    return ((void *(*)(void *, void *))VA(0x100155BB0ULL))(layout, str);
}
static const char *stdstr_c(uint8_t *s) { return (s[23] & 0x80) ? *(const char **)s : (const char *)s; }
static NSString *tb_text(void *tb) {
    if (!tb) return @"";
    // sub_10006DD10(Textbox*) は std::string を値で返す(戻り先は x8)。24 バイトの構造体戻りとして呼ぶ
    typedef struct { uint8_t b[24]; } Str24;
    Str24 r = ((Str24 (*)(void *))VA(0x10006DD10ULL))(tb);
    return [NSString stringWithUTF8String:stdstr_c(r.b)] ?: @"";
}
static void tb_set(void *tb, NSString *t) {
    if (!tb) return;
    uint8_t str[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(str, t.UTF8String);
    ((void (*)(void *, void *))VA(0x10006DCB4ULL))(tb, str);
}
static void fx_bind(void *self, void *layout) {
    memset(&gFx, 0, sizeof(gFx));
    gFx.sel = -1;
    gFx.panel = fx_find(layout, "_fxpanel");
    void *l = fx_find(layout, "_fxlist");
    gFx.list = cast_to(l, 0x1008EADF0ULL);                 // ListSprite
    gFx.beatLbl = fx_find(layout, "_fxtext-3");
    gFx.kindLbl = fx_find(layout, "_fxicon-4");
    gFx.tb[0] = cast_to(fx_find(layout, "_fxet-1"), 0x1008F0B00ULL);   // Textbox(値の入力欄は1つ)
    gFx.editor = self;
    os_log(L, "fx_bind: panel=%p list=%p beat=%p kind=%p tb=%p", gFx.panel, gFx.list, gFx.beatLbl, gFx.kindLbl, gFx.tb[0]);
}
static NSString *fx_beat_str(const uint8_t *e) {   // BPMリストと同じ表記(拍は 1 始まり)
    return [NSString stringWithFormat:@"%d:%d/%d", *(int32_t *)(e + 8) + 1, *(int32_t *)(e + 16), *(int32_t *)(e + 20)];
}
static int fx_first_kind(const uint8_t *e) {   // 設定されている最初の種類(無ければ -1)
    for (int k = 0; k < 4; k++) if (!e[FX_FLAG[k]]) return k;
    return -1;
}
static NSString *fx_val_str(const uint8_t *e) {
    int k = fx_first_kind(e);
    if (k < 0) return @"-";
    int n = 0; for (int i = 0; i < 4; i++) if (!e[FX_FLAG[i]]) n++;
    return [NSString stringWithFormat:@"%s %g%@", FX_KIND_NAME[k], *(double *)(e + FX_VAL[k]), n > 1 ? @" +" : @""];
}
static void fx_set_kind(int k) {
    gFx.kind = k & 3;
    if (gFx.kindLbl) set_label_text(gFx.kindLbl, FX_KIND_NAME[gFx.kind]);
}
static void fx_update_beat_label(void) {
    BeatV b;
    if (gFx.beatLbl && cur_beat(&b))
        set_label_text(gFx.beatLbl, [NSString stringWithFormat:@"%d:%d/%d", b.m + 1, b.num, b.den].UTF8String);
}
static void fx_fill_inputs(void) {
    if (gFx.sel >= 0 && gFx.sel < (int)eff_count()) {
        const uint8_t *e = eff_at(gFx.sel);
        if (e[FX_FLAG[gFx.kind]]) { int k = fx_first_kind(e); if (k >= 0) fx_set_kind(k); }   // 現在の種類が未設定なら先頭の種類へ
        tb_set(gFx.tb[0], fmt_opt(e, FX_FLAG[gFx.kind], FX_VAL[gFx.kind]));
        set_label_text(gFx.beatLbl, fx_beat_str(e).UTF8String);
    } else {
        tb_set(gFx.tb[0], @"");
        fx_update_beat_label();
    }
}
static void fx_highlight(void) {   // 選択行の背景を不透明度100に(BPMリストと同じ)
    for (int i = 0; i < 4096; i++) {
        void *row = ((void *(*)(void *, int))VA(0x100048F14ULL))(gFx.list, i);
        if (!row) break;
        void *bg = VT(row, 528, void *(*)(void *, int))(row, 0);
        if (bg) VT(bg, 1032, void (*)(void *, int))(bg, i == gFx.sel ? 100 : 0);
    }
}
static void fx_row_click(void *ed, void *a2, void *row) {   // sub_100074B08 相当
    (void)ed; (void)a2;
    if (!row) return;
    intptr_t ud = (intptr_t)VT(row, 704, void *(*)(void *))(row);
    int idx = (int)ud - 1;
    gFx.sel = (gFx.sel == idx) ? -1 : idx;
    fx_highlight();
    fx_fill_inputs();
}
static void fx_refresh_list(void) {   // sub_1000775DC の行生成と同じ
    void *list = gFx.list, *ed = gFx.editor;
    if (!list || !ed) return;
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)((uint8_t *)ed + 672), "NormalFont_H");
    float *sz = VT(list, 328, float *(*)(void *))(list);
    float listW = sz[0];
    ((void (*)(void *))VA(0x100048BF4ULL))(list);          // list->clear()
    for (size_t i = 0; i < eff_count(); i++) {
        const uint8_t *e = eff_at(i);
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
        float p1[2] = { fontH, fontH };
        VT(l1, 136, void (*)(void *, float *))(l1, p1);
        VT(l1, 296, void (*)(void *, float *))(l1, ahalf);
        set_label_text(l1, fx_beat_str(e).UTF8String);
        VT(l1, 680, void (*)(void *, int))(l1, 1);
        VT(row, 504, void (*)(void *, void *, int))(row, l1, 2);
        void *l2 = ((void *(*)(float))VA(0x1001F3608ULL))(fontH);
        float p2[2] = { fontH + listW * 0.5f, fontH };
        VT(l2, 136, void (*)(void *, float *))(l2, p2);
        VT(l2, 296, void (*)(void *, float *))(l2, ahalf);
        set_label_text(l2, fx_val_str(e).UTF8String);
        VT(l2, 680, void (*)(void *, int))(l2, 2);
        VT(row, 504, void (*)(void *, void *, int))(row, l2, 2);
        VT(row, 720, void (*)(void *, void *))(row, (void *)(intptr_t)(i + 1));   // 行 -> 添字+1
    }
    ((void (*)(void *, int))VA(0x100048FF8ULL))(list, 0);   // 再レイアウト
    fx_highlight();
}
static void fx_register_click(void) {   // 行タップのコールバックを BPMリストと同じ方式で登録
    if (!gFx.list) return;
    uint8_t src[40] = {0};
    uint8_t *f = malloc(0x20);
    *(uintptr_t *)f = VA(0x1008F1BD8ULL);          // BPMリストの行コールバックと同じ functor vtable
    *(uintptr_t *)(f + 8) = (uintptr_t)fx_row_click;
    *(uintptr_t *)(f + 16) = 0;
    *(uintptr_t *)(f + 24) = (uintptr_t)gFx.editor;
    *(void **)(src + 0x18) = f;
    ((void (*)(void *, void *))VA(0x100034944ULL))((uint8_t *)gFx.list + 0x648, src);
    // 元実装と同様に、作業用のコピー元を破棄(destroy_deallocate)
    void **vt = *(void ***)f;
    ((void (*)(void *))vt[5])(f);
}
static BOOL gFxClickRegistered;
static void fx_open(void *self) {
    if (!gFx.panel || !gFx.list) { alert(@"Effects", @"panel not available"); return; }
    gEditor = self; gOuter = *(void **)((uint8_t *)self + 0x2d8); gFx.editor = self;
    if (!work_load()) { alert(@"Effects", @"effect store layout mismatch"); return; }
    if (!gFxClickRegistered) { fx_register_click(); gFxClickRegistered = YES; }
    gFx.sel = -1;
    fx_refresh_list();
    fx_fill_inputs();
    // BPMリストの表示処理(sub_1000775DC の show 側)と同じ: スライドイン + 操作フラグ
    int vis = VT(gFx.panel, 344, int (*)(void *))(gFx.panel);
    if (!vis) {
        void *act = ((void *(*)(void *, int))VA(0x100547054ULL))(gFx.panel, 1);
        float from[2] = { LF(self, 664) * 0.5f, 0.0f };
        int64_t to = *(int64_t *)VA(0x1016CB970ULL);
        ((void *(*)(void *, int, float *, void *, int64_t))VA(0x1005921E8ULL))(act, 200, from, &to, 2097156);
        VT(gFx.panel, 336, void (*)(void *, int))(gFx.panel, 1);
        *((uint8_t *)self + 902) = 0;
    }
}
static void fx_close(void) {
    void *ed = gFx.editor;
    if (!gFx.panel || !ed) return;
    int vis = VT(gFx.panel, 344, int (*)(void *))(gFx.panel);
    if (!vis) return;
    // BPMリストの閉じ処理(sub_1000775DC の hide 側)と同じ: スライドアウト + 右メニュー復帰 + 操作フラグ
    void *act = ((void *(*)(void *, int))VA(0x100547054ULL))(gFx.panel, 1);
    int64_t to = *(int64_t *)VA(0x1016CB970ULL);
    float out[2] = { LF(ed, 664) * 0.5f, 0.0f };
    void *r = ((void *(*)(void *, int, void *, float *, int64_t))VA(0x1005921E8ULL))(act, 200, &to, out, 6291458);
    ((void (*)(void *, int, int))VA(0x100592284ULL))(r, 201, 1);
    void *menu = *(void **)((uint8_t *)ed + 992);
    void *a2 = ((void *(*)(void *, int))VA(0x1005470B4ULL))(menu, 1);
    ((void (*)(void *, int))VA(0x10053CF04ULL))(a2, 0);
    *((uint8_t *)ed + 902) = 1;
}
static void fx_apply(void) {   // 「Apply」: 選択行の現在の種類を更新 / 未選択なら現在位置に追加(同じbeatは統合)
    NSMutableData *buf = [NSMutableData dataWithLength:EFF_SZ];
    uint8_t *tmp = buf.mutableBytes;
    int found = -1;
    if (gFx.sel >= 0 && gFx.sel < (int)eff_count()) { memcpy(tmp, eff_at(gFx.sel), EFF_SZ); found = gFx.sel; }
    else {
        BeatV cb;
        if (!cur_beat(&cb)) { alert(@"Effects", @"position not available"); return; }
        eff_init(tmp); beat_set(tmp, cb);
        double key = beat_key(tmp);
        for (size_t i = 0; i < eff_count(); i++) if (fabs(beat_key(eff_at(i)) - key) < 1e-9) { found = (int)i; memcpy(tmp, eff_at(i), EFF_SZ); break; }
    }
    set_opt(tmp, FX_FLAG[gFx.kind], FX_VAL[gFx.kind], tb_text(gFx.tb[0]));   // 空欄なら未設定
    double key2 = beat_key(tmp);
    BOOL any = fx_first_kind(tmp) >= 0;
    if (!any && found >= 0) eff_remove(found);              // 何も設定が残らない要素は削除
    else if (found >= 0) memcpy(eff_at(found), tmp, EFF_SZ);
    else if (any) eff_insert(tmp);
    work_commit();
    gFx.sel = -1;
    for (size_t i = 0; i < eff_count(); i++) if (fabs(beat_key(eff_at(i)) - key2) < 1e-9) { gFx.sel = (int)i; break; }
    fx_refresh_list();
    fx_fill_inputs();
}
static void fx_remove(void) {
    if (gFx.sel < 0 || gFx.sel >= (int)eff_count()) return;
    eff_remove(gFx.sel);
    work_commit();
    gFx.sel = -1;
    fx_refresh_list();
    fx_fill_inputs();
}
static void fx_edit_input(void) {   // 入力欄タップ: ゲーム標準の Textbox を入力状態にする(BPM リストの入力欄と同じ)
    if (gFx.tb[0]) ((void (*)(void *))VA(0x10006DA24ULL))(gFx.tb[0]);
}

// ---- フック ----
typedef intptr_t (*touch_fn)(void *self, int ev, void *node, void *pos);
static touch_fn orig_touch;
typedef void (*refresh_fn)(void *, float);   static refresh_fn orig_refresh;
typedef intptr_t (*zoom_fn)(void *, float); static zoom_fn orig_zoom;
typedef intptr_t (*keytouch_fn)(void *, int, void *, void *); static keytouch_fn orig_keytouch, orig_s_touch;
typedef void (*uiinit_fn)(void *, void *); static uiinit_fn orig_uiinit;
static refresh_fn orig_s_refresh; static zoom_fn orig_s_zoom;

// ---- 文字列(std::string)ユーティリティ: ゲームの関数で生成/設定する ----
static void set_label_text(void *label, const char *txt) {
    uint8_t buf[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(buf, txt);     // std::string 生成
    ((void (*)(void *, void *))VA(0x100569DF8ULL))(label, buf);         // Text::setString
}
// ---- 右ツールバー「BPM」押下 → [BPM / Effect] の選択メニュー(「その他」のゲーム内ポップアップを一時的に差し替え) ----
#define MAX_ORIG_ITEMS 24
static void *gMenuOrig[MAX_ORIG_ITEMS]; static int gMenuOrigN;
static BOOL gMenuSwapped;
static void *gMenuToolbar;
static void *menu_popup(void *toolbar) { return *(void **)toolbar; }
static void *menu_list(void *toolbar) { return *(void **)((uint8_t *)toolbar + 8); }
static void add_menu_item(void *toolbar, const char *iconName, int id, const char *label) {
    uint8_t icon[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(icon, iconName);
    ((int64_t (*)(void *, void *, int64_t, int64_t))VA(0x1001FF1ACULL))(toolbar, icon, 0, id);
    void *list = menu_list(toolbar), *it = NULL;
    for (int i = 0; i < 64; i++) {
        void *x = ((void *(*)(void *, int))VA(0x100048F14ULL))(list, i);
        if (!x) break;
        it = x;
    }
    if (it) {
        void *lab = ((void *(*)(void *, int))VA(0x100544B5CULL))(it, 1);
        if (lab) set_label_text(lab, label);
    }
}
static void menu_restore(void) {   // 元の項目へ戻す
    if (!gMenuSwapped || !gMenuToolbar) return;
    void *tb = gMenuToolbar, *list = menu_list(tb);
    // 自前の項目を取り除く
    for (int guard = 0; guard < 64; guard++) {
        void *x = ((void *(*)(void *, int))VA(0x100048F14ULL))(list, 0);
        if (!x) break;
        VT(x, 608, void (*)(void *, int))(x, 1);   // removeFromParentAndCleanup(true)
    }
    // 元の項目を戻す(退避時に retain した分を戻した後で解放)
    for (int i = 0; i < gMenuOrigN; i++) {
        VT(list, 496, void (*)(void *, void *))(list, gMenuOrig[i]);   // addChild
        *(uint32_t *)((uint8_t *)gMenuOrig[i] + 8) -= 1;                // 退避時の retain を解除
    }
    ((void (*)(void *, int))VA(0x100048FF8ULL))(list, 1);
    gMenuOrigN = 0; gMenuSwapped = NO;
}
static void menu_restore(void);
typedef struct { int tag; const char *icon; const char *label; } MItem;
static void menu_show_items(void *self, const MItem *items, int n) {   // 「その他」のポップアップを一時的に items へ差し替えて表示
    void *tb = (uint8_t *)self + 1400;
    if (gMenuSwapped) menu_restore();
    void *list = menu_list(tb);
    gMenuToolbar = tb; gMenuOrigN = 0;
    for (int i = 0; i < MAX_ORIG_ITEMS; i++) {
        void *x = ((void *(*)(void *, int))VA(0x100048F14ULL))(list, 0);   // 先頭を取り出しては退避
        if (!x) break;
        *(uint32_t *)((uint8_t *)x + 8) += 1;                  // retain
        VT(x, 608, void (*)(void *, int))(x, 0);               // removeFromParentAndCleanup(false)
        gMenuOrig[gMenuOrigN++] = x;
    }
    for (int i = 0; i < n; i++) add_menu_item(tb, items[i].icon, items[i].tag, items[i].label);
    gMenuSwapped = YES;
    VT(menu_popup(tb), 336, void (*)(void *, int))(menu_popup(tb), 1);   // 表示
}
static void menu_show_choice(void *self) {   // BPM / Effect の選択メニュー
    static const MItem it[] = { { BM_TAG_MORE_BPM, "ui-edit-icon1.png", "BPM" }, { BM_TAG_MORE_FX, "ui-edit-icon8.png", "Effect" },
                                  { BM_TAG_PLUGINS, "ui-edit-icon28.png", "Plugins" }, { BM_TAG_FOLDER, "ui-edit-icon19.png", "Folder" },
                                  { BM_TAG_CHOICE_BACK, "ui-edit-icon15.png", "< Back" } };
    menu_show_items(self, it, 5);
}

// ---- 選択操作(全選択 / 反転 / 解除): 標準エディタの選択状態(選択スプライトのベクタ)を直接操作する ----
// Key  : 選択ベクタ = this+1592/1600/1608, ハイライト sub_1001B2A9C(this,note,sprite), 全解除 sub_1001B1BDC(this)
// Slide: 選択ベクタ = this+1560/1568/1576, ハイライト sub_1000C7BB4(this,note,sprite), 全解除 sub_1000C5954(this)
#define BM_TAG_SEL_ALL  710
#define BM_TAG_SEL_INV  711
#define BM_TAG_SEL_CLR  712
static void **sel_vec(void *ed) { return (void **)((uint8_t *)ed + (lay_of(ed)->slide ? 1560 : 1592)); }   // [begin,end,cap]
static void sel_clear(void *ed) {
    if (lay_of(ed)->slide) ((void (*)(void *))VA(0x1000C5954ULL))(ed);
    else ((void (*)(void *))VA(0x1001B1BDCULL))(ed);
}
static BOOL sel_has(void *ed, void *sp) {
    void **v = sel_vec(ed);
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) if (*p == sp) return YES;
    return NO;
}
static void sel_add(void *ed, void *sp) {
    if (sel_has(ed, sp)) return;
    void **v = sel_vec(ed);
    void *note = VT(sp, 704, void *(*)(void *))(sp);
    if (!note) return;
    if (v[1] == v[2]) {
        void *tmp = sp;
        ((void (*)(void *, void *))VA(0x1000C930CULL))(v, &tmp);   // vector::push_back の拡張経路(元実装と同じ)
    } else { *(void **)v[1] = sp; v[1] = (uint8_t *)v[1] + 8; }
    if (lay_of(ed)->slide) ((void (*)(void *, void *, void *))VA(0x1000C7BB4ULL))(ed, note, sp);
    else ((void (*)(void *, void *, void *))VA(0x1001B2A9CULL))(ed, note, sp);
}
static NSArray *sel_children(void *ed) {   // 画面に存在するノートのスプライト一覧
    NSMutableArray *a = [NSMutableArray array];
    nvec *ch = node_children(*(void **)((uint8_t *)ed + 1472));
    for (void **p = ch->begin; p != ch->end; p++) {
        void *note = VT(*p, 704, void *(*)(void *))(*p);
        if (!note) continue;
        if (!((uint8_t *)*p)[577]) continue;                 // 非表示(下位レイヤー等)は対象外
        [a addObject:[NSValue valueWithPointer:*p]];
    }
    return a;
}
// 標準エディタは「タッチ開始(ev=0)」で選択を即解除するため、メニュー操作の時点では選択が既に空になっている。
// keytouch フック(下)で解除される直前の選択を控えておき、反転ではそれも「元の選択」として扱う。
static NSMutableSet *gSelSnap;
static void *gSelSnapEd;
static CFAbsoluteTime gSelSnapT;
static void sel_snapshot_take(void *ed) {
    void **v = sel_vec(ed);
    if (v[0] == v[1]) return;
    gSelSnap = [NSMutableSet set];
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) [gSelSnap addObject:[NSValue valueWithPointer:*p]];
    gSelSnapEd = ed; gSelSnapT = CFAbsoluteTimeGetCurrent();
}
static void sel_snapshot_drop(void) { gSelSnap = nil; gSelSnapEd = NULL; }
static void sel_select_all(void *ed) {
    sel_snapshot_drop();
    for (NSValue *v in sel_children(ed)) sel_add(ed, v.pointerValue);
}
static void sel_invert(void *ed) {
    NSMutableSet *was = [NSMutableSet set];
    NSArray *kids = sel_children(ed);
    void **v = sel_vec(ed);
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) [was addObject:[NSValue valueWithPointer:*p]];
    if (![was count] && gSelSnap && gSelSnapEd == ed && CFAbsoluteTimeGetCurrent() - gSelSnapT < 30.0) {
        for (NSValue *x in kids) if ([gSelSnap containsObject:x]) [was addObject:x];   // 現存するスプライトのみ
    }
    sel_snapshot_drop();
    sel_clear(ed);
    for (NSValue *x in kids) if (![was containsObject:x]) sel_add(ed, x.pointerValue);
}
static void sel_toast_count(void *ed) {
    void **v = sel_vec(ed);
    os_log(L, "selection: %ld", (long)(((uint8_t *)v[1] - (uint8_t *)v[0]) / 8));
}


// ---- コピー / ペースト(選択範囲ベース・反転ペースト対応) ----
// HitObject は vtable+16 に clone() を持つ。選択ノートを複製して保持し、貼り付け時にもう一度複製して
// 拍をずらし(現在位置基準)、必要なら左右反転してから、元のノート追加処理と同じ手順で挿入する。
#define BM_TAG_COPY      713
#define BM_TAG_PASTE     714
#define BM_TAG_PASTE_MIR 715
typedef struct { int64_t n, d; } Rat;
static int64_t gcd64(int64_t a, int64_t b) { if (a < 0) a = -a; if (b < 0) b = -b; while (b) { int64_t t = a % b; a = b; b = t; } return a; }
static Rat rat_of(const uint8_t *p) {
    int32_t m = *(int32_t *)p, num = *(int32_t *)(p + 8), den = *(int32_t *)(p + 12);
    if (den <= 0) den = 1;
    return (Rat){ (int64_t)m * den + num, den };
}
static Rat rat_add(Rat a, Rat b) {
    int64_t g = gcd64(a.d, b.d); int64_t d = a.d / g * b.d;
    Rat r = { a.n * (d / a.d) + b.n * (d / b.d), d };
    int64_t h = gcd64(r.n, r.d); if (h > 1) { r.n /= h; r.d /= h; }
    return r;
}
static Rat rat_neg(Rat a) { return (Rat){ -a.n, a.d }; }
static double rat_dbl(Rat a) { return (double)a.n / (double)a.d; }
static void beat_write_rat(uint8_t *p, Rat r) {
    int64_t m = r.n / r.d; int64_t num = r.n - m * r.d;
    if (num < 0) { m -= 1; num += r.d; }
    ((float (*)(uint8_t *, uint64_t, uint32_t))VA(0x1001F2EC4ULL))(p, (uint32_t)(int32_t)m | ((uint64_t)(uint32_t)num << 32), (uint32_t)r.d);
}
static void ed_toast_lv(void *ed, int langId, int level) {
    void *m = ((void *(*)(void *))VA(0x100055C00ULL))(*(void **)((uint8_t *)ed + 632));
    ((void (*)(void *, long, long, long))VA(0x1001D154CULL))(m, langId, level, BM_TOAST_MS);
}
static void ed_toast(void *ed, int langId) { ed_toast_lv(ed, langId, 3); }
static void *note_clone(void *n) { return VT(n, 16, void *(*)(void *))(n); }

// 標準のクリップボード(editor+840): +0/+8/+16 = vector<HitObject*>(複製), +24 = 範囲選択中フラグ,
//   +32 = 範囲始点(拍,double), +40 = 範囲終点, +48 = 切り取りフラグ。標準の Paste もこれを使う。
static uint8_t *stock_clip(void *ed) { return *(uint8_t **)((uint8_t *)ed + 840); }
static void stock_clip_clear(uint8_t *c) {
    void **b = *(void ***)c, **e = *(void ***)(c + 8);
    for (void **p = b; p != e; p++) if (*p) VT(*p, 8, void (*)(void *))(*p);
    *(void ***)(c + 8) = b;
}
static void stock_clip_push(uint8_t *c, void *n) {
    void ***e = (void ***)(c + 8), ***cap = (void ***)(c + 16);
    if ((void **)*e >= (void **)*cap) { void *t = n; ((void (*)(void *, void *))VA(0x100063FDCULL))(c, &t); }
    else { **e = n; *e += 1; }
}
static Rat rat_from_double(double v) { return (Rat){ (int64_t)llround(v * 27720.0), 27720 }; }

static NSArray *sel_effective(void *ed) {   // 現在の選択。メニュー操作で解除済みなら直前の選択(控え)を使う
    NSMutableArray *out = [NSMutableArray array];
    void **v = sel_vec(ed);
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) [out addObject:[NSValue valueWithPointer:*p]];
    if (![out count] && gSelSnap && gSelSnapEd == ed && CFAbsoluteTimeGetCurrent() - gSelSnapT < 30.0) {
        for (NSValue *x in sel_children(ed)) if ([gSelSnap containsObject:x]) [out addObject:x];
    }
    return out;
}
static void clip_copy(void *ed) {
    NSArray *sel = sel_effective(ed);
    uint8_t *c = stock_clip(ed);
    if (!c || c[24]) { ed_toast(ed, 2028); return; }   // 標準の範囲選択(始点指定済み)の途中は触らない
    NSMutableArray *a = [NSMutableArray array];
    double lo = 1e18, hi = -1e18;
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        void *note = VT(sp, 704, void *(*)(void *))(sp);
        if (!note) continue;
        double s0 = rat_dbl(rat_of((uint8_t *)note + 64)), s1 = rat_dbl(rat_of((uint8_t *)note + 88));
        if (s0 < lo) lo = s0;
        if (s1 > hi) hi = s1;
        void *cl = note_clone(note);
        if (cl) [a addObject:[NSValue valueWithPointer:cl]];
    }
    if (![a count]) { ed_toast(ed, 2028); return; }
    stock_clip_clear(c);
    for (NSValue *v in a) stock_clip_push(c, v.pointerValue);
    c[48] = 0;
    *(double *)(c + 32) = lo;
    *(double *)(c + 40) = hi + 0.001;
    sel_clear(ed);                                  // 選択表示を復元
    for (NSValue *sv in sel) sel_add(ed, sv.pointerValue);
    ed_toast(ed, 1040);
    os_log(L, "copy: %lu notes", (unsigned long)[a count]);
}
static void *sprite_for_note(void *ed, void *note) {
    nvec *ch = node_children(*(void **)((uint8_t *)ed + 1472));
    for (void **p = ch->begin; p != ch->end; p++)
        if (VT(*p, 704, void *(*)(void *))(*p) == note) return *p;
    return NULL;
}
static void clip_paste(void *ed, BOOL mirror) {
    uint8_t *clip = stock_clip(ed);
    if (!clip || *(void ***)clip == *(void ***)(clip + 8)) { ed_toast(ed, 2028); return; }
    gEditor = ed;
    BeatV cb;
    if (!cur_beat(&cb)) { ed_toast(ed, 2028); return; }
    Rat T = { (int64_t)cb.m * cb.den + cb.num, cb.den };
    Rat delta = rat_add(T, rat_neg(rat_from_double(*(double *)(clip + 32))));
    BOOL slide = lay_of(ed)->slide ? YES : NO;
    uint8_t *e = (uint8_t *)ed;
    void *chart = *(void **)(e + (slide ? 1592 : 1624));
    void *lay = *(void **)(e + 1112), *snd = *(void **)(e + 1080);
    int lanes = e[1700];
    NSMutableArray *added = [NSMutableArray array];
    NSMutableArray *src = [NSMutableArray array];
    for (void **p = *(void ***)clip; p != *(void ***)(clip + 8); p++) [src addObject:[NSValue valueWithPointer:*p]];
    for (NSValue *cv in src) {
        uint8_t *n = (uint8_t *)note_clone(cv.pointerValue);
        if (!n) continue;
        beat_write_rat(n + 64, rat_add(rat_of(n + 64), delta));
        beat_write_rat(n + 88, rat_add(rat_of(n + 88), delta));
        BOOL path = slide && (n[113] & 8);   // 0x800 = Slide(パス) フラグ(保存関数の v28[28] & 0x800)
        if (mirror) {
            if (!slide) { if (lanes > 0 && n[129] < lanes) { n[129] = (uint8_t)(lanes - 1 - n[129]); n[128] = n[129]; } }
            else if (!path) { n[128] = (uint8_t)(255 - n[128]); }
            else {
                n[128] = (uint8_t)(255 - n[128]);
                uint8_t *b = *(uint8_t **)(n + 136), *en = *(uint8_t **)(n + 144);
                for (; b != en; b += 28) *(int16_t *)(b + 24) = (int16_t)(0 - *(int16_t *)(b + 24));   // 節の x は int16
            }
        }
        if (!path) {     // 同じ位置に既にノートがある場合は貼り付けない
            double beat = rat_dbl(rat_of(n + 64));
            int lane = slide ? n[128] : n[129];
            BOOL hit = slide ? ((int (*)(void *, long, double))VA(0x1001C8F60ULL))(chart, lane, beat)
                             : VT(chart, 40, int (*)(void *, long, double))(chart, lane, beat);
            if (hit) { VT(n, 8, void (*)(void *))(n); continue; }
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
        [added addObject:[NSValue valueWithPointer:n]];
    }
    sel_clear(ed);
    for (NSValue *nv in added) { void *sp = sprite_for_note(ed, nv.pointerValue); if (sp) sel_add(ed, sp); }
    *(e + 883) = 1;
    os_log(L, "paste%{public}s: %lu/%lu notes", mirror ? " (mirror)" : "", (unsigned long)[added count], (unsigned long)[src count]);
}


// ---- Effect の範囲操作(コピー / ペースト / 削除) ----
// 範囲 = 選択ノートの [最初の開始拍, 最後の終了拍]。選択が無ければ標準の範囲コピー(Select copy range)の範囲。
#define BM_TAG_MARKERS 719
#define BM_TAG_FX_COPY 716
#define BM_TAG_FX_PASTE 717
#define BM_TAG_FX_DEL  718
static NSMutableArray<NSData *> *gFxClip;
static Rat gFxClipBase;
static BOOL fx_range(void *ed, Rat *lo, double *hi, BOOL *inclusive) {
    NSArray *sel = sel_effective(ed);
    BOOL have = NO; double h = -1e18; Rat l = {0, 1};
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *note = VT(sp, 704, void *(*)(void *))(sp);
        if (!note) continue;
        Rat s0 = rat_of(note + 64);
        if (!have || rat_dbl(s0) < rat_dbl(l)) { l = s0; have = YES; }
        double e1 = rat_dbl(rat_of(note + 88));
        if (e1 > h) h = e1;
    }
    if (have) { *lo = l; *hi = h; *inclusive = YES; return YES; }
    uint8_t *c = stock_clip(ed);
    if (c && !c[24]) {
        double a = *(double *)(c + 32), b = *(double *)(c + 40);
        if (b >= 0 && fabs(b - a) >= 0.0001) { *lo = rat_from_double(a); *hi = b; *inclusive = NO; return YES; }
    }
    return NO;
}
static BOOL fx_in_range(double key, Rat lo, double hi, BOOL inc) {
    double l = rat_dbl(lo);
    if (key < l - 1e-9) return NO;
    return inc ? key <= hi + 1e-6 : key < hi - 1e-9;
}
static void fx_range_op(void *ed, int tag) {
    gEditor = ed; gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    if (!gOuter || !work_load()) { ed_toast_lv(ed, 2036, 2); return; }
    if (tag == BM_TAG_FX_PASTE) {
        if (![gFxClip count]) { ed_toast_lv(ed, 2036, 2); return; }
        BeatV cb;
        if (!cur_beat(&cb)) { ed_toast_lv(ed, 2036, 2); return; }
        Rat T = { (int64_t)cb.m * cb.den + cb.num, cb.den };
        Rat delta = rat_add(T, rat_neg(gFxClipBase));
        int n = 0;
        for (NSData *d in gFxClip) {
            NSMutableData *e = [d mutableCopy];
            uint8_t *p = e.mutableBytes;
            beat_write_rat(p + 8, rat_add(rat_of(p + 8), delta));
            double key = beat_key(p);
            if (key < 0) continue;
            for (size_t i = 0; i < eff_count();) {                     // 同じ拍の既存要素は置き換え
                if (fabs(beat_key(eff_at(i)) - key) < 1e-9) eff_remove(i); else i++;
            }
            eff_insert(p); n++;
        }
        work_commit();
        *((uint8_t *)ed + 883) = 1;
        ed_toast_lv(ed, n ? 1043 : 2036, n ? 3 : 2);
        os_log(L, "fx paste: %d", n);
        return;
    }
    Rat lo; double hi; BOOL inc;
    if (!fx_range(ed, &lo, &hi, &inc)) { ed_toast_lv(ed, 2028, 2); return; }
    if (tag == BM_TAG_FX_COPY) {
        NSMutableArray *a = [NSMutableArray array];
        for (size_t i = 0; i < eff_count(); i++)
            if (fx_in_range(beat_key(eff_at(i)), lo, hi, inc)) [a addObject:[NSData dataWithBytes:eff_at(i) length:EFF_SZ]];
        if (![a count]) { ed_toast_lv(ed, 2036, 2); return; }
        gFxClip = a; gFxClipBase = lo;
        ed_toast_lv(ed, 1043, 3);
        os_log(L, "fx copy: %lu", (unsigned long)[a count]);
    } else {   // 削除
        int n = 0;
        for (size_t i = 0; i < eff_count();) {
            if (fx_in_range(beat_key(eff_at(i)), lo, hi, inc)) { eff_remove(i); n++; } else i++;
        }
        if (!n) { ed_toast_lv(ed, 2036, 2); return; }
        work_commit();
        *((uint8_t *)ed + 883) = 1;
        ed_toast_lv(ed, 1043, 3);
        os_log(L, "fx delete: %d", n);
    }
}


// ---- 選択ノートの幅を一括変更 (Slide) ----
// note+129 = 幅(0..255 = xwidth の割合), note+128 = 中心x。標準のドラッグ処理と同じ式で中心を [w/2, 255-w/2] に収める。
#define BM_TAG_WIDTH 720
static void width_set_one(void *ed, void *sp, int w255) {   // 1 ノートの幅を変更(標準のドラッグ処理と同じ式で中心を収める)
    uint8_t *e = (uint8_t *)ed;
    float xw = LF(ed, 1696);
    uint8_t *note = VT(sp, 704, uint8_t *(*)(void *))(sp);
    if (!note) return;
    note[129] = (uint8_t)w255;
    unsigned c = note[128], w2 = (unsigned)w255 >> 1, t = w2 + c;
    if (w2 > c) c = w2;
    if (t > 255) c = (uint8_t)~w2;
    note[128] = (uint8_t)c;
    float wpx = xw * (float)w255 / 255.0f;
    if (note[113] & 8) {
        ((void (*)(void *, float))VA(0x100167D8CULL))(sp, wpx);          // PathSpriteEditor の幅
    } else {
        uint8_t *fac = *(uint8_t **)(e + ((note[113] & 4) ? 1608 : 1600));
        float minw = *(float *)(fac + 32), h = *(float *)(fac + 36);
        float sz[2] = { wpx > minw ? wpx : minw, h };
        ((void (*)(void *, float *))VA(BM_VA_SETSIZE))(sp, sz);
    }
    ((void (*)(void *, void *, void *))VA(0x1000C7CE8ULL))(ed, note, sp);   // 位置の更新
    VT(ed, 1664, void (*)(void *, void *))(ed, note);                       // 変更の登録(元の処理と同じ)
}
static void width_apply(void *ed, NSArray *sel, int w255) {
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    uint8_t *e = (uint8_t *)ed;
    float xw = LF(ed, 1696);
    int n = 0;
    (void)xw;
    for (NSValue *sv in sel) { width_set_one(ed, sv.pointerValue, w255); n++; }
    if (gPreview && ed == gEditor) ed_relayout(ed);
    sel_clear(ed);
    for (NSValue *sv in sel) sel_add(ed, sv.pointerValue);
    *(e + 883) = 1;
    ed_toast_lv(ed, 1043, 3);
    os_log(L, "width: %d notes -> %d", n, w255);
}
static void width_prompt(void *ed) {      // 画面の幅スライダー(ゲーム内UI)の現在値を選択ノートへ適用する
    NSArray *sel = sel_effective(ed);
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    float xw = LF(ed, 1696);
    float cur = ((float (*)(void *))VA(0x1001FF40CULL))((uint8_t *)ed + 1732);
    if (xw <= 0 || cur < 0) { ed_toast_lv(ed, 2036, 2); return; }
    int w255 = cur >= xw ? 255 : (int)lroundf(cur * 255.0f / xw);
    width_apply(ed, sel, w255);
}


// ---- 選択ノートの移動 / 分割数 (標準のドラッグ移動と同じ後処理: 音の再登録 / 位置更新 / 変更登録) ----
#define BM_TAG_DIV_UP   727   // 既存の分割メニュー(リスト先頭)の ▲ / ▼ 行(スキンで tag=727/728)
#define BM_TAG_DIV_DN   728
#define BM_TAG_MV_LATER 723
#define BM_TAG_MV_EARL  724
#define BM_TAG_MV_LEFT  725
#define BM_TAG_MV_RIGHT 726
static int cur_div(void *ed) {   // 正本は layer(editor+1112) の +1452。editor 側の +1709/+1713 は格子の再描画時に写される
    int v = *(uint8_t *)(*(uint8_t **)((uint8_t *)ed + 1112) + 1452);
    return v > 0 ? v : *(uint8_t *)((uint8_t *)ed + (lay_of(ed)->slide ? 1713 : 1709));
}
static void div_step(void *ed, int delta) {
    uint8_t *e = (uint8_t *)ed;
    int d = cur_div(ed) + delta;
    if (d < 1) d = 1;
    if (d > 96) d = 96;
    ((void (*)(void *, int))VA(0x100021814ULL))(e + 744, d);                 // 標準の分割数変更ハンドラと同じ3手順
    ((void (*)(void *, int))VA(0x1001A2C7CULL))(*(void **)(e + 1112), d);
    *(*(uint8_t **)(e + 728) + 497) = (uint8_t)d;
    e[883] = 1;
    if (gPreview && ed == gEditor) ed_relayout(ed);            // プレビュー中は G で歪めた格子を引き直す
    else grid_restore(ed);                                       // 元の格子描画(分割数を読み込んで再描画 + 位置スナップ)
    char buf[48]; snprintf(buf, sizeof buf, "Grid division: %d", d);
    ed_toast_text(ed, buf, 4);
}
static void move_selection(void *ed, int dir, int dlane) {   // dir: +1 後ろ(拍) / -1 前 / 0 なし, dlane: -1/+1/0
    NSArray *sel = sel_effective(ed);
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    uint8_t *e = (uint8_t *)ed;
    BOOL slide = lay_of(ed)->slide ? YES : NO;
    int div = cur_div(ed); if (div < 1) div = 1;
    Rat step = { dir, div };
    int lanes = e[1700];
    int sx = 8;
    if (slide) { int c = e[1733]; if (c >= 2 && c <= 64) sx = 255 / c; }
    // 事前検査: 拍が負になる / レーンが範囲外になる移動は行わない
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
        if (!n) continue;
        if (rat_dbl(rat_add(rat_of(n + 64), step)) < 0) { ed_toast_lv(ed, 2036, 2); return; }
        if (!slide && dlane) { int l = n[129] + dlane; if (l < 0 || l >= lanes) { ed_toast_lv(ed, 2036, 2); return; } }
    }
    void *snd = *(void **)(e + 1080), *lay = *(void **)(e + 1112);
    int now = (int)((double (*)(void *))VA(0x100021B8CULL))(e + 744);
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
        if (!n) continue;
        ((void (*)(void *, long))VA(0x10006C244ULL))(snd, *(int32_t *)(n + 80));          // 旧位置の音を外す
        if (dir) {
            beat_write_rat(n + 64, rat_add(rat_of(n + 64), step));
            beat_write_rat(n + 88, rat_add(rat_of(n + 88), step));
        }
        if (dlane) {
            if (!slide) { n[129] = n[128] = (uint8_t)(n[129] + dlane); }
            else {
                unsigned w2 = (unsigned)n[129] >> 1;
                int c = (int)n[128] + dlane * sx;
                if (c < (int)w2) c = (int)w2;
                if (c > 255 - (int)w2) c = 255 - (int)w2;
                n[128] = (uint8_t)c;
            }
        }
        if (!slide) ((void (*)(float *, void *, void *))VA(0x1001B2B84ULL))((float *)ed, n, sp);
        else ((void (*)(void *, void *, void *))VA(0x1000C7CE8ULL))(ed, n, sp);
    }
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
        if (!n) continue;
        *(int32_t *)(n + 80) = now;
        if (!slide) *(int32_t *)(n + 104) = now;
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, now);
        if (!slide) ((void (*)(void *, void *))VA(0x1001A331CULL))(lay, n);
        VT(ed, 1664, void (*)(void *, void *))(ed, n);
    }
    ((void (*)(void *))VA(0x10006C288ULL))(snd);
    if (gPreview && ed == gEditor) ed_relayout(ed);
    sel_clear(ed);
    for (NSValue *sv in sel) sel_add(ed, sv.pointerValue);
    e[883] = 1;
}


// ---- 選択ノートのクオンタイズ(現在の分割数の格子へ丸める) ----
#define BM_TAG_QUANT 721
static Rat rat_quant(Rat t, int div) {   // t を 1/div 拍の最も近い値へ(非負)
    int64_t q = (2 * t.n * div + t.d) / (2 * t.d);
    return (Rat){ q, div };
}
static void quantize_selection(void *ed, int divOverride) {
    NSArray *sel = sel_effective(ed);
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    uint8_t *e = (uint8_t *)ed;
    BOOL slide = lay_of(ed)->slide ? YES : NO;
    int div = divOverride > 0 ? divOverride : cur_div(ed); if (div < 1) div = 1;
    void *snd = *(void **)(e + 1080), *lay = *(void **)(e + 1112);
    int now = (int)((double (*)(void *))VA(0x100021B8CULL))(e + 744);
    int changed = 0;
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
        if (!n) continue;
        Rat s0 = rat_of(n + 64), e0 = rat_of(n + 88);
        if (s0.n < 0) continue;
        Rat s1 = rat_quant(s0, div);
        BOOL held = rat_dbl(e0) > rat_dbl(s0) + 1e-9;
        Rat e1 = s1;
        if (held) { e1 = rat_quant(e0, div); if (rat_dbl(e1) <= rat_dbl(s1)) e1 = rat_add(s1, (Rat){ 1, div }); }
        // Slide のセグメント(頭からの相対拍): 絶対位置で丸めてから頭基準へ戻す
        uint8_t *sb = *(uint8_t **)(n + 136), *se = *(uint8_t **)(n + 144);
        BOOL segs = slide && sb && se && sb != se && ((se - sb) / 28) < 6000;
        BOOL same = rat_dbl(s1) == rat_dbl(s0) && rat_dbl(e1) == rat_dbl(e0);
        if (same && !segs) continue;
        ((void (*)(void *, long))VA(0x10006C244ULL))(snd, *(int32_t *)(n + 80));      // 旧位置の音を外す
        if (segs) {
            for (uint8_t *b = sb; b != se; b += 28) {
                Rat a = rat_add(s0, rat_of(b));
                Rat q = rat_quant(a, div);
                Rat r = rat_add(q, rat_neg(s1));
                if (rat_dbl(r) < 0) r = (Rat){ 0, 1 };
                beat_write_rat(b, r);
            }
        }
        beat_write_rat(n + 64, s1);
        beat_write_rat(n + 88, e1);
        if (!slide) ((void (*)(float *, void *, void *))VA(0x1001B2B84ULL))((float *)ed, n, sp);
        else ((void (*)(void *, void *, void *))VA(0x1000C7CE8ULL))(ed, n, sp);
        changed++;
    }
    for (NSValue *sv in sel) {
        void *sp = sv.pointerValue;
        uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
        if (!n) continue;
        *(int32_t *)(n + 80) = now;
        if (!slide) *(int32_t *)(n + 104) = now;
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, now);
        if (!slide) ((void (*)(void *, void *))VA(0x1001A331CULL))(lay, n);
        VT(ed, 1664, void (*)(void *, void *))(ed, n);
    }
    ((void (*)(void *))VA(0x10006C288ULL))(snd);
    if (gPreview && ed == gEditor) ed_relayout(ed);
    sel_clear(ed);
    for (NSValue *sv in sel) sel_add(ed, sv.pointerValue);
    e[883] = 1;
    char buf[64]; if (changed) snprintf(buf, sizeof buf, "Quantize 1/%d: %d moved, %d on grid", div, changed, (int)[sel count] - changed);
    else snprintf(buf, sizeof buf, "Quantize 1/%d: already on grid", div);
    ed_toast_text(ed, buf, 4);
    os_log(L, "quantize: div=%d changed=%d / %lu", div, changed, (unsigned long)[sel count]);
}


// ---- Lasso 選択(自由な閉曲線で囲んだノートを選択) ----
// 「その他」の Lasso mode を ON にすると、レーン上のなぞり操作が線描画になる。指を離すと閉じた多角形の内側のノートを選択する。
// 線は格子用 DrawNode(editor+1480)へ描く(離した時に元の格子描画で消える)。
#define BM_TAG_LASSO 729
static BOOL gLasso;
static NSMutableData *gLassoPts;    // float{x,y} の列(editor 空間)
static BOOL gLassoActive;
// ノーツ種別のツールバー(editor+1336)の強調: Lasso を ON にしたら外し、OFF に戻したら選択中のツール(editor+1368 = 押したボタンのタグ)を強調し直す
static void lasso_tool_highlight(void *ed, BOOL on) {
    uint8_t *tb = (uint8_t *)ed + 1336;
    void **b = *(void ***)(tb + 8), **en = *(void ***)(tb + 16);
    if (!b || b >= en || (en - b) > 64) return;
    int tool = *(int *)((uint8_t *)ed + 1368);
    for (void **p = b; p != en; p++) {
        void *btn = *p;
        if (!btn) continue;
        if (!on) { VT(btn, 1080, void (*)(void *, void *))(btn, (void *)VA(0x1016CC9F8ULL)); continue; }
        if (((int (*)(void *))(*(void ***)btn)[0x2a0 / 8])(btn) == tool) { ((int (*)(void *, void *))VA(0x1001FE188ULL))(tb, btn); break; }
    }
}
static float pl_node_px(void *n);                  // plugins.inc: プラグイン一覧でレーンを縮めている間は、縮める前のノードの x
static float pl_lasso_local_x(void *n, float vx);  // plugins.inc: 縮める前の座標系の x → ノードのローカル x(縮尺込みで元のタッチ位置に描ける)
static void lasso_redraw(void *ed, BOOL withLine) {
    const Lay *l = lay_of(ed);
    ((void (*)(void *))VA(l->slide ? VA_GRID_SLIDE : VA_GRID_KEY))(ed);          // 元の格子描画(DrawNode を消去して引き直す)
    if (!withLine || gLassoPts.length < 16) return;
    void *node = *(void **)((uint8_t *)ed + 1480);
    float ny = VT(node, 208, float (*)(void *))(node);
    const float *p = gLassoPts.bytes;
    size_t n = gLassoPts.length / 8;
    float col[4]; mk(col, 1.0f, 0.9f, 0.2f, 1.0f);
    for (size_t i = 1; i < n; i++) dline(node, pl_lasso_local_x(node, p[2 * i - 2]), p[2 * i - 1] - ny, pl_lasso_local_x(node, p[2 * i]), p[2 * i + 1] - ny, col);
    float col2[4]; mk(col2, 1.0f, 0.9f, 0.2f, 0.5f);
    dline(node, pl_lasso_local_x(node, p[2 * n - 2]), p[2 * n - 1] - ny, pl_lasso_local_x(node, p[0]), p[1] - ny, col2);   // 閉じる線(薄く)
}
static BOOL poly_has(const float *p, size_t n, float x, float y) {   // even-odd
    BOOL in = NO;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-9f) + xi)) in = !in;
    }
    return in;
}
static void lasso_select(void *ed) {
    size_t n = gLassoPts.length / 8;
    if (n < 3) return;
    const Lay *l = lay_of(ed);
    uint8_t *e = (uint8_t *)ed;
    void *cont = *(void **)(e + 1472);
    float cx = pl_node_px(cont), cy = VT(cont, 208, float (*)(void *))(cont);
    float K = LF(ed, l->zoom) * LF(ed, l->base);
    float xw = l->slide ? LF(ed, 1696) : 0;
    const float *p = gLassoPts.bytes;
    NSMutableArray *hit = [NSMutableArray array];
    for (NSValue *sv in sel_children(ed)) {
        void *sp = sv.pointerValue;
        uint8_t *note = VT(sp, 704, uint8_t *(*)(void *))(sp);
        float sx = cx + VT(sp, 192, float (*)(void *))(sp), sy = cy + VT(sp, 208, float (*)(void *))(sp);
        BOOL in = poly_has(p, n, sx, sy);
        if (!in && !l->slide && (note[112] & 0x80)) {            // Key の hold: 中間と終端
            float *sz = VT(sp, 328, float *(*)(void *))(sp);
            float h = sz ? sz[1] : 0;
            in = poly_has(p, n, sx, sy + h * 0.5f) || poly_has(p, n, sx, sy + h);
        }
        if (!in && l->slide && (note[113] & 8)) {                // Slide のパス: 各点
            double head = note_beat(note, 64, 72, 76);
            uint8_t *b = *(uint8_t **)(note + 136), *en = *(uint8_t **)(note + 144);
            int cnt = 0;
            for (; b != en && !in && cnt < 6000; b += 28, cnt++) {
                float x = cx + xw * (float)(uint8_t)(note[128] + b[24]) / 255.0f;
                float y = cy + K * (float)(head + note_beat(b, 0, 8, 12));
                in = poly_has(p, n, x, y);
            }
        }
        if (in) [hit addObject:sv];
    }
    for (NSValue *sv in hit) sel_add(ed, sv.pointerValue);   // 既に選択中のノートは解除せず、囲んだものを加える
    void **sv2 = sel_vec(ed);
    char buf[48]; snprintf(buf, sizeof buf, "Lasso: +%lu (selected %ld)", (unsigned long)[hit count], (long)((void **)sv2[1] - (void **)sv2[0]));
    ed_toast_text(ed, buf, 4);
}
// タッチ処理(keytouch / s_touch フックから)。戻り値 YES = 消費した
static BOOL lasso_touch(void *ed, int ev, void *pos) {
    if (!gLasso || gPreview || !lay_of(ed)) return NO;
    if (*((uint8_t *)ed + 903) || *((uint8_t *)ed + 901)) return NO;
    const float *pt = pos;
    if (ev == 0) {
        const Lay *l = lay_of(ed);
        void *cont = *(void **)((uint8_t *)ed + 1472);
        float cx = pl_node_px(cont);
        float x0, x1;
        if (!l->slide) {
            int cnt = ((uint8_t *)ed)[1700]; if (cnt < 1) cnt = 1;
            x0 = cx + LF(ed, 1632); x1 = cx + LF(ed, 1632 + 4 * (cnt - 1)) + LF(ed, 1672);
        } else { x0 = cx; x1 = cx + LF(ed, 1696); }
        if (pt[0] < x0 || pt[0] > x1) return NO;          // レーン外(ツールバー等)は元の処理
        gLassoActive = YES;
        gLassoPts = [NSMutableData data];
    }
    if (!gLassoActive) return NO;
    if (ev == 0 || ev == 1 || ev == 2) {
        size_t n = gLassoPts.length / 8;
        BOOL add = YES;
        if (n) {
            const float *q = (const float *)gLassoPts.bytes + 2 * (n - 1);
            if (fabsf(q[0] - pt[0]) + fabsf(q[1] - pt[1]) < 3.0f) add = NO;
        }
        if (add) { float v[2] = { pt[0], pt[1] }; [gLassoPts appendBytes:v length:8]; }
    }
    if (ev == 2) {
        gLassoActive = NO;
        lasso_select(ed);
        lasso_redraw(ed, NO);
        gLassoPts = nil;
    } else if (ev == 6) {
        gLassoActive = NO; lasso_redraw(ed, NO); gLassoPts = nil;
    } else {
        lasso_redraw(ed, YES);
    }
    return YES;
}


// ---- 再生速度メニュー(標準の _right-11): ▲+0.05 / ▼-0.05 とプリセット(1.00/0.75/0.50/0.25) ----
// 標準: speed% を chart+498(byte)へ保存し、再生機(editor+720)へ v=%/100 を設定、editor+704 の +168 にも保存。
// 速度メニューのボタン群(editor+1304 の toolbar: +8 begin / +16 end)の色を自前で強調する(標準の強調は使わない)。
#define BM_TAG_SPD_UP 730
#define BM_TAG_SPD_DN 731
static int spd_preset(int tag) {   // タグ 732.. → %
    switch (tag) { case 732: return 100; case 733: return 75; case 734: return 50; case 735: return 25; }
    return 0;
}
static int speed_pct(void *ed) { return *(uint8_t *)(*(uint8_t **)((uint8_t *)ed + 728) + 498); }
static int gSpdShown = -1; static void *gSpdShownEd;
static void speed_highlight(void *ed) {
    int pct = speed_pct(ed);
    uint8_t *tbv = (uint8_t *)ed + 1304;
    void **b = *(void ***)(tbv + 8), **en = *(void ***)(tbv + 16);
    if (!b || b >= en || (en - b) > 32) return;
    uint8_t blue[8] = {0};
    ((void (*)(void *, long, long, long))VA(0x1005545B8ULL))(blue, 2, 206, 255);
    for (void **p = b; p != en; p++) {
        int tag = VT(*p, 672, int (*)(void *))(*p);
        BOOL on = spd_preset(tag) == pct;
        VT(*p, 1080, void (*)(void *, void *))(*p, on ? (void *)blue : (void *)VA(0x1016CC9F8ULL));
    }
    gSpdShown = pct; gSpdShownEd = ed;
}
static void speed_set(void *ed, int pct) {
    if (pct < 15) pct = 15;       // 下限 0.125 倍(make_slow_patch.py)に収まる範囲
    if (pct > 100) pct = 100;
    uint8_t *e = (uint8_t *)ed;
    *(*(uint8_t **)(e + 728) + 498) = (uint8_t)pct;
    float v = pct / 100.0f;
    ((void (*)(void *, float))VA(0x1001F5A84ULL))(*(void **)(e + 720), v);
    *(float *)(*(uint8_t **)(e + 704) + 168) = v;
    speed_highlight(ed);
    char buf[40]; snprintf(buf, sizeof buf, "Speed: %.2fx", v);
    ed_toast_text(ed, buf, 4);
}


// ---- 情報画面の追加項目(Video / BGA / Skin): タップ → システムのファイル選択シート → 譜面フォルダへコピーしてメタに書き込む ----
// メタ(outer = editor+728): video = outer+112, skin = outer+448, bga = outer+472 (いずれも埋め込み std::string)。
// スキンの _infol/_infoe/_infot-12..14 (tag 745..747)。保存は標準の Save で .mc に書き出される。
#define BM_TAG_INFO_VIDEO 745
static const int kInfoOff[3] = { 112, 472, 448 };            // video, bga, skin
static const char *const kInfoName[3] = { "Video", "BGA", "Skin" };
#define BM_TAG_INFO_BAR_DN 748   // Bar begin  - / +
#define BM_TAG_INFO_BAR_UP 749
#define BM_TAG_INFO_SPD_DN 750   // Speed (Catch) - / +
#define BM_TAG_INFO_SPD_UP 751
#define BM_TAG_INFO_CLEAR  752   // Video / BGA / Skin の解除 (752..754)
static void *gInfoLbl[5];        // 0..2 = Video/BGA/Skin, 3 = Bar begin, 4 = Speed
static void *gInfoEd;
static int gPickKind = -1;
static void info_set_label(int k, const char *text) {
    if (k < 0 || k > 4 || !gInfoLbl[k]) return;
    uint8_t str[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(str, (text && *text) ? text : "-");
    ((void (*)(void *, void *))VA(0x10019ECDCULL))(gInfoLbl[k], str);          // FieldText::setText(std::string)
}
static const char *info_get(void *ed, int k) {   // outer+off の埋め込み std::string の C 文字列
    uint8_t *s = *(uint8_t **)((uint8_t *)ed + 728) + kInfoOff[k];
    return (s[23] & 0x80) ? *(const char **)s : (const char *)s;
}
static void info_put(void *ed, int k, const char *name) {
    typedef void *(*assign_fn)(void *, const char *);
    static assign_fn as;
    if (!as) as = (assign_fn)dlsym(RTLD_DEFAULT, "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");
    if (!as) { os_log(L, "info: std::string::assign not found"); return; }
    as(*(uint8_t **)((uint8_t *)ed + 728) + kInfoOff[k], name);
}
// 数値項目: chart+396 = bar_begin (Key/Taiko。負 = 未指定), chart+340 = speed (Catch)。chart = outer+8 なので outer 基準では +404 / +348。保存は標準の Save が meta から書く。
static int info_mode(void *ed) { return *(int *)(*(uint8_t **)((uint8_t *)ed + 728) + 444); }   // outer = chart + 8 (保存関数の a1 + 436)
static int32_t *info_num_ptr(void *ed, int k) {   // k: 3 = bar_begin, 4 = speed。対象外の譜面種別なら NULL
    uint8_t *m = *(uint8_t **)((uint8_t *)ed + 728);
    int mode = info_mode(ed);
    if (k == 3) return (mode == 0 || mode == 5 || mode == 6 || mode == 8) ? (int32_t *)(m + 404) : NULL;
    return mode == 3 ? (int32_t *)(m + 348) : NULL;
}
static void info_fill_labels(void *ed) {
    for (int k = 0; k < 3; k++) info_set_label(k, info_get(ed, k));
    for (int k = 3; k < 5; k++) {
        int32_t *p = info_num_ptr(ed, k);
        char b[24];
        if (!p) snprintf(b, sizeof b, "-");
        else if (*p < 0) snprintf(b, sizeof b, "Off");
        else snprintf(b, sizeof b, "%d", *p);
        info_set_label(k, b);
    }
}
static void info_step(void *ed, int k, int d) {
    int32_t *p = info_num_ptr(ed, k);
    if (!p) { ed_toast_text(ed, "Info: not available for this mode", 2); return; }
    int v = *p + d;
    if (k == 3) { int lo = (info_mode(ed) == 5) ? 0 : -1; if (v < lo) v = lo; if (v > 9999) v = 9999; }
    else { if (v < 1) v = 1; if (v > 100) v = 100; }
    *p = v;
    info_fill_labels(ed);
}
static void info_clear(void *ed, int k) {
    if (!*info_get(ed, k)) return;
    info_put(ed, k, "");
    info_set_label(k, "");
    char buf[48]; snprintf(buf, sizeof buf, "%s: cleared", kInfoName[k]);
    ed_toast_text(ed, buf, 4);
}
static void pl_import_urls(NSArray<NSURL *> *urls, void *ed);
static void cfg_audio_picked(NSURL *u);   // 設定画面: Custom key sound の wav 取り込み
static void pl_bytes_done(NSData *d);   // ReadBytesSelect の結果(nil = キャンセル)
@interface BMPickDelegate : NSObject
@end
static BMPickDelegate *gPickDelegate;
@implementation BMPickDelegate
- (void)documentPicker:(id)picker didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *u = urls.firstObject;
    void *ed = gInfoEd; int k = gPickKind; gPickKind = -1;
    if (k == 100) { pl_import_urls(urls, ed); return; }          // Lua プラグインのインポート
    if (k == 101) return;                                         // フォルダ表示用の選択シート(結果は使わない)
    if (k == 103) { cfg_audio_picked(u); return; }                // 設定画面: custom.wav の取り込み
    if (k == 102) {                                               // ReadBytesSelect
        NSData *d = nil;
        if (u) { BOOL sc = [u startAccessingSecurityScopedResource]; d = [NSData dataWithContentsOfURL:u]; if (sc) [u stopAccessingSecurityScopedResource]; }
        pl_bytes_done(d);
        return;
    }
    if (!u || !ed || k < 0) return;
    void *outer = *(void **)((uint8_t *)ed + 728);
    NSString *mc = outer ? spec_find_chart_path(outer, 0x800) : nil;
    if (!mc) mc = spec_find_chart_path(ed, 0x800);
    if (!mc) { ed_toast_text(ed, "Info: chart folder not found", 2); return; }
    NSString *dir = mc.stringByDeletingLastPathComponent;
    NSString *name = u.lastPathComponent;
    NSString *dst = [dir stringByAppendingPathComponent:name];
    NSFileManager *fm = NSFileManager.defaultManager;
    NSError *er = nil;
    BOOL same = [u.path isEqualToString:dst];
    if (!same) {
        [fm removeItemAtPath:dst error:nil];
        BOOL scoped = [u startAccessingSecurityScopedResource];
        BOOL ok = [fm copyItemAtURL:u toURL:[NSURL fileURLWithPath:dst] error:&er];
        if (scoped) [u stopAccessingSecurityScopedResource];
        if (!ok) { ed_toast_text(ed, "Info: copy failed", 2); os_log(L, "info: copy failed %{public}@", er.localizedDescription); return; }
    }
    info_put(ed, k, name.UTF8String);
    info_set_label(k, name.UTF8String);
    char buf[160]; snprintf(buf, sizeof buf, "%s: %s", kInfoName[k], name.UTF8String);
    ed_toast_text(ed, buf, 4);
    os_log(L, "info: %{public}s = %{public}@ (copied to %{public}@)", kInfoName[k], name, dir);
}
- (void)documentPickerWasCancelled:(id)picker { int k = gPickKind; gPickKind = -1; if (k == 102) pl_bytes_done(nil); }
@end
static void info_pick(void *ed, int k) {
    gInfoEd = ed; gPickKind = k;
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            Class pc = NSClassFromString(@"UIDocumentPickerViewController");
            if (!pc) { ed_toast_text(ed, "Info: file picker unavailable", 2); return; }
            NSArray *types = (k == 0) ? @[@"public.movie"] : @[@"public.item"];
            id p = ((id (*)(id, SEL))objc_msgSend)((id)pc, @selector(alloc));
            p = ((id (*)(id, SEL, id, NSInteger))objc_msgSend)(p, NSSelectorFromString(@"initWithDocumentTypes:inMode:"), types, 0 /* Import */);
            if (!gPickDelegate) gPickDelegate = [BMPickDelegate new];
            [p setValue:gPickDelegate forKey:@"delegate"];
            id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
            id win = [app valueForKey:@"keyWindow"];
            id vc = [win valueForKey:@"rootViewController"];
            while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
            ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"presentViewController:animated:completion:"), p, YES, nil);
        } @catch (NSException *ex) {
            os_log(L, "info: picker exception %{public}@", ex.reason);
            ed_toast_text(ed, "Info: file picker failed", 2);
        }
    });
}

#include "plugins.inc"
#include "undo.inc"
#include "osu.inc"
#include "opts.inc"
#include "kbd.inc"
#include "settings.inc"
#include "gameplay.inc"
#include "turbo.inc"
#include "taiko.inc"
// More(…)ボタンの強調: Lasso / Order(More の中のツール)が有効な間は、ノーツ配置系ツールと同じ青にする。
// ボタンの特定は「押した直後にツールメニュー(editor+1400)が開いたノード」を覚える(最初に More を開いたときに分かる)
static void *gMoreNode, *gMoreEd;
static int gMoreApplied = -1;
static void more_sync(void *ed) {
    if (!gMoreNode || gMoreEd != ed) return;
    BOOL want = gLasso || (gOrdMode && gOrdEd == ed);
    if (gMoreApplied == (int)want) return;
    gMoreApplied = want;
    if (want) { uint8_t col[32]; ((void (*)(void *, long, long, long))VA(0x1005545B8ULL))(col, 2, 206, 255); VT(gMoreNode, 1080, void (*)(void *, void *))(gMoreNode, col); }
    else VT(gMoreNode, 1080, void (*)(void *, void *))(gMoreNode, (void *)VA(0x1016CC9F8ULL));
}

static void *gSelMenuFor;
// 「その他」メニューの分類(カテゴリ)。カテゴリ項目を押すと、その中の項目へ差し替えて表示する(最後に「戻る」)。
#define BM_TAG_CAT_SELECT 760
#define BM_TAG_CAT_COPY   761
#define BM_TAG_CAT_MOVE   762
#define BM_TAG_CAT_EFFECT 763
#define BM_TAG_CAT_SPEC   764
#define BM_TAG_CAT_BACK   765
#define BM_TAG_ORDER      768   // 「順番に選択」の ON/OFF
#define BM_TAG_WIDTH_SD   755   // 幅をゲージで指定して適用(Slide)
#define BM_TAG_QUANT_SD   756   // クオンタイズの分割数をゲージで指定
#define BM_TAG_DIV_SD     757   // 分割数(グリッド)をゲージで設定
static const MItem kCatSelect[] = {
    { BM_TAG_SEL_ALL, "ui-edit-icon12.png", "Select all" }, { BM_TAG_SEL_INV, "ui-edit-icon13.png", "Invert selection" },
    { BM_TAG_SEL_CLR, "ui-edit-icon21.png", "Deselect" }, { BM_TAG_LASSO, "ui-edit-icon12.png", "Lasso mode" },
    { BM_TAG_ORDER, "ui-edit-icon13.png", "Order select" },
    { BM_TAG_CAT_BACK, "ui-edit-icon15.png", "< Back" } };
static const MItem kCatCopy[] = {
    { BM_TAG_COPY, "ui-edit-icon8.png", "Copy selection" }, { BM_TAG_PASTE_MIR, "ui-edit-icon26.png", "Paste mirrored" },
    { BM_TAG_CAT_BACK, "ui-edit-icon15.png", "< Back" } };
static const MItem kCatMove[] = {
    { BM_TAG_MV_LATER, "ui-edit-icon14.png", "Move later" }, { BM_TAG_MV_EARL, "ui-edit-icon15.png", "Move earlier" },
    { BM_TAG_MV_LEFT, "ui-edit-icon21.png", "Move left" }, { BM_TAG_MV_RIGHT, "ui-edit-icon20.png", "Move right" },
    { BM_TAG_WIDTH, "ui-edit-icon28.png", "Apply width slider" }, { BM_TAG_WIDTH_SD, "ui-edit-icon28.png", "Width..." },
    { BM_TAG_QUANT, "ui-edit-icon12.png", "Quantize to grid" }, { BM_TAG_QUANT_SD, "ui-edit-icon12.png", "Quantize..." },
    { BM_TAG_CAT_BACK, "ui-edit-icon15.png", "< Back" } };
static const MItem kCatEffect[] = {
    { BM_TAG_FX_COPY, "ui-edit-icon8.png", "Copy effects in range" }, { BM_TAG_FX_PASTE, "ui-edit-icon9.png", "Paste effects here" },
    { BM_TAG_FX_DEL, "ui-edit-icon25.png", "Delete effects in range" }, { BM_TAG_MARKERS, "ui-edit-icon1.png", "Effect markers" },
    { BM_TAG_CAT_BACK, "ui-edit-icon15.png", "< Back" } };
static const MItem kCatSpec[] = {
    { BM_TAG_SPEC_TOGGLE, "ui-edit-icon1.png", "Spectrogram ON/OFF" }, { BM_TAG_SPEC_MORE, "ui-edit-icon12.png", "Opacity..." },
    { BM_TAG_SPEC_OFFSET, "ui-edit-icon8.png", "Check offset" },
    { BM_TAG_CAT_BACK, "ui-edit-icon15.png", "< Back" } };
static BOOL is_cat_submenu_tag(int tag) {   // サブメニュー内の項目(押した後に元の「その他」へ戻す対象)
    return (tag >= 710 && tag <= 721) || (tag >= 723 && tag <= 729) || (tag >= 736 && tag <= 739) || (tag >= 755 && tag <= 757) || tag == 768;
}
static void cat_show(void *self, int cat) {
    switch (cat) {
        case BM_TAG_CAT_SELECT: menu_show_items(self, kCatSelect, (int)(sizeof kCatSelect / sizeof *kCatSelect)); break;
        case BM_TAG_CAT_COPY:   menu_show_items(self, kCatCopy, (int)(sizeof kCatCopy / sizeof *kCatCopy)); break;
        case BM_TAG_CAT_MOVE: {
            int n = (int)(sizeof kCatMove / sizeof *kCatMove);
            if (!lay_of(self)->slide) {                     // Key: 幅スライダー項目なし
                MItem t[14]; int m = 0;
                for (int i = 0; i < n; i++) if (kCatMove[i].tag != BM_TAG_WIDTH && kCatMove[i].tag != BM_TAG_WIDTH_SD) t[m++] = kCatMove[i];
                menu_show_items(self, t, m);
            } else menu_show_items(self, kCatMove, n);
            break;
        }
        case BM_TAG_CAT_EFFECT: menu_show_items(self, kCatEffect, (int)(sizeof kCatEffect / sizeof *kCatEffect)); break;
        case BM_TAG_CAT_SPEC:   menu_show_items(self, kCatSpec, (int)(sizeof kCatSpec / sizeof *kCatSpec)); break;
    }
}
static void sel_add_menu_items(void *self) {   // 「その他」メニューにカテゴリ項目を追加(エディタごとに1回)
    if (gSelMenuFor == self || !lay_of(self)) return;
    gSelMenuFor = self;
    void *tb = (uint8_t *)self + 1400;
    add_menu_item(tb, "ui-edit-icon12.png", BM_TAG_CAT_SELECT, "Select >");
    add_menu_item(tb, "ui-edit-icon8.png", BM_TAG_CAT_COPY, "Copy / Paste >");
    add_menu_item(tb, "ui-edit-icon14.png", BM_TAG_CAT_MOVE, "Move >");
    add_menu_item(tb, "ui-edit-icon1.png", BM_TAG_CAT_EFFECT, "Effect >");
    add_menu_item(tb, "ui-edit-icon26.png", BM_TAG_CAT_SPEC, "Spectrogram >");
    add_menu_item(tb, "ui-edit-icon28.png", BM_TAG_PDEV, "Plugin Dev...");
    add_menu_item(tb, "ui-edit-icon28.png", BM_TAG_OPTS, "Editor options...");
}

// ---- 汎用スライダーダイアログ(ゲーム標準の編集中断ダイアログ script_editpause.mui と同じ見た目: 中央のパネルにゲージ最大 2 本)----
// ゲージは LineProgressSprite(割合は +592 の float 0..1。描画のたびに読む)。値は min..max を自由に指定でき、
// ゲージ上のタッチ位置から決める。確定(OK)で onDone、取消(Cancel / 外側タップ)で onCancel。
#define BM_TAG_SD_BAR   342      // 342, 343 = ゲージ 1, 2
#define BM_TAG_SD_OK    344
#define BM_TAG_SD_CANCEL 345
typedef struct { NSString *label; double min, max, val, step; } SdRow;
static struct {
    void *panel, *lbl[2], *barbg[2], *bar[2], *str[2], *editor;
    int n; SdRow row[2];
    BOOL open, drag;
    void (^onChange)(int row, double v);
    void (^onDone)(void);
    void (^onCancel)(void);
} gSd;
static void sd_bind(void *self, void *layout) {
    memset(&gSd, 0, sizeof gSd);
    gSd.panel = fx_find(layout, "_sdpanel");
    for (int i = 0; i < 2; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "_sdlabel-%d", i + 1);  gSd.lbl[i] = fx_find(layout, nm);
        snprintf(nm, sizeof nm, "_sdbarbg-%d", i + 1);  gSd.barbg[i] = fx_find(layout, nm);
        snprintf(nm, sizeof nm, "_sdbar-%d", i + 1);    gSd.bar[i] = fx_find(layout, nm);
        snprintf(nm, sizeof nm, "_sdstr-%d", i + 1);    gSd.str[i] = fx_find(layout, nm);
    }
    gSd.editor = self;
}
static void sd_row_show(int i) {
    SdRow *r = &gSd.row[i];
    double span = r->max - r->min;
    double t = span > 0 ? (r->val - r->min) / span : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (gSd.bar[i]) ((void (*)(void *, float))VA(0x100166D10ULL))(gSd.bar[i], (float)(t * 100.0));   // LineProgressSprite の割合(0..100)
    if (gSd.str[i]) {
        char b[32];
        if (fabs(r->val - llround(r->val)) < 1e-9) snprintf(b, sizeof b, "%lld", (long long)llround(r->val));
        else snprintf(b, sizeof b, "%g", r->val);
        set_label_text(gSd.str[i], b);
    }
}
static void sd_open(void *ed, const SdRow *rows, int n, void (^onChange)(int, double), void (^onDone)(void), void (^onCancel)(void)) {
    if (!gSd.panel) { ed_toast_text(ed, "Slider dialog not available", 2); return; }
    if (n < 1) return;
    if (n > 2) n = 2;
    gEditor = ed; gSd.editor = ed;
    gSd.n = n;
    gSd.onChange = onChange; gSd.onDone = onDone; gSd.onCancel = onCancel;
    for (int i = 0; i < 2; i++) {
        BOOL on = i < n;
        if (on) { gSd.row[i] = rows[i]; if (gSd.lbl[i]) set_label_text(gSd.lbl[i], rows[i].label.UTF8String ?: ""); sd_row_show(i); }
    }
    for (int i = 0; i < 2; i++) {
        BOOL on = i < n;
        void *nodes[4] = { gSd.lbl[i], gSd.barbg[i], gSd.bar[i], gSd.str[i] };
        for (int k = 0; k < 4; k++) if (nodes[k]) VT(nodes[k], 336, void (*)(void *, int))(nodes[k], on);
    }
    VT(gSd.panel, 336, void (*)(void *, int))(gSd.panel, 1);
    gSd.open = YES; gSd.drag = NO;
}
static void sd_close(BOOL commit) {
    if (!gSd.open) return;
    gSd.open = NO; gSd.drag = NO;
    if (gSd.panel) VT(gSd.panel, 336, void (*)(void *, int))(gSd.panel, 0);
    void (^cb)(void) = commit ? gSd.onDone : gSd.onCancel;
    gSd.onChange = nil; gSd.onDone = nil; gSd.onCancel = nil;
    if (cb) cb();
}
typedef struct { float x, y; } BMV2;
static BOOL sd_bar_touch(int ev, int row, const float *pt) {   // ゲージ上のタッチ → 値(元の編集中断ダイアログと同じく sub_100166E3C で割合を求める)
    if (!gSd.open || row < 0 || row >= gSd.n || !gSd.bar[row]) return NO;
    void *bar = gSd.bar[row];
    ((void (*)(void *, const float *))VA(0x100166E3CULL))(bar, pt);   // タッチ位置(ワールド, float[2] へのポインタ)→ゲージ内の割合
    float pct = *(float *)((uint8_t *)bar + 1012);
    double t = pct / 100.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    SdRow *r = &gSd.row[row];
    double v = r->min + t * (r->max - r->min);
    if (r->step > 0) v = r->min + llround((v - r->min) / r->step) * r->step;
    if (v < r->min) v = r->min;
    if (v > r->max) v = r->max;
    r->val = v;
    os_log(L, "slider ev=%d row=%d pt=(%.1f,%.1f) pct=%.1f val=%g", ev, row, pt[0], pt[1], pct, v);
    sd_row_show(row);
    if (gSd.onChange) gSd.onChange(row, v);
    return YES;
}
// 汎用ラッパ(plugins.inc の GetUserSlider からも使う): rows = [{t,min,max,val,step}]、確定で done(values)、取消で cancel
static void bm_slider_dialog(void *ed, NSArray<NSDictionary *> *rows, void (^done)(NSArray<NSNumber *> *), void (^cancel)(void)) {
    SdRow r[2];
    int n = rows.count > 2 ? 2 : (int)rows.count;
    if (n < 1) { if (cancel) cancel(); return; }
    for (int i = 0; i < n; i++) {
        NSDictionary *d = rows[(NSUInteger)i];
        NSString *t = d[@"t"] ?: @"";
        if (t.length > 26) t = [t substringToIndex:26];
        r[i] = (SdRow){ t, [d[@"min"] doubleValue], [d[@"max"] doubleValue], [d[@"val"] doubleValue], [d[@"step"] doubleValue] };
        if (r[i].val < r[i].min) r[i].val = r[i].min;
        if (r[i].val > r[i].max) r[i].val = r[i].max;
    }
    int cnt = n;
    sd_open(ed, r, n, nil,
        ^{ NSMutableArray *v = [NSMutableArray array]; for (int i = 0; i < cnt; i++) [v addObject:@(gSd.row[i].val)]; if (done) done(v); },
        ^{ if (cancel) cancel(); });
}
// エディタの設定: 幅 / クオンタイズ分割 / グリッド分割
static NSArray *sel_effective(void *ed);
static void width_apply(void *ed, NSArray *sel, int w255);
static void sd_open_width(void *ed) {
    NSArray *sel = sel_effective(ed);
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    uint8_t *n0 = VT([sel[0] pointerValue], 704, uint8_t *(*)(void *))([sel[0] pointerValue]);
    int cur = n0 ? n0[129] : 24;
    if (cur < 1) cur = 1;
    SdRow r = { @"Width (1-255)", 1, 255, cur, 1 };
    sd_open(ed, &r, 1, nil, ^{ width_apply(ed, sel, (int)lround(gSd.row[0].val)); }, nil);
}
static void sd_open_hs(void *ed) {   // Taiko: 選択したノートへ hs を適用(値は文字入力)
    if (!t_sel_nodes(ed).count) { ed_toast_text(ed, "Select notes first (More > Select mode)", 4); return; }
    bm_input_dialog(ed, @"HS value (e.g. 1.5)", @"1", ^(NSString *t) {
        double v;
        if (!pl_numeric([t stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet], &v) || v <= 0 || v > 1000) {
            ed_toast_text(ed, "Invalid HS value", 2);
            return;
        }
        t_apply_hs(ed, v);
    });
}
static void sd_open_hs_ramp(void *ed) {   // Taiko: 選択の最初〜最後のノートの間で hs を増減(入力: 開始,終了[,曲線])
    if (t_sel_nodes(ed).count < 2) { ed_toast_text(ed, "Select the start and end notes (More > Select mode)", 4); return; }
    bm_input_dialog(ed, @"HS ramp: start,end[,curve l/i/o/s/e]", @"1,2", ^(NSString *t) {
        NSArray<NSString *> *parts = [[t stringByReplacingOccurrencesOfString:@" " withString:@""] componentsSeparatedByString:@","];
        double v0, v1;
        if (parts.count < 2 || !pl_numeric(parts[0], &v0) || !pl_numeric(parts[1], &v1) || v0 <= 0 || v1 <= 0 || v0 > 1000 || v1 > 1000) {
            ed_toast_text(ed, "Invalid: e.g. 1,3 or 1,3,s", 2);
            return;
        }
        char c = 'l';
        if (parts.count >= 3 && parts[2].length) {
            c = (char)tolower([parts[2] characterAtIndex:0]);
            if (!strchr("liose", c)) { ed_toast_text(ed, "Curve: l i o s e", 2); return; }
        }
        t_apply_hs_ramp(ed, v0, v1, c);
    });
}
static void sd_open_hs_mul(void *ed) {   // Taiko: 選択範囲の hs に倍率をかける
    if (!t_sel_nodes(ed).count) { ed_toast_text(ed, "Select notes first (More > Select mode)", 4); return; }
    bm_input_dialog(ed, @"HS multiply factor (e.g. 1.5)", @"1.5", ^(NSString *t) {
        double f;
        if (!pl_numeric([t stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet], &f) || f <= 0 || f > 1000) { ed_toast_text(ed, "Invalid factor", 2); return; }
        t_hs_range_op(ed, 1, f);
    });
}
static void sd_open_quant(void *ed) {
    NSArray *sel = sel_effective(ed);
    if (![sel count]) { ed_toast_lv(ed, 2028, 2); return; }
    int cur = cur_div(ed);
    SdRow r = { @"Quantize division (1/N)", 1, 96, cur < 1 ? 4 : cur, 1 };
    sd_open(ed, &r, 1, nil, ^{ quantize_selection(ed, (int)lround(gSd.row[0].val)); }, nil);
}
static void sd_open_grid(void *ed) {
    int prev = cur_div(ed); if (prev < 1) prev = 4;
    SdRow r = { @"Grid divide (1/N)", 1, 96, prev, 1 };
    sd_open(ed, &r, 1,
        ^(int i, double v) { (void)i; div_step(ed, (int)lround(v) - cur_div(ed)); },
        nil,
        ^{ div_step(ed, prev - cur_div(ed)); });
}
// 濃さ(スペクトログラム)
static void sd_open_opacity(void *ed) {
    int prev = gSpecAlpha;
    SdRow r = { @"Spectrogram opacity (%)", 10, 100, (double)(gSpecAlpha * 100 / 255), 1 };
    sd_open(ed, &r, 1,
        ^(int i, double v) { (void)i; gSpecAlpha = (int)lround(v * 255.0 / 100.0); },
        ^{ [NSUserDefaults.standardUserDefaults setInteger:gSpecAlpha forKey:@"bm.spec.alpha"]; },
        ^{ gSpecAlpha = prev; });
}

static void fx_kind_cb(void *owner, void *dlg, int id) {   // 種類ダイアログ: 400..403 = Scroll / HS / Jump / Sign
    (void)owner; (void)dlg;
    if (id < 400 || id > 403) return;
    fx_set_kind(id - 400);
    if (gFx.sel >= 0 && gFx.sel < (int)eff_count())          // 選択行があれば、その種類の値を表示
        tb_set(gFx.tb[0], fmt_opt(eff_at(gFx.sel), FX_FLAG[gFx.kind], FX_VAL[gFx.kind]));
}
static int mod_is_node(void *node);   // modules.inc: 1 = プラグインの描画物 / 0 = 違う(描画物があるときだけ -1 以外)
static int gZmTag; static CFAbsoluteTime gZmTagT;   // ズームのボタンの押下(下の zm_filter が参照)
static intptr_t hook_touch(void *self, int ev, void *node, void *pos) {
    if (ev != 7 && is_key_editor(self)) un_mark();
    if (gPlROn && ev != 7 && node) {            // 診断: 一覧を開いている間のノードへのタッチ(タグ / 種類 / 位置)
        const float *pp = (const float *)pos;
        os_log(L, "rtouch ev=%d tag=%d cls=%{public}s node=%p list=%p pos=(%.0f,%.0f)", ev,
               ((int (*)(void *))(*(void ***)node)[0x2a0 / 8])(node), pl_cls(node), node, gPlRList, pp ? pp[0] : -1, pp ? pp[1] : -1);
    }
    if (node && ev != 7 && lay_of(self)) {   // プラグインの描画物(つまみなど)の上のタッチは、レーン(編集領域)へのタッチとして扱う
        int isMod = mod_is_node(node);
        if (isMod > 0) node = *(void **)((uint8_t *)self + 1032);
    }
    if (self != gEditor && gPreview) gPreview = NO;   // 別のエディタ(再入場)では preview を解除
    if (lay_of(self) && !gMenuSwapped) sel_add_menu_items(self);
    else if (is_taiko(self) && !gMenuSwapped) taiko_add_menu_items(self);
    if (node && ev != 7) {
        int tag = ((int (*)(void *))(*(void ***)node)[0x2a0 / 8])(node);
        if (taiko_menu_click(self, node, ev, tag)) return 0;
        if (tag == BM_TAG_THR && is_taiko(self)) { if (ev == 3) { gEditor = self; sd_open_hs_ramp(self); } return 0; }
        if (tag == BM_TAG_THM && is_taiko(self)) { if (ev == 3) { gEditor = self; sd_open_hs_mul(self); } return 0; }
        if (tag == BM_TAG_THC && is_taiko(self)) { if (ev == 3) { gEditor = self; t_hs_range_op(self, 0, 1.0); } return 0; }
        if (tag == BM_TAG_THS && is_taiko(self)) { if (ev == 3) { gEditor = self; sd_open_hs(self); } return 0; }
        // 選択メニューが差し替わったまま閉じられた場合は元に戻す(項目自身のクリック処理中は除く)
        if (gMenuSwapped && ev == 3 && is_cat_submenu_tag(tag)) {                // サブメニュー項目: 処理後(非同期)に元の「その他」へ戻す
            dispatch_async(dispatch_get_main_queue(), ^{ menu_restore(); });
        }
        if (gMenuSwapped && tag != BM_TAG_MORE_BPM && tag != BM_TAG_MORE_FX && tag != BM_TAG_PLUGINS && tag != BM_TAG_FOLDER && tag != BM_TAG_CHOICE_BACK && !is_cat_submenu_tag(tag) && tag != BM_TAG_CAT_BACK && gMenuToolbar == (uint8_t *)self + 1400) {
            void *pop = menu_popup(gMenuToolbar);
            int vis = pop ? VT(pop, 344, int (*)(void *))(pop) : 0;
            if (!vis) menu_restore();
        }
        if (tag >= BM_TAG_INFO_VIDEO && tag <= BM_TAG_INFO_VIDEO + 2 && lay_of(self)) {   // 情報画面: Video / BGA / Skin
            if (ev == 3) info_pick(self, tag - BM_TAG_INFO_VIDEO);
            return 0;
        }
        if (tag >= BM_TAG_INFO_BAR_DN && tag <= BM_TAG_INFO_CLEAR + 2 && lay_of(self)) {   // 情報画面: 数値の増減 / 解除
            if (ev == 3) {
                gInfoEd = self;
                if (tag >= BM_TAG_INFO_CLEAR) info_clear(self, tag - BM_TAG_INFO_CLEAR);
                else info_step(self, 3 + (tag - BM_TAG_INFO_BAR_DN) / 2, ((tag - BM_TAG_INFO_BAR_DN) & 1) ? +1 : -1);
            }
            return 0;
        }
        if ((tag == BM_TAG_UNDO || tag == BM_TAG_REDO) && (lay_of(self) || is_taiko(self))) {   // 右バーの取り消し / やり直し
            if (ev == 3) { gEditor = self; un_do(self, tag == BM_TAG_UNDO); }
            return 0;
        }
        if (tag == BM_TAG_PDEV && (lay_of(self) || is_taiko(self))) {             // 「その他 > Plugin Dev」: ログ / Lua コンソール
            if (ev == 3) { gEditor = self; os_log(L, "plugin dev: open"); dispatch_async(dispatch_get_main_queue(), ^{ pl_dev_menu(); }); }
            return 0;
        }
        if (tag == BM_TAG_OPTS && (lay_of(self) || is_taiko(self))) {             // 「その他 > Editor options」: オプトインの設定
            if (ev == 3) { gEditor = self; dispatch_async(dispatch_get_main_queue(), ^{ bm_opts_menu(); }); }
            return 0;
        }
        if (tag == BM_TAG_FOLDER && (lay_of(self) || is_taiko(self))) {           // 「その他 > Folder」: 曲のフォルダを開く
            if (ev == 3) {
                gEditor = self;
                dispatch_async(dispatch_get_main_queue(), ^{
                    menu_restore();
                    bm_open_song_folder(self);
                    // 項目を押すと右メインメニューが畳まれるので、パネルを閉じるときと同じ手順で元へ戻す
                    void *menu = *(void **)((uint8_t *)self + 992);
                    void *a2 = ((void *(*)(void *, int))VA(0x1005470B4ULL))(menu, 1);
                    ((void (*)(void *, int))VA(0x10053CF04ULL))(a2, 0);
                    *((uint8_t *)self + 902) = 1;
                });
            }
            return 0;
        }
        if (tag == 772 && (lay_of(self) || is_taiko(self))) {                     // 右メニューの Plugins ボタン: 実行一覧の表示 / 非表示
            if (ev == 3) { gEditor = self; pl_right_toggle(); }
            return 0;
        }
        if (tag == BM_TAG_PLUGINS && (lay_of(self) || is_taiko(self))) {          // 「その他 > Plugins」: プラグイン管理パネルを開く
            if (ev == 3) { gEditor = self; dispatch_async(dispatch_get_main_queue(), ^{ menu_restore(); pl_open(self); }); }
            return 0;
        }
        if (tag >= BM_TAG_PL_CH0 && tag < BM_TAG_PL_CH0 + 4 && (lay_of(self) || is_taiko(self))) {     // GetUserSelect / GetUserOption の選択肢
            if (ev == 3) { gEditor = self; int ci = tag - BM_TAG_PL_CH0; dispatch_async(dispatch_get_main_queue(), ^{ pl_choice(self, ci); }); }
            return 0;
        }
        if ((tag == BM_TAG_PL_INOK || tag == BM_TAG_PL_INBOX) && (lay_of(self) || is_taiko(self))) {   // GetUserInput の入力欄 / 確定
            if (ev == 3) {
                gEditor = self;
                if (tag == BM_TAG_PL_INOK) dispatch_async(dispatch_get_main_queue(), ^{ pl_input_ok(self); });
                else pl_input_edit();
            }
            return 0;
        }
        if (tag == BM_TAG_PL_IMPORT && (lay_of(self) || is_taiko(self))) {
            if (ev == 3) { gEditor = self; pl_import_pick(self); }
            return 0;
        }
        if ((tag == BM_TAG_PL_RUN || tag == BM_TAG_PL_RELOAD) && (lay_of(self) || is_taiko(self))) {
            if (ev == 3) {
                gEditor = self;
                if (tag == BM_TAG_PL_RELOAD) { pl_scan(); gPl.sel = -1; pl_refresh_list(); pl_info_text(); }
                else if (gPl.sel >= 0 && gPl.sel < (int)gPlFiles.count) {
                    int idx = gPl.sel;
                    // パネルは開いたまま実行する(GetUserInput の入力欄をそのまま使える)。入力待ちが無ければ実行後に閉じる
                    dispatch_async(dispatch_get_main_queue(), ^{
                        pl_run(self, (NSUInteger)idx);
                        if (!gPlInQ.count) pl_close();
                    });
                } else ed_toast_text(self, "Plugin: select one first", 2);
            }
            return 0;
        }
        if (tag >= BM_TAG_CAT_SELECT && tag <= BM_TAG_CAT_SPEC && lay_of(self)) {   // カテゴリ項目: 中身のサブメニューを表示
            if (ev == 3) {
                gEditor = self;
                int cat = tag;
                dispatch_async(dispatch_get_main_queue(), ^{ cat_show(self, cat); });   // 項目のクリック処理が戻ってから差し替える
            }
            return 0;
        }
        if (tag == BM_TAG_CHOICE_BACK && (lay_of(self) || is_taiko(self))) {   // 選択メニューの Back: 元へ戻してメニューを閉じる
            if (ev == 3) dispatch_async(dispatch_get_main_queue(), ^{
                menu_restore(); void *tb = (uint8_t *)self + 1400; VT(menu_popup(tb), 336, void (*)(void *, int))(menu_popup(tb), 0);
                void *r = ((void *(*)(void *, int))VA(0x1005470B4ULL))(*(void **)((uint8_t *)self + 0x3e0), 1);     // BPM アイコンで畳んだ右メインメニューを戻す
                ((void (*)(void *, int))VA(0x10053CF04ULL))(r, 0);
            });
            return 0;
        }
        if (tag == BM_TAG_CAT_BACK && lay_of(self)) {   // 戻る: 元の「その他」を再表示
            if (ev == 3) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    menu_restore();
                    if (gMenuToolbar || YES) { void *tb = (uint8_t *)self + 1400; VT(menu_popup(tb), 336, void (*)(void *, int))(menu_popup(tb), 1); }
                });
            }
            return 0;
        }
        if (tag == BM_TAG_OPEN_FX) {
            if (ev == 3) {          // 左のFXボタン: プレビュー ON/OFF のみ
                gEditor = self;
                gOuter = *(void **)((uint8_t *)self + 0x2d8);
                if ((lay_of(self) || is_taiko(self)) && gOuter) {
                    preview_set(!gPreview);
                    nvec *ch = node_children(node);
                    if (ch->begin != ch->end) set_label_text(*ch->begin, gPreview ? "FX ON" : "FX");
                }
            }
            return 0;
        }
        if ((tag == BM_TAG_SEL_ALL || tag == BM_TAG_SEL_INV || tag == BM_TAG_SEL_CLR) && lay_of(self)) {
            if (ev == 3) {
                if (*((uint8_t *)self + 903)) return 0;    // 再生中などの編集不可状態
                if (tag == BM_TAG_SEL_ALL) sel_select_all(self);
                else if (tag == BM_TAG_SEL_INV) sel_invert(self);
                else { sel_snapshot_drop(); sel_clear(self); }
                sel_toast_count(self);
            }
            return 0;
        }
        if ((tag == BM_TAG_COPY || tag == BM_TAG_PASTE || tag == BM_TAG_PASTE_MIR) && lay_of(self)) {
            if (ev == 3 && !*((uint8_t *)self + 903)) {
                if (tag == BM_TAG_COPY) clip_copy(self);
                else clip_paste(self, tag == BM_TAG_PASTE_MIR);
            }
            return 0;
        }
        if (((tag >= BM_TAG_MV_LATER && tag <= BM_TAG_MV_RIGHT) || tag == BM_TAG_QUANT || tag == BM_TAG_DIV_UP || tag == BM_TAG_DIV_DN) && lay_of(self)) {
            if (ev == 3 && !*((uint8_t *)self + 903)) {
                gEditor = self;
                switch (tag) {
                    case BM_TAG_DIV_UP: div_step(self, +1); break;
                    case BM_TAG_DIV_DN: div_step(self, -1); break;
                    case BM_TAG_QUANT: quantize_selection(self, 0); break;
                    case BM_TAG_MV_LATER: move_selection(self, +1, 0); break;
                    case BM_TAG_MV_EARL:  move_selection(self, -1, 0); break;
                    case BM_TAG_MV_LEFT:  move_selection(self, 0, -1); break;
                    case BM_TAG_MV_RIGHT: move_selection(self, 0, +1); break;
                }
            }
            return 0;
        }
        if (tag >= BM_TAG_SPD_UP && tag <= 735 && lay_of(self)) {
            if (ev == 3) {
                gEditor = self;
                int cur = speed_pct(self);
                if (tag == BM_TAG_SPD_UP) speed_set(self, cur + 5);
                else if (tag == BM_TAG_SPD_DN) speed_set(self, cur - 5);
                else speed_set(self, spd_preset(tag));
            }
            return 0;
        }
        if (tag == BM_TAG_SPEC_OFFSET && lay_of(self)) {
            if (ev == 3) { gEditor = self; spec_offset_check(self); }
            return 0;
        }
        if (tag >= BM_TAG_SPEC_TOGGLE && tag <= BM_TAG_SPEC_LESS && lay_of(self)) {
            if (ev == 3) {
                gEditor = self;
                char buf[48];
                if (tag == BM_TAG_SPEC_TOGGLE) {
                    gSpecOn = !gSpecOn;
                    if (gSpecOn && gSpecState == 3) gSpecState = 0;     // 失敗していたら再試行
                    ed_toast_text(self, gSpecOn ? "Spectrogram: ON" : "Spectrogram: OFF", 4);
                } else if (tag == BM_TAG_SPEC_MORE) {            // 「Opacity...」: ゲージのパネルを開く
                    dispatch_async(dispatch_get_main_queue(), ^{ menu_restore(); sd_open_opacity(self); });
                } else {
                    gSpecAlpha += (tag == BM_TAG_SPEC_MORE) ? 25 : -25;
                    if (gSpecAlpha < 25) gSpecAlpha = 25;
                    if (gSpecAlpha > 255) gSpecAlpha = 255;
                    [NSUserDefaults.standardUserDefaults setInteger:gSpecAlpha forKey:@"bm.spec.alpha"];
                    snprintf(buf, sizeof buf, "Spectrogram opacity: %d%%", gSpecAlpha * 100 / 255);
                    ed_toast_text(self, buf, 4);
                }
            }
            return 0;
        }
        if (tag == BM_TAG_ORDER && lay_of(self)) {   // 順番に選択(タップ順に記録。OFF にしても順番は残る / 何も無い所のタップでリセット)
            if (ev == 3) {
                if (!gOrdNotes) gOrdNotes = [NSMutableArray array];
                gOrdMode = !gOrdMode; gOrdEd = self;
                if (gOrdMode) ord_normal_reset(self);
                ed_toast_text(self, gOrdMode ? "Order select: ON (tap slides in order)" : "Order select: OFF (order kept)", 4);
            }
            return 0;
        }
        if (tag == BM_TAG_LASSO && lay_of(self)) {
            if (ev == 3) {
                gLasso = !gLasso; gLassoActive = NO; gLassoPts = nil;
                lasso_tool_highlight(self, !gLasso);
                ed_toast_text(self, gLasso ? "Lasso mode: ON (draw around notes)" : "Lasso mode: OFF", 4);
            }
            return 0;
        }
        if ((tag == BM_TAG_WIDTH_SD || tag == BM_TAG_QUANT_SD || tag == BM_TAG_DIV_SD) && lay_of(self)) {
            if (ev == 3 && !*((uint8_t *)self + 903)) {
                gEditor = self;
                dispatch_async(dispatch_get_main_queue(), ^{
                    menu_restore();
                    if (tag == BM_TAG_WIDTH_SD) { if (lay_of(self)->slide) sd_open_width(self); else ed_toast_text(self, "Width: Slide mode only", 2); }
                    else if (tag == BM_TAG_QUANT_SD) sd_open_quant(self);
                    else sd_open_grid(self);
                });
            }
            return 0;
        }
        if (tag == BM_TAG_WIDTH && lay_of(self)) {
            if (ev == 3 && !*((uint8_t *)self + 903) && lay_of(self)->slide) { gEditor = self; width_prompt(self); }
            return 0;
        }
        if (tag == BM_TAG_MARKERS && lay_of(self)) {
            if (ev == 3) {
                gMarkers = !gMarkers;
                ed_toast_text(self, gMarkers ? "Effect markers: ON" : "Effect markers: OFF", 4);
            }
            return 0;
        }
        if ((tag == BM_TAG_FX_COPY || tag == BM_TAG_FX_PASTE || tag == BM_TAG_FX_DEL) && lay_of(self)) {
            if (ev == 3 && !*((uint8_t *)self + 903)) fx_range_op(self, tag);
            return 0;
        }
        if (tag == BM_TAG_BPM_ICON && (lay_of(self) || is_taiko(self))) {          // BPM アイコン: 選択メニュー(BPM / Effect)
            if (ev == 3) {
                gEditor = self;
                // 元の処理と同様に右メインメニューを畳む
                void *p1 = *(void **)((uint8_t *)self + 0x3e0);
                void *r = ((void *(*)(void *, int))VA(0x1005470B4ULL))(p1, 1);
                ((void (*)(void *, int))VA(0x10053CF04ULL))(r, 1);
                menu_show_choice(self);
            }
            return 0;
        }
        if (tag >= BM_TAG_SD_BAR && tag <= BM_TAG_SD_CANCEL && lay_of(self)) {   // スライダーダイアログ
            if (tag < BM_TAG_SD_OK) {
                if (pos && (ev == 0 || ev == 1 || ev == 2 || ev == 3)) sd_bar_touch(ev, tag - BM_TAG_SD_BAR, (const float *)pos);
                return 1;                     // 消費して、続く移動 / 終了イベントを受け取る
            }
            if (ev == 3) sd_close(tag == BM_TAG_SD_OK);
            return 0;
        }
        if ((tag >= 310 && tag <= 313) || tag == 317 || tag == 38) {            // Effect パネルのボタン/入力欄
            if (ev == 3) {
                if (tag == 310) fx_apply();
                else if (tag == 311) fx_remove();
                else if (tag == 312 || tag == 38) { fx_close(); pl_close(); sd_close(NO); }   // パネル外(=パネル自体)のタップで閉じる(BPMリストと同じ)
                else if (tag == 313) fx_edit_input();
                else if (tag == 317) {          // 種類の選択: ゲーム標準のダイアログ(Scroll / HS / Jump / Sign)
                    NSMutableArray *names = [NSMutableArray array];
                    for (int k = 0; k < 4; k++) [names addObject:[NSString stringWithFormat:@"%s%s", FX_KIND_NAME[k], k == gFx.kind ? "  *" : ""]];
                    pl_dlg_open(self, @"Effect type", names, 400, fx_kind_cb);
                }
            }
            return 0;
        }
        if (tag == BM_TAG_MORE_BPM) {
            if (ev == 3) {
                dispatch_async(dispatch_get_main_queue(), ^{ menu_restore(); });   // クリック処理が戻ってから元へ戻す
                ((void (*)(void *, int))VA(0x1000775DCULL))(self, 1);   // 元のBPMリストを開く
            }
            return 0;
        }
        if (tag == BM_TAG_MORE_FX) {
            if (ev == 3) {
                dispatch_async(dispatch_get_main_queue(), ^{ menu_restore(); });
                fx_open(self);   // BPMリストを再現した Effect パネル
            }
            return 0;
        }
    }
    if (ev == 3 && node && lay_of(self) && lay_of(self)->slide) {   // 標準の Wipe→Slide(タグ 90)は、選択に Wipe 以外が 1 つでも混ざると拒否する(3 個以上の Wipe が必要)。Wipe だけを選び直してから渡す
        int dt = ((int (*)(void *))(*(void ***)node)[0x2a0 / 8])(node);
        if (dt == 90) {
            void **sv = sel_vec(self);
            NSMutableArray<NSValue *> *wipes = [NSMutableArray array];
            long other = 0;
            for (void **q = (void **)sv[0]; q != (void **)sv[1]; q++) {
                uint8_t *n = VT(*q, 704, uint8_t *(*)(void *))(*q);
                if (n && (n[113] & 4)) [wipes addObject:[NSValue valueWithPointer:n]]; else other++;
            }
            os_log(L, "wipe2slide: selected wipes=%lu other=%ld", (unsigned long)wipes.count, other);
            if (other && wipes.count >= 3) {
                ((void (*)(void *))VA(0x1000C5954ULL))(self);               // 選択を全部外してから、Wipe だけ選び直す
                for (NSValue *w in wipes) { void *sp = sprite_for_note(self, w.pointerValue); if (sp) sel_add(self, sp); }
            }
        }
    }
    if (ev == 3 && node && lay_of(self)) {                // ズームのボタン(タグ 6 / 7)の押下を控える(直後に標準がズーム関数を呼ぶ)
        int zt = ((int (*)(void *))(*(void ***)node)[0x2a0 / 8])(node);
        if (zt == 6 || zt == 7) { gZmTag = zt; gZmTagT = CFAbsoluteTimeGetCurrent(); os_log(L, "zoom button: tag=%d", zt); }
    }
    BOOL popBefore = NO;
    if (ev == 3 && node && lay_of(self)) { void *pp = menu_popup((uint8_t *)self + 1400); popBefore = pp && VT(pp, 344, int (*)(void *))(pp); }
    intptr_t tr = orig_touch(self, ev, node, pos);
    if (ev == 3 && node && lay_of(self) && !popBefore) {     // 押した直後にツールメニューが開いた = More ボタン
        void *pp = menu_popup((uint8_t *)self + 1400);
        if (pp && VT(pp, 344, int (*)(void *))(pp) && gMoreNode != node) { gMoreNode = node; gMoreEd = self; gMoreApplied = -1; os_log(L, "more button learned: %p", node); }
    }
    if (gLasso && ev == 3 && node && lay_of(self)) {   // ノーツ種別のアイコン(押した後の選択中ツールのタグと一致)を押したら、Lasso を終えて配置へ戻る
        int tg = ((int (*)(void *))(*(void ***)node)[0x2a0 / 8])(node);
        if (tg != BM_TAG_LASSO && tg == *(int *)((uint8_t *)self + 1368)) {
            gLasso = NO; gLassoActive = NO; gLassoPts = nil;
            ed_toast_text(self, "Lasso mode: OFF", 4);
        }
    }
    return tr;
}

// ---- エディタの Effect マーカー(ゲーム内ラベル: エディタのノードへ直接追加。OS の UI は使わない) ----
// レーン外側の余白に、各 effect の位置へ「内容 > / < 内容」を表示し、上部に現在位置の有効値を表示する。
// 座標は editor 空間そのもの: y = contY + K*(G(b)-gG0) - off
static void *gMkEditor;                         // ラベルを載せたエディタ
static NSMutableArray<NSValue *> *gMkPool;      // ラベルノード(エディタの子)
static NSMutableArray<NSString *> *gMkText;     // 各ラベルの現在の文字列(変更時だけ更新)
static void *gMkStatus; static NSString *gMkStatusText;
static int gMkLeft = -1;                        // 現在の配置側(アンカー再設定の判定)
static void mk_reset(void) { gMkEditor = NULL; gMkPool = nil; gMkText = nil; gMkStatus = NULL; gMkStatusText = nil; gMkLeft = -1; }
static void *mk_new_label(void *ed, float fontH) {
    void *l = ((void *(*)(float))VA(0x1001F3608ULL))(fontH);
    VT(ed, 504, void (*)(void *, void *, int))(ed, l, 90000);   // addChild(child, zOrder)
    return l;
}
static void mk_set_text(void *lb, NSString *t, NSString *__strong *cache) {
    if (*cache && [*cache isEqualToString:t]) return;
    set_label_text(lb, t.UTF8String);
    *cache = t;
}
static void mk_hide_all(void) {
    for (NSValue *v in gMkPool) VT(v.pointerValue, 336, void (*)(void *, int))(v.pointerValue, 0);
    if (gMkStatus) VT(gMkStatus, 336, void (*)(void *, int))(gMkStatus, 0);
}
// 種別ごとの色分け(Scroll=青 / HS=橙 / Sign=緑 / Jump=赤 / Bar=灰 / GGT=紫)。1 effect の中の項目は別ラベルで横に並べる。
typedef struct { NSString *text; uint8_t r, g, b; } MkPart;
static int fx_parts(const uint8_t *e, MkPart *o) {
    int n = 0;
    if (!e[32]) o[n++] = (MkPart){ [NSString stringWithFormat:@"SC %g", *(double *)(e + 40)], 80, 170, 255 };
    if (!e[48]) o[n++] = (MkPart){ [NSString stringWithFormat:@"HS %g", *(double *)(e + 56)], 255, 160, 60 };
    if (!e[64]) o[n++] = (MkPart){ [NSString stringWithFormat:@"SIG %g", *(double *)(e + 72)], 110, 220, 110 };
    if (!e[88]) o[n++] = (MkPart){ [NSString stringWithFormat:@"JMP %g", *(double *)(e + 96)], 255, 90, 90 };
    if (!e[80]) o[n++] = (MkPart){ [NSString stringWithFormat:@"BAR %@", e[81] ? @"on" : @"off"], 190, 190, 190 };
    if (!e[104]) o[n++] = (MkPart){ e[105] ? @"GGT" : @"GGT-", 200, 120, 255 };
    return n;
}
static void mk_set_color(void *lb, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t c[8] = {0};
    ((void (*)(void *, long, long, long))VA(0x1005545B8ULL))(c, r, g, b);
    VT(lb, 1080, void (*)(void *, void *))(lb, c);
}
static void ovl_update(void *a1) {
    const Lay *l = lay_of(a1);
    if (!l) return;
    uint8_t *ed = (uint8_t *)a1;
    if (gSpdShownEd != a1 || gSpdShown != speed_pct(a1)) speed_highlight(a1);   // 速度メニューの強調を現在値に合わせる
    if (!gMarkers) { if (gMkEditor == a1) mk_hide_all(); return; }
    void *outer = *(void **)(ed + 0x2d8);
    if (!outer) return;
    deq *q = (deq *)((uint8_t *)outer + OFF_EFFQ);
    if (!deq_sane(q, 112)) return;
    if (gMkEditor != a1) { mk_reset(); gMkEditor = a1; gMkPool = [NSMutableArray array]; gMkText = [NSMutableArray array]; }
    float H0 = LF(a1, 128);
    if (H0 < 1) return;
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(ed + 672), "NormalFont_H") * 0.8f;
    if (fontH < 8) fontH = 8;
    void *cont = *(void **)(ed + 1472);
    float contX = VT(cont, 192, float (*)(void *))(cont);
    float contY = VT(cont, 208, float (*)(void *))(cont);
    float K = LF(a1, l->zoom) * LF(a1, l->base);
    float off = l->off >= 0 ? LF(a1, l->off) : 0.0f;
    float x0, x1;
    if (!l->slide) {
        int cnt = ed[1700]; if (cnt < 1) cnt = 1;
        x0 = contX + LF(a1, 1632);
        x1 = contX + LF(a1, 1632 + 4 * (cnt - 1)) + LF(a1, 1672);
    } else { x0 = contX; x1 = contX + LF(a1, 1696); }
    float W0 = LF(a1, 664);
    int left = (x0 >= fontH * 10 || x0 >= W0 - x1) ? 1 : 0;
    float ax[2] = { left ? 1.0f : 0.0f, 0.5f };
    float px = left ? x0 - 4 : x1 + 4;
    double pos = *(double *)(ed + 872);
    double sc = 1, hs = 1, sig = 4; int bar = 1;
    size_t n = q->size;
    int shown = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t *e = deq_at(q, i);
        if (!e) break;
        double b = beat_key(e);
        if (b <= pos + 1e-9) {
            if (!e[32]) sc = *(double *)(e + 40);
            if (!e[48]) hs = *(double *)(e + 56);
            if (!e[64]) sig = *(double *)(e + 72);
            if (!e[80]) bar = e[81];
        }
        float y = contY + K * (float)(G(b) - gG0) - off;
        if (y < -fontH || y > H0 + fontH) continue;
        MkPart parts[6];
        int np = fx_parts(e, parts);
        float cursor = px;
        for (int k = 0; k < np && shown < 90; k++) {
            while ((int)gMkPool.count <= shown) {
                [gMkPool addObject:[NSValue valueWithPointer:mk_new_label(a1, fontH)]];
                [gMkText addObject:@""];
            }
            void *lb = gMkPool[shown].pointerValue;
            NSString *t = parts[k].text;
            if (k == 0) t = left ? [t stringByAppendingString:@" >"] : [@"< " stringByAppendingString:t];   // レーンに最も近い項目に矢印
            NSString *cur = gMkText[shown];
            if (!cur || ![cur isEqualToString:t]) {
                mk_set_text(lb, t, &cur); gMkText[shown] = cur;
                mk_set_color(lb, parts[k].r, parts[k].g, parts[k].b);
            }
            if (gMkLeft != left) VT(lb, 296, void (*)(void *, float *))(lb, ax);
            float pp[2] = { cursor, y };
            VT(lb, 136, void (*)(void *, float *))(lb, pp);
            VT(lb, 336, void (*)(void *, int))(lb, 1);
            float *sz = VT(lb, 328, float *(*)(void *))(lb);
            float w = sz ? sz[0] : fontH * 5;
            cursor += left ? -(w + fontH * 0.6f) : (w + fontH * 0.6f);
            shown++;
        }
    }
    for (int i = shown; i < (int)gMkPool.count; i++) VT(gMkPool[i].pointerValue, 336, void (*)(void *, int))(gMkPool[i].pointerValue, 0);
    if (!gMkStatus) gMkStatus = mk_new_label(a1, fontH);
    if (gMkLeft != left) VT(gMkStatus, 296, void (*)(void *, float *))(gMkStatus, ax);
    NSString *st = [NSString stringWithFormat:@"SC %g  HS %g  SIG %g  BAR %@", sc, hs, sig, bar ? @"on" : @"off"];
    mk_set_text(gMkStatus, st, &gMkStatusText);
    float sp[2] = { px, H0 - fontH * 1.5f };
    VT(gMkStatus, 136, void (*)(void *, float *))(gMkStatus, sp);
    VT(gMkStatus, 336, void (*)(void *, int))(gMkStatus, 1);
    gMkLeft = left;
}


// ---- スペクトログラム(エディタのレーン上に半透明で重ねる) ----
// 音源(.ogg)を stb_vorbis でデコード → STFT(vDSP) → 対数周波数 96 帯 → 拍に合わせたタイル(RGBA)を作り、
// ゲームのテクスチャ/スプライト生成関数で Sprite 化してノートコンテナ(editor+1472)の背面へ追加する。
// 縦 = 時間(拍に比例。FX プレビュー中は G で歪める) / 横 = 周波数(左=低音)。
#define SPEC_BANDS 96
#define SPEC_TILE_BEATS 4    // タイルの拍数。タイル内は G を直線で近似するので、FX プレビュー(scroll 変化)での誤差を小さくするため短めに
#define SPEC_RPB 16
typedef struct { int frames; int bands; float hopSec; float centerSec; uint8_t *q; } BMSpec;   // centerSec = 窓の中心(N/2)の時刻。フレーム f の時刻 = f*hopSec + centerSec
static BMSpec *gSpec;                 // 計算済みデータ(メインスレッドで参照)
static volatile int gSpecState;       // 0=未 1=計算中 2=完了 3=失敗
static int gSpecAlpha = 200;          // 0..255 (既定 約78%。スペクトログラムを主役にする)
static void *gSpecEditor;
static NSMutableDictionary<NSNumber *, NSValue *> *gSpecTiles;   // タイル番号 → Sprite
static double gSpecOffsetMs;
static NSString *gSpecPath;
static uint8_t gSpecLut[256][4];

static BOOL bm_safe_read(const void *src, void *dst, size_t n) {
    vm_size_t got = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)src, n, (vm_address_t)dst, &got) == KERN_SUCCESS && got == n;
}
static NSString *spec_find_chart_path(void *obj, size_t span) {   // obj 内の std::string から「/」で始まり .mc で終わる文字列を探す
    uint8_t *b = obj;
    for (size_t off = 0; off + 24 <= span; off += 8) {
        uint8_t flag = b[off + 23];
        char tmp[600];
        size_t len = 0;
        if (flag & 0x80) {
            uint64_t ptr, l;
            if (!bm_safe_read(b + off, &ptr, 8) || !bm_safe_read(b + off + 8, &l, 8)) continue;
            if (l < 6 || l > 500 || ptr < 0x100000) continue;
            if (!bm_safe_read((void *)ptr, tmp, l + 1)) continue;
            len = l;
        } else {
            len = flag;
            if (len < 6 || len > 22) continue;
            memcpy(tmp, b + off, len); tmp[len] = 0;
        }
        if (tmp[0] != '/' || len < 6 || strcmp(tmp + len - 3, ".mc") != 0) continue;
        BOOL ok = YES;
        for (size_t i = 0; i < len; i++) if ((unsigned char)tmp[i] < 0x20) { ok = NO; break; }
        if (ok) return [NSString stringWithUTF8String:tmp];
    }
    return nil;
}

static void spec_init_lut(void) {
    static const struct { float t; int r, g, b; } c[] = { {0.00f, 0, 0, 0}, {0.20f, 20, 0, 70}, {0.40f, 140, 10, 90}, {0.60f, 230, 60, 20}, {0.80f, 255, 160, 20}, {1.00f, 255, 255, 200} };
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f; int k = 0;
        while (k < 4 && t > c[k + 1].t) k++;
        float u = (t - c[k].t) / (c[k + 1].t - c[k].t);
        gSpecLut[i][0] = (uint8_t)(c[k].r + (c[k + 1].r - c[k].r) * u);
        gSpecLut[i][1] = (uint8_t)(c[k].g + (c[k + 1].g - c[k].g) * u);
        gSpecLut[i][2] = (uint8_t)(c[k].b + (c[k + 1].b - c[k].b) * u);
        gSpecLut[i][3] = 255;
    }
}

static int spec_cmp_f(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : (x > y); }
static BMSpec *spec_compute(const char *path) {
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) { os_log(L, "spec: open failed %d", err); return NULL; }
    stb_vorbis_info inf = stb_vorbis_get_info(v);
    int ch = inf.channels; float sr = (float)inf.sample_rate;
    unsigned total = stb_vorbis_stream_length_in_samples(v);
    float *mono = calloc((size_t)total + 8192, sizeof(float));
    float *buf = malloc(sizeof(float) * 4096 * ch);
    size_t n = 0;
    for (;;) {
        int got = stb_vorbis_get_samples_float_interleaved(v, ch, buf, 4096 * ch);
        if (got <= 0) break;
        if (n + got > total + 8192) break;
        for (int i = 0; i < got; i++) { float a = 0; for (int c2 = 0; c2 < ch; c2++) a += buf[i * ch + c2]; mono[n + i] = a / ch; }
        n += got;
    }
    free(buf); stb_vorbis_close(v);
    // 窓長 = 時間方向のにじみ。44.1k/48k で 2048(約 43〜46ms)だと音の切れ目が遅れて残るので 1024(約 21〜23ms)にする
    const int N = sr > 32000 ? 1024 : 512, LOG2N = sr > 32000 ? 10 : 9, H = (int)lroundf(sr * 0.0058f);
    if (n < (size_t)N * 2 || H < 1) { free(mono); return NULL; }
    int frames = (int)((n - N) / H) + 1;
    int half = N / 2;
    float *win = malloc(sizeof(float) * N);
    vDSP_hann_window(win, N, vDSP_HANN_NORM);
    FFTSetup fs = vDSP_create_fftsetup(LOG2N, kFFTRadix2);
    float *fr = malloc(sizeof(float) * N), *re = malloc(sizeof(float) * half), *im = malloc(sizeof(float) * half), *mg = malloc(sizeof(float) * half);
    // 帯域 → ビン範囲
    float fmin = 50.0f, fmax = fminf(14000.0f, sr * 0.45f);
    int lo[SPEC_BANDS], hi[SPEC_BANDS]; float wgt[SPEC_BANDS];
    for (int b = 0; b < SPEC_BANDS; b++) {
        float f0 = fmin * powf(fmax / fmin, (float)b / SPEC_BANDS), f1 = fmin * powf(fmax / fmin, (float)(b + 1) / SPEC_BANDS);
        float binw = sr / N;
        int a = (int)ceilf(f0 / binw), z = (int)ceilf(f1 / binw) - 1;
        if (z >= a) { lo[b] = a; hi[b] = z; wgt[b] = 0; }
        else { float c = sqrtf(f0 * f1) / binw; int j = (int)floorf(c); lo[b] = j; hi[b] = -1; wgt[b] = c - j; }
        if (lo[b] < 1) lo[b] = 1;
        if (lo[b] > half - 2) lo[b] = half - 2;
        if (hi[b] > half - 1) hi[b] = half - 1;
    }
    float *db = malloc(sizeof(float) * (size_t)frames * SPEC_BANDS);
    for (int f = 0; f < frames; f++) {
        vDSP_vmul(mono + (size_t)f * H, 1, win, 1, fr, 1, N);
        DSPSplitComplex sc = { re, im };
        vDSP_ctoz((const DSPComplex *)fr, 2, &sc, 1, half);
        vDSP_fft_zrip(fs, &sc, 1, LOG2N, FFT_FORWARD);
        vDSP_zvabs(&sc, 1, mg, 1, half);
        for (int b = 0; b < SPEC_BANDS; b++) {
            float m;
            if (hi[b] >= lo[b]) { m = 0; for (int k = lo[b]; k <= hi[b]; k++) if (mg[k] > m) m = mg[k]; }
            else m = (1 - wgt[b]) * mg[lo[b]] + wgt[b] * mg[lo[b] + 1];
            db[(size_t)f * SPEC_BANDS + b] = 20.0f * log10f(m / N + 1e-6f);
        }
    }
    // 正規化(上位 99.5 パーセンタイル基準・70dB レンジ)
    size_t cnt = (size_t)frames * SPEC_BANDS, step = cnt > 200000 ? cnt / 200000 : 1, ns = cnt / step;
    float *sm = malloc(sizeof(float) * ns);
    for (size_t i = 0; i < ns; i++) sm[i] = db[i * step];
    qsort(sm, ns, sizeof(float), spec_cmp_f);
    float hiDb = sm[(size_t)(ns * 0.995)], loDb = hiDb - 70.0f;
    free(sm);
    BMSpec *sp = calloc(1, sizeof(BMSpec));
    sp->frames = frames; sp->bands = SPEC_BANDS; sp->hopSec = (float)H / sr; sp->centerSec = (float)(N / 2) / sr;
    sp->q = malloc(cnt);
    for (size_t i = 0; i < cnt; i++) {
        float t = (db[i] - loDb) / (hiDb - loDb);
        sp->q[i] = (uint8_t)(fminf(fmaxf(t, 0.f), 1.f) * 255.0f);
    }
    free(db); free(mono); free(win); free(fr); free(re); free(im); free(mg);
    vDSP_destroy_fftsetup(fs);
    os_log(L, "spec: %d frames x %d bands, hop %.1fms, range %.0f..%.0fdB", frames, SPEC_BANDS, sp->hopSec * 1000, loDb, hiDb);
    return sp;
}

static double spec_ms_of_beat(double beat);
// 譜面(メモリ上)の音源オフセット。保存関数 sub_1001D2CF4 が書き出す音源一覧 = chart+0xD0 の vector(88B 要素: +8 音源名 std::string /
//   +0x3C vol / +0x40 拍 / +0x54 offset(int))。chart = outer+8 なので outer+216 / +224。保存前の変更も反映される。取れなければ NAN
static double spec_live_offset(void *outer) {
    if (!outer) return NAN;
    uint8_t *b = *(uint8_t **)((uint8_t *)outer + 216), *en = *(uint8_t **)((uint8_t *)outer + 224);
    if (!b || en <= b || (en - b) % 88 || (en - b) / 88 > 16) return NAN;
    int32_t off = *(int32_t *)(b + 0x54);
    return (off > -600000 && off < 600000) ? (double)off : NAN;
}
static void spec_start(void *ed) {
    if (gSpecState == 1) return;
    void *outer = *(void **)((uint8_t *)ed + 0x2d8);
    NSString *mc = outer ? spec_find_chart_path(outer, 0x800) : nil;
    if (!mc) mc = spec_find_chart_path(ed, 0x800);
    if (!mc) { gSpecState = 3; ed_toast_text(ed, "Spectrogram: chart path not found", 2); os_log(L, "spec: chart path not found"); return; }
    NSData *js = [NSData dataWithContentsOfFile:mc];
    id root = js ? [NSJSONSerialization JSONObjectWithData:js options:0 error:nil] : nil;
    NSString *snd = nil; double off = 0;
    for (NSDictionary *n in [root isKindOfClass:NSDictionary.class] ? root[@"note"] : @[]) {
        if ([n isKindOfClass:NSDictionary.class] && [n[@"type"] intValue] == 1) { snd = n[@"sound"]; off = [n[@"offset"] doubleValue]; }
    }
    if (!snd) { gSpecState = 3; ed_toast_text(ed, "Spectrogram: no audio in chart", 2); return; }
    NSString *ogg = [[mc stringByDeletingLastPathComponent] stringByAppendingPathComponent:snd];
    double live = spec_live_offset(outer);
    os_log(L, "spec: offset file=%.0f memory=%.0f", off, live);   // memory(NaN でなければ)を使う
    gSpecOffsetMs = isnan(live) ? off : live;
    gSpecPath = ogg;
    gSpecState = 1;
    ed_toast_text(ed, "Spectrogram: analyzing...", 4);
    os_log(L, "spec: start %{public}@ offset=%.0f", ogg, off);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BMSpec *r = spec_compute(ogg.UTF8String);
        dispatch_async(dispatch_get_main_queue(), ^{
            gSpec = r; gSpecState = r ? 2 : 3;
            if (r) { deq *bq = BPMQ(); { char tb[400]; int o = 0; static const int bs[] = { 0, 64, 256, 600, 640, 700, 800, 1000, 1500, 2000 }; for (int q = 0; q < 10; q++) o += snprintf(tb + o, sizeof tb - o, " t(%d)=%.1f", bs[q], spec_ms_of_beat(bs[q])); os_log(L, "spec: time map offset=%.0f bpmCount=%lu%{public}s", gSpecOffsetMs, (unsigned long)(bq ? bq->size : 0), tb); } }
            if (!r) os_log(L, "spec: decode failed");
        });
    });
}

// 拍 → 音源上の時刻(ms)。BPM リスト(editor の outer+584)の拍と拍長から積算する
static double spec_ms_of_beat(double beat) {
    deq *q = BPMQ();
    if (!gOuter || !q->size) return beat * 500.0 - gSpecOffsetMs;
    // ゲーム自身の「拍 → 時刻」(BPM の変化・delay も含む)を使い、音源上の時刻 = 譜面の時刻 − offset(offset 正 = 音源が譜面の時刻 0 より遅れて始まる。mourning の実データでノート位置と音の立ち上がりが揃うのは −offset)
    uint8_t buf[24] = {0};
    long m = (long)floor(beat); double fr = beat - (double)m;
    long den = 16384, num = lround(fr * den);
    if (num >= den) { m += 1; num = 0; }
    *(int32_t *)(buf + 8) = (int32_t)m; *(int32_t *)(buf + 16) = (int32_t)num; *(int32_t *)(buf + 20) = (int32_t)den;
    return time_of(buf) - gSpecOffsetMs;
}

static void *spec_make_tile(void *ed, int k) {
    static BOOL lutReady;
    if (!lutReady) { spec_init_lut(); lutReady = YES; }     // ★これを呼ばないと全画素が透明(0,0,0,0)になる
    const int W = SPEC_BANDS, HH = SPEC_TILE_BEATS * SPEC_RPB;
    uint8_t *px = malloc((size_t)W * HH * 4);
    for (int r = 0; r < HH; r++) {
        // 画像の先頭行 = 最も後ろの拍(画面上側)
        double b0 = (double)(k + 1) * SPEC_TILE_BEATS - (r + 1.0) / SPEC_RPB, b1 = b0 + 1.0 / SPEC_RPB;
        double t0 = spec_ms_of_beat(b0), t1 = spec_ms_of_beat(b1);
        double f0 = (t0 / 1000.0 - gSpec->centerSec) / gSpec->hopSec, f1 = (t1 / 1000.0 - gSpec->centerSec) / gSpec->hopSec;   // 窓の中心の時刻で表示(窓の先頭で表示すると約 23ms 早く見える)
        int a = (int)ceil(f0), z = (int)ceil(f1) - 1;           // 窓の中心が行の時間範囲 [t0, t1) に入るフレーム
        if (z < a) a = z = (int)lround((f0 + f1) * 0.5);       // 行がホップより短いときは最寄りの 1 フレーム
        for (int b = 0; b < W; b++) {
            int m = 0;
            for (int f = a; f <= z; f++) {
                if (f < 0 || f >= gSpec->frames) continue;
                int v = gSpec->q[(size_t)f * W + b];
                if (v > m) m = v;
            }
            uint8_t *d = px + ((size_t)r * W + b) * 4;
            memcpy(d, gSpecLut[m], 4);
        }
    }
    struct { void *addr; size_t len; } mip = { px, (size_t)W * HH * 4 };
    void *tex = calloc(1, 0x30);                       // operator new(0x30) 相当(Apple は malloc ベース)
    ((void (*)(void *))VA(0x1005783E8ULL))(tex);                                                  // Texture2D ctor
    long ok = ((long (*)(void *, void *, int, int, long, long))VA(0x100578504ULL))(tex, &mip, 1, 2 /*RGBA8888*/, W, HH);
    free(px);
    if (!ok) { os_log(L, "spec: texture init failed"); return NULL; }
    // Sprite::createWithTexture 相当: new(0x3E0) + ctor(sub_10056ADCC) + initWithTexture(vtable+1344) + autorelease(sub_100588938)
    //   ★sub_10056CB50 は SpriteBatchNode(テクスチャ+容量)で単体のスプライトではない(子が無いと何も描かれない)
    void *sp = calloc(1, 0x3E0);
    ((void *(*)(void *))VA(0x10056ADCCULL))(sp);
    if (!VT(sp, 1344, long (*)(void *, void *))(sp, tex)) { os_log(L, "spec: sprite init failed"); return NULL; }
    ((void (*)(void *))VA(0x100588938ULL))(sp);
    float a0[2] = { 0, 0 };
    VT(sp, 296, void (*)(void *, float *))(sp, a0);
    VT(sp, 1032, void (*)(void *, int))(sp, gSpecAlpha);
    return sp;
}
static void *gSpecDbg;
// タイルはノートの容器(+1472)の子にせず、専用の入れ物(エディタ直下)へ載せる。容器の子に混ぜると、標準の処理が子の並び(ノートだけの前提)を
// 頼りにするため、タイルの数に応じて一定区間のノートが表示されたり消えたりした(FX プレビューを切った後など)。
// 入れ物の位置と拡縮は毎フレーム容器に合わせる(スクロール・レーンの縮小に追従)。
static void *gSpecHolder;
// 子ノードの zOrder フィールド(オフセット)を実行時に特定する: エディタ直下の子の並び(zOrder 昇順)に対して単調増加になる int フィールド
static int spec_z_offset(void *ed) {
    static int off = -2;
    if (off != -2) return off;
    off = -1;
    nvec *ch = node_children(ed);
    size_t n = (size_t)(ch->end - ch->begin);
    if (n < 4 || n > 400) return off;
    for (int o = 48; o < 400 && off < 0; o += 4) {
        int distinct = 0; BOOL ok = YES; int prev = INT32_MIN;
        for (void **p = ch->begin; p != ch->end; p++) {
            int v = *(int *)((uint8_t *)*p + o);
            if (v < prev) { ok = NO; break; }
            if (v != prev) distinct++;
            prev = v;
        }
        if (ok && distinct >= 3) off = o;
    }
    void *cont = *(void **)((uint8_t *)ed + 1472);
    os_log(L, "spec: zOrder field offset=%d container z=%d (children=%zu)", off, off >= 0 ? *(int *)((uint8_t *)cont + off) : 0, n);
    return off;
}
static void *spec_holder(void *ed) {
    if (gSpecHolder) return gSpecHolder;
    void *h = ((void *(*)(void))VA(0x100544A64ULL))();
    if (!h) return NULL;
    int zo = spec_z_offset(ed);
    void *cont = *(void **)((uint8_t *)ed + 1472);
    int z = (zo >= 0 && cont) ? *(int *)((uint8_t *)cont + zo) - 1 : -1;   // 容器の 1 つ後ろ(ノートの背面・グリッド / 背景の前面。従来の「容器の子・最背面」と同じ並び)
    VT(ed, 504, void (*)(void *, void *, int))(ed, h, z);
    gSpecHolder = h;
    return h;
}
static void spec_holder_sync(void *ed) {
    void *h = gSpecHolder, *cont = *(void **)((uint8_t *)ed + 1472);
    if (!h || !cont) return;
    float p[2] = { VT(cont, 192, float (*)(void *))(cont), VT(cont, 208, float (*)(void *))(cont) };
    VT(h, 136, void (*)(void *, float *))(h, p);
    *(float *)((uint8_t *)h + 64) = *(float *)((uint8_t *)cont + 64);      // scaleX / scaleY(レーン縮小で容器が縮むとき)
    *(float *)((uint8_t *)h + 68) = *(float *)((uint8_t *)cont + 68);
    uint8_t *b = (uint8_t *)h; b[168] = 1; b[368] = 1; b[300] = 1; b[437] = 1;   // 変換の再計算(縮小の処理と同じ印)
}
static void spec_add_to_container(void *ed, void *sp) {
    void *h = spec_holder(ed);
    if (!h) return;
    VT(h, 504, void (*)(void *, void *, int))(h, sp, 0);
}

static void spec_hide_all(void) {
    for (NSValue *v in gSpecTiles.allValues) VT(v.pointerValue, 336, void (*)(void *, int))(v.pointerValue, 0);
}
static void spec_update(void *ed) {
    const Lay *l = lay_of(ed);
    if (!l) return;
    if (gSpecEditor != ed) { gSpecDbg = NULL; gSpecHolder = NULL; gSpecEditor = ed; gSpecTiles = [NSMutableDictionary dictionary]; gSpecState = 0; gSpec = NULL; gSpecOn = NO; }
    if (!gSpecOn) { if (gSpecTiles.count) spec_hide_all(); return; }
    if (gSpecState == 0) { gOuter = *(void **)((uint8_t *)ed + 0x2d8); spec_start(ed); return; }
    if (gSpecState != 2 || !gSpec) return;
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    spec_holder_sync(ed);
    {   // 譜面のオフセットが変わったら(情報画面 / Check offset の適用)、タイルを作り直す
        double live = spec_live_offset(gOuter);
        if (!isnan(live) && live != gSpecOffsetMs) {
            os_log(L, "spec: offset changed %.0f -> %.0f", gSpecOffsetMs, live);
            gSpecOffsetMs = live;
            for (NSValue *v in gSpecTiles.allValues) VT(v.pointerValue, 608, void (*)(void *, int))(v.pointerValue, 1);   // removeFromParentAndCleanup
            [gSpecTiles removeAllObjects];
        }
    }
    uint8_t *e = (uint8_t *)ed;
    static int dbgN;
    float H0 = LF(ed, 128);
    float K = LF(ed, l->zoom) * LF(ed, l->base);
    float off = l->off >= 0 ? LF(ed, l->off) : 0.0f;
    if (K < 1e-3f || H0 < 1) { if ((dbgN++ % 120) == 0) os_log(L, "spec: bad K=%.4f H0=%.1f", K, H0); return; }
    float x0, xw;
    if (!l->slide) {
        int cnt = e[1700]; if (cnt < 1) cnt = 1;
        x0 = LF(ed, 1632);
        xw = LF(ed, 1632 + 4 * (cnt - 1)) + LF(ed, 1672) - x0;
    } else { x0 = 0; xw = LF(ed, 1696); }
    double pos = *(double *)(e + 872);
    double span = H0 / K;                        // 画面に入る拍数
    int kmin = (int)floor((pos - span * 0.6) / SPEC_TILE_BEATS), kmax = (int)floor((pos + span * 1.6) / SPEC_TILE_BEATS);
    if (kmin < -1) kmin = -1;
    int made = 0;
    for (NSNumber *kn in gSpecTiles.allKeys) {      // 範囲外は隠す
        int kk = kn.intValue;
        if (kk < kmin || kk > kmax) VT(gSpecTiles[kn].pointerValue, 336, void (*)(void *, int))(gSpecTiles[kn].pointerValue, 0);
    }
    for (int k = kmin; k <= kmax; k++) {
        NSValue *tv = gSpecTiles[@(k)];
        if (!tv) {
            if (made >= 2) continue;               // 1 フレームに作るタイルは 2 枚まで
            void *sp = spec_make_tile(ed, k);
            if (!sp) { os_log(L, "spec: tile %d create failed", k); continue; }
            spec_add_to_container(ed, sp);
            made++;
            tv = [NSValue valueWithPointer:sp]; gSpecTiles[@(k)] = tv;
        }
        void *sp = tv.pointerValue;
        double bs = (double)k * SPEC_TILE_BEATS, be = bs + SPEC_TILE_BEATS;
        double ys = K * (G(bs) - gG0) - off, ye = K * (G(be) - gG0) - off;
        float yLow = (float)fmin(ys, ye), h = (float)fabs(ye - ys);
        if (h < 0.5f) { VT(sp, 336, void (*)(void *, int))(sp, 0); continue; }
        float pp[2] = { x0, yLow };
        VT(sp, 136, void (*)(void *, float *))(sp, pp);
        ((float (*)(void *, float, float))VA(0x10054471CULL))(sp, xw, h);                         // 表示サイズに合わせて拡縮
        VT(sp, 1032, void (*)(void *, int))(sp, gSpecAlpha);
        VT(sp, 336, void (*)(void *, int))(sp, 1);
    }
}


// ---- オフセット検証: 強いアタックと拍グリッド(1/4拍)のズレを集計し、提案オフセットを示す ----
static double spec_beat_of_ms(double ms) {   // spec_ms_of_beat の逆写像(単調なので二分探索)
    double lo = -4.0, hi = 20000.0;
    for (int k = 0; k < 60; k++) {
        double mid = (lo + hi) * 0.5;
        if (spec_ms_of_beat(mid) < ms) lo = mid; else hi = mid;
    }
    return (lo + hi) * 0.5;
}
static void spec_offset_check(void *ed) {
    if (gSpecState != 2 || !gSpec) { ed_toast_text(ed, "Spectrogram: turn ON and wait for analysis", 3); return; }
    gOuter = *(void **)((uint8_t *)ed + 0x2d8);
    int F = gSpec->frames, B = gSpec->bands;
    float *flux = malloc(sizeof(float) * F);
    float mx = 1e-9f;
    flux[0] = 0;
    for (int f = 1; f < F; f++) {
        const uint8_t *a = gSpec->q + (size_t)f * B, *p = a - B;
        int s = 0;
        for (int b = 0; b < B; b++) { int d = (int)a[b] - (int)p[b]; if (d > 0) s += d; }
        flux[f] = (float)s;
        if (flux[f] > mx) mx = flux[f];
    }
    // 上位 7% を閾値に(サンプルから推定)
    size_t step = F > 20000 ? F / 20000 : 1, ns = F / step;
    float *sm = malloc(sizeof(float) * ns);
    for (size_t i = 0; i < ns; i++) sm[i] = flux[i * step];
    qsort(sm, ns, sizeof(float), spec_cmp_f);
    float thr = sm[(size_t)(ns * 0.93)];
    free(sm);
    double here = *(double *)((uint8_t *)ed + 872);
    NSMutableArray<NSNumber *> *all = [NSMutableArray array], *loc = [NSMutableArray array];
    for (int f = 1; f < F - 1; f++) {
        if (flux[f] < thr || flux[f] < flux[f - 1] || flux[f] <= flux[f + 1]) continue;
        double ms = (((double)f - 0.5) * gSpec->hopSec + gSpec->centerSec) * 1000.0;   // 増加量はフレーム f-1 と f の間 → 両者の中心の中点
        double b = spec_beat_of_ms(ms);
        if (b < 0) continue;
        double g = round(b * 4.0) / 4.0;
        double dev = ms - spec_ms_of_beat(g);       // 音の時刻 - 最寄りグリッドの時刻
        [all addObject:@(dev)];
        if (fabs(b - here) <= 32.0) [loc addObject:@(dev)];
    }
    free(flux);
    NSArray *(^med)(NSArray<NSNumber *> *) = ^NSArray *(NSArray<NSNumber *> *a) {
        if (a.count < 8) return nil;
        NSArray *s2 = [a sortedArrayUsingSelector:@selector(compare:)];
        double m = [s2[s2.count / 2] doubleValue], sum = 0;
        for (NSNumber *n in a) sum += ([n doubleValue] - m) * ([n doubleValue] - m);
        return @[@(m), @(sqrt(sum / a.count)), @(a.count)];
    };
    NSArray *ra = med(all), *rl = med(loc);
    char buf[160];
    if (!ra) { ed_toast_text(ed, "Offset check: not enough onsets", 3); return; }
    double m = [ra[0] doubleValue];
    int sug = (int)lround(gSpecOffsetMs - m);      // 音源上の時刻 = 譜面の時刻 − offset なので、音がグリッドより m だけ遅いなら offset を m 減らす
    gSpecSuggest = sug;
    if (rl) snprintf(buf, sizeof buf, "Offset: all %+.0fms (n=%d)  here %+.0fms (n=%d)  -> %d", m, [ra[2] intValue], [rl[0] doubleValue], [rl[2] intValue], sug);
    else snprintf(buf, sizeof buf, "Offset: %+.0fms (sd %.0f, n=%d)  -> suggest %d", m, [ra[1] doubleValue], [ra[2] intValue], sug);
    ed_toast_text(ed, buf, 4);
    os_log(L, "offset: all median=%+.1fms sd=%.1f n=%d | local %@ | current=%.0f suggest=%d", m, [ra[1] doubleValue], [ra[2] intValue], rl ? [NSString stringWithFormat:@"%+.1f n=%d", [rl[0] doubleValue], [rl[2] intValue]] : @"-", gSpecOffsetMs, sug);
    // 後続(オフセット適用)のため、sound deque の先頭要素の中身を記録しておく
    deq *sq = (deq *)((uint8_t *)gOuter + 632);
    if (deq_sane(sq, 8) && sq->size) {
        uint8_t *e0 = deq_at(sq, 0);
        char hx[400]; hx[0] = 0;
        for (int i = 0; i < 48; i += 8) { char t[40]; snprintf(t, sizeof t, "%016llx ", *(unsigned long long *)(e0 + i)); strlcat(hx, t, sizeof hx); }
        os_log(L, "offset: sound deque size=%llu e0=%p words: %{public}s", (unsigned long long)sq->size, e0, hx);
    }
}


#include "modules.inc"
static void hook_refresh_inner(void *a1, float dt);
static void hook_refresh(void *a1, float dt) { hook_refresh_inner(a1, dt); if (lay_of(a1)) { kb_tick(a1); ovl_update(a1); spec_update(a1); mod_update(a1); un_tick(a1); pl_right_tick(a1); more_sync(a1); } }
static void hook_refresh_inner(void *a1, float dt) {   // sub_1001B0AF0 = 毎フレーム update(dt)
    if (gPreview && a1 == gEditor) {
        ((void (*)(void *, float))VA(0x100075D2CULL))(a1, dt);   // 元の先頭呼び出し(基底の位置更新)
        if (*((uint8_t *)a1 + 883)) {
            ed_apply_scroll(a1);
            *((uint8_t *)a1 + 883) = 0;
        }
        return;
    }
    orig_refresh(a1, dt);
}
// ---- ズームの上限を広げる(+5 段階): 標準の + ボタンが上限で止まったら、こちらで同じ比率だけ拡大を続ける ----
// 標準の段階は layout 側の表で決まる(上限 16 段)。ボタンのタグ(6 / 7)の押下の直後にズーム関数が呼ばれるので、そこで要求値を差し替える。縮小は足した分を先に戻す
#define ZM_EXTRA_MAX 5
static int gZmExtra, gZmTag;
static float gZmLimit, gZmStep = 0.1f;                    // 上限に当たったときの標準の値 / 標準の 1 段の幅
static CFAbsoluteTime gZmTagT;
// 拡大 = タグ 6 / 縮小 = タグ 7。標準は上限で値を変えない(要求値 = 上限)。上限に当たったら、標準と同じ幅で最大 ZM_EXTRA_MAX 段、要求値を差し替えて拡大を続ける。
// 縮小は足した分を先に戻す(標準は縮小で 1 段下がった値を要求してくるので、標準側の値を上限に戻す)
static float zm_filter(void *ed, float z) {
    const Lay *l = lay_of(ed);
    if (!l || CFAbsoluteTimeGetCurrent() - gZmTagT > 0.3 || !gZmTag) { gZmExtra = 0; return z; }   // ボタン以外(ピンチなど)の変更
    float z0 = LF(ed, l->zoom);
    uint8_t *lo = *(uint8_t **)((uint8_t *)ed + 1112);                 // layout(標準側のズーム値は +1444)
    if (z0 <= 0 || z <= 0 || !lo) return z;
    if (gZmTag == 6) {
        if (gZmExtra == 0 && z > z0 * 1.000001f) { gZmStep = fmaxf(z - z0, 0.01f); return z; }            // 標準の拡大
        if (gZmExtra == 0 && fabsf(z - z0) > 1e-6f * z0) return z;                                       // 想定外
        if (gZmExtra == 0) gZmLimit = z;                                                               // 上限に当たった
        if (gZmExtra < ZM_EXTRA_MAX) gZmExtra++;
        os_log(L, "zoom: extra %d -> %.4f", gZmExtra, gZmLimit + gZmStep * gZmExtra);
        return gZmLimit + gZmStep * gZmExtra;
    }
    if (gZmTag == 7 && gZmExtra > 0) {
        gZmExtra--;
        *(float *)(lo + 1444) = gZmLimit;                              // 標準側は上限のまま保つ(次の縮小・拡大の基準)
        os_log(L, "zoom: extra %d -> %.4f", gZmExtra, gZmLimit + gZmStep * gZmExtra);
        return gZmLimit + gZmStep * gZmExtra;
    }
    gZmExtra = 0;
    return z;
}
static intptr_t hook_zoom(void *a1, float z) {  // sub_1001B10C0 (zoom 変更: 子ノードを線形再スケール)
    intptr_t r = orig_zoom(a1, zm_filter(a1, z));
    if (gPreview && a1 == gEditor) ed_relayout(a1);
    return r;
}
// 順番の番号表示: 始点の位置に、黒丸に白い 1, 2, 3…(動画のイメージ)をモジュール(丸 = sprite / 番号 = text)で出す
static void ord_marks_refresh(void *ed) {
    for (BMMod *m in [gMods copy]) if ([m->name hasPrefix:@"bm-ord-"]) { mod_drop_node(m); [gMods removeObject:m]; }
    if (!gOrdNotes.count || !ed || !lay_of(ed) || !lay_of(ed)->slide) return;
    void *savedEd = gPlEd;
    gPlEd = ed;
    for (NSUInteger i = 0; i < gOrdNotes.count; i++) {
        uint8_t *n = (uint8_t *)[gOrdNotes[i] pointerValue];
        int den = *(int32_t *)(n + 64 + 12);
        double beat = *(int32_t *)(n + 64) + (den > 0 ? (double)*(int32_t *)(n + 64 + 8) / den : 0.0);
        char nm[32], cn[32], tx[16];
        snprintf(cn, sizeof cn, "bm-ord-c%lu", (unsigned long)i);
        snprintf(nm, sizeof nm, "bm-ord-%lu", (unsigned long)i);
        snprintf(tx, sizeof tx, "%lu", (unsigned long)i + 1);
        double mx = (n[128] + 0.5) * 100.0 / 256.0;
        mod_add(1, cn, "bm-ord-circle");                      // 黒丸(自前のピクセル。画像ファイルは使わない)
        BMMod *cm = mod_find([NSString stringWithUTF8String:cn]);
        if (cm) {
            const int S = 64;
            free(cm->rgba);
            cm->rgba = calloc((size_t)S * S, 4); cm->texW = cm->texH = S; cm->fallback = NO;
            for (int yy = 0; yy < S; yy++) for (int xx = 0; xx < S; xx++) {
                double d = hypot(xx + 0.5 - S / 2.0, yy + 0.5 - S / 2.0), a = fmin(1.0, fmax(0.0, S / 2.0 - 1.0 - d));   // 縁は 1px ぼかす
                uint8_t *px = cm->rgba + ((size_t)yy * S + xx) * 4;
                uint8_t v = d > S / 2.0 - 6.0 ? 255 : 0;                                                                // 外側 5px = 白の縁取り / 内側 = 黒
                px[0] = px[1] = px[2] = (uint8_t)lround(v * a); px[3] = (uint8_t)lround(a * 255.0);                    // premultiplied
            }
        }
        mod_set_num(cn, "x", mx); mod_set_num(cn, "beat", beat);
        {   // 正円にする: 高さ(Height)は 1080 を基準にした単位(レーン幅 / 1080 倍)、幅(Width)はレーン幅に対する % なので、直径 D から両方を同じ長さに換算する
            double D = 36.0;
            mod_set_num(cn, "width", D * 100.0 / 1080.0); mod_set_num(cn, "height", D);
        }
        mod_add(0, nm, tx);                                   // 白い番号(丸の上)
        mod_set_num(nm, "x", mx);
        mod_set_num(nm, "beat", beat);
        mod_set_color(nm, 255, 255, 255);
        BMMod *om = mod_find([NSString stringWithUTF8String:nm]); if (om) om->fontMul = 1.3;
    }
    gPlEd = savedEd;
}
// ---- 順番(Order)は通常の選択とは別のレイヤー ----
// Order で選んだノートは、標準の選択(editor+1560)には入れず、本体 = 赤 / ほか = 青の色(スプライトの tint)と番号で表す。通常の選択(lasso など)はそのまま独立して使え、
// GetSelectNotes と GetOrderNotes が混ざらない。標準の処理はタップしたノートを選択へ入れるので、その直後に選択から外し、見た目を通常へ戻してから色を付ける
static NSMutableSet<NSValue *> *gOrdTinted;       // 色を付けているノート
static NSMutableSet<NSValue *> *gOrdNormal;       // Order 中に控えている通常の選択(Order のタップと区別する)
static void tint_tree(void *node, BOOL on, int r, int g, int b, int depth) {
    if (!node || depth > 4) return;
    if (cast_to(node, 0x1009647B0ULL)) {           // cocos2d::Sprite
        if (on) { uint8_t col[32]; ((void (*)(void *, long, long, long))VA(0x1005545B8ULL))(col, r, g, b); VT(node, 1080, void (*)(void *, void *))(node, col); }
        else VT(node, 1080, void (*)(void *, void *))(node, (void *)VA(0x1016CC9F8ULL));   // 白 = 元の色
    }
    nvec *ch = node_children(node);
    if (ch && ch->begin && ch->end >= ch->begin && (ch->end - ch->begin) < 4096) for (void **p = ch->begin; p != ch->end; p++) tint_tree(*p, on, r, g, b, depth + 1);
}
static void ord_tint_sync(void *ed) {
    if (!ed || !lay_of(ed)) return;
    if (!gOrdTinted) gOrdTinted = [NSMutableSet set];
    for (NSValue *t in [gOrdTinted copy]) {
        if ([gOrdNotes containsObject:t]) continue;
        void *sp = sprite_for_note(ed, t.pointerValue);
        if (sp) tint_tree(sp, NO, 255, 255, 255, 0);
        [gOrdTinted removeObject:t];
    }
    for (NSUInteger i = 0; i < gOrdNotes.count; i++) {
        void *sp = sprite_for_note(ed, [gOrdNotes[i] pointerValue]);
        if (!sp) continue;
        if (i == 0) tint_tree(sp, YES, 255, 70, 70, 0); else tint_tree(sp, YES, 60, 170, 255, 0);
        [gOrdTinted addObject:gOrdNotes[i]];
    }
}
static void ord_normal_reset(void *ed) {           // Order を始める時点の通常の選択を控える
    gOrdNormal = [NSMutableSet set];
    if (!ed || !lay_of(ed)) return;
    void **v = sel_vec(ed);
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) { void *n = VT(*p, 704, void *(*)(void *))(*p); if (n) [gOrdNormal addObject:[NSValue valueWithPointer:n]]; }
}
static void ord_unsel(void *ed, void *sp) {         // 標準の選択解除(sub_1000C5954)と同じ手順で 1 つだけ外す: 選択から抜き、見た目(テクスチャ)を通常へ戻す
    uint8_t *e = (uint8_t *)ed;
    uint8_t *n = VT(sp, 704, uint8_t *(*)(void *))(sp);
    void **sv = sel_vec(ed);
    for (void **q = (void **)sv[0]; q != (void **)sv[1]; q++) {
        if (*q != sp) continue;
        memmove(q, q + 1, (size_t)((uint8_t *)sv[1] - (uint8_t *)(q + 1)));
        sv[1] = (uint8_t *)sv[1] - 8;
        break;
    }
    if (!n) return;
    void **tb = *(void ***)(e + 1200), **te = *(void ***)(e + 1208);
    long cnt = te - tb; uint32_t base = *(uint32_t *)(e + 1264);
    int fl = *(int *)(n + 112);
    if (fl & 0x800) {
        void *path = cast_to(sp, 0x100922780ULL);       // PathSpriteEditor
        uint32_t idx = base + 2;
        if (path) ((void (*)(void *, void *))VA(0x100167BCCULL))(path, (long)idx < cnt ? tb[idx] : NULL);
    } else {
        void *s3 = cast_to(sp, 0x10090BE50ULL);         // Scale3Sprite
        uint32_t idx = (fl & 0x400) ? base + 1 : base;
        if (s3) VT(s3, 1224, void (*)(void *, void *))(s3, (long)idx < cnt ? tb[idx] : NULL);
    }
}
// 標準の当たり判定は経路上の中継点(小さい丸)だけなので、選択から外した(見た目が通常に戻った)Order のスライドは狙いにくく、外すと「何も無い所のタップ」= リセットになってしまう。
// 何も選ばれなかったタップは、スライドの経路(丸の列)の近くかを調べ、最寄りのスライドを押したものとして扱う
static float gOrdMinD; static int gOrdNSlides;
static void *ord_hit_slide(void *ed, const void *pos) {
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    uint8_t *e = (uint8_t *)ed;
    gOrdMinD = 1e9f; gOrdNSlides = 0;
    if (!pos) return NULL;
    void *cont = *(void **)(e + 1472);
    V2Ret r = ((V2Ret (*)(void *, const void *))VA(0x100546980ULL))(cont, pos);   // 押した位置 → レーンの座標(標準のノート配置と同じ換算)
    double xw = LF(ed, 1696), K = (double)LF(ed, 1716) * (double)LF(ed, 1720);
    if (xw <= 0 || K <= 0) return NULL;
    double tb = r.v[1] / K, u = r.v[0] * 255.0 / xw;                                // 拍 / 横位置(0〜255)
    nvec *ch = node_children(cont);
    if (!ch) return NULL;
    void *best = NULL; double bs = 1e9;
    for (void **p = ch->begin; p != ch->end; p++) {
        uint8_t *n = VT(*p, 704, uint8_t *(*)(void *))(*p);
        if (!n || !(n[113] & 8)) continue;
        double head = note_beat(n, 64, 72, 76), end = note_beat(n, 88, 96, 100);
        uint8_t *sb = *(uint8_t **)(n + 136), *se = *(uint8_t **)(n + 144);
        long cnt = (se - sb) / 28;
        if (cnt < 0 || cnt > 4096) continue;
        double lastB = cnt ? head + note_beat(sb + 28 * (cnt - 1), 0, 8, 12) : head;
        double maxB = fmax(end, lastB);
        if (tb < head - 0.06 || tb > maxB + 0.06) continue;                        // 時間の範囲外
        gOrdNSlides++;
        double pb = head, px = 0, off = 0; BOOL found = NO;                        // 中継点の間は中心のずれを線形に補間
        for (long i = 0; i < cnt; i++) {
            double bb = head + note_beat(sb + 28 * i, 0, 8, 12), xx = (double)seg_x16(sb + 28 * i);
            if (tb <= bb) { double t = bb > pb ? (tb - pb) / (bb - pb) : 1.0; off = px + (xx - px) * t; found = YES; break; }
            pb = bb; px = xx;
        }
        if (!found) off = px;
        double center = n[128] + off, w = n[129];                                  // +128 は中心 x(標準の配置は、幅の半分で端に収めた中心を入れる)/ +129 は幅
        double d = fabs(u - center) - w / 2; if (d < 0) d = 0;
        if (d < gOrdMinD) gOrdMinD = (float)d;
        if (d > 6.0) continue;                                                     // 幅の外(余裕 6/255)
        double score = fabs(u - center);
        if (score < bs) { bs = score; best = n; }
    }
    return best;
}
// 「順番に選択」中: 標準の処理でタップしたノートが選択へ入った直後に、タップ順に控える(選択からは外す)。何も無い所をタップ = 順番をリセット
static void ord_track(void *ed, int ev, const void *pos) {
    if (!gOrdMode || ed != gOrdEd || ev != 3 || !lay_of(ed)) return;   // タップの終わりで判定(選択は押下〜離しの間に更新される)
    if (!gOrdNormal) ord_normal_reset(NULL);
    NSMutableArray<NSValue *> *fresh = [NSMutableArray array], *hit = [NSMutableArray array], *freshSp = [NSMutableArray array], *hitSp = [NSMutableArray array];
    NSUInteger total = 0;
    void **v = sel_vec(ed);
    for (void **p = (void **)v[0]; p != (void **)v[1]; p++) {
        void *note = VT(*p, 704, void *(*)(void *))(*p);
        if (!note) continue;
        total++;
        NSValue *nv = [NSValue valueWithPointer:note];
        if ([gOrdNotes containsObject:nv]) { [hit addObject:nv]; [hitSp addObject:[NSValue valueWithPointer:*p]]; }
        else if (![gOrdNormal containsObject:nv]) { [fresh addObject:nv]; [freshSp addObject:[NSValue valueWithPointer:*p]]; }
    }
    char b[64];
    os_log(L, "ord: tap total=%lu fresh=%lu hit=%lu order=%lu normal=%lu pos=(%.0f,%.0f)", (unsigned long)total, (unsigned long)fresh.count, (unsigned long)hit.count, (unsigned long)gOrdNotes.count, (unsigned long)gOrdNormal.count, pos ? ((const float *)pos)[0] : -1.f, pos ? ((const float *)pos)[1] : -1.f);
    if (fresh.count == 1) {                       // 新しく選んだ: タップ順に加える(1 番目 = 本体)
        [gOrdNotes addObject:fresh[0]];
        if (gOrdNotes.count == 1) snprintf(b, sizeof b, "Order 1 (main)");
        else snprintf(b, sizeof b, "Order %lu added", (unsigned long)gOrdNotes.count);
        ed_toast_text(ed, b, 4);
        [hit addObject:fresh[0]]; [hitSp addObject:freshSp[0]];
    } else if (!total) {                          // 標準では何も選ばれなかったタップ
        void *hn = ord_hit_slide(ed, pos);        // スライドの近く = そのスライドを押した扱い
        os_log(L, "ord: miss -> near slide=%p minDist=%.1f slides=%d tool=%d", hn, gOrdMinD, gOrdNSlides, *(int *)((uint8_t *)ed + 1368));
        if (hn) {
            NSValue *nv = [NSValue valueWithPointer:hn];
            if ([gOrdNotes containsObject:nv]) ed_toast_text(ed, "Already in order", 4);
            else {
                [gOrdNotes addObject:nv];
                if (gOrdNotes.count == 1) snprintf(b, sizeof b, "Order 1 (main)");
                else snprintf(b, sizeof b, "Order %lu added", (unsigned long)gOrdNotes.count);
                ed_toast_text(ed, b, 4);
            }
        } else if (gOrdNotes.count) {             // 何も無い所をタップ: 順番を全部リセット(editor.html / Slide Editor の Order と同じ)
            [gOrdNotes removeAllObjects];
            ed_toast_text(ed, "Order reset", 4);
        }
    } else if (hit.count == 1 && !fresh.count) {  // 順番に入っているノートを再タップ
        ed_toast_text(ed, "Already in order", 4);
    }
    for (NSValue *sp in hitSp) ord_unsel(ed, sp.pointerValue);   // Order のノートは通常の選択に残さない
    ord_normal_reset(ed);                         // いま残っている選択 = 通常の選択
    ord_tint_sync(ed);
    ord_marks_refresh(ed);
}
static intptr_t hook_keytouch(void *a1, int ev, void *a3, void *pos) {   // sub_1001B1804
    if (gPreview && a1 == gEditor) return 1;     // preview 中は編集操作を無効化
    if (lay_of(a1)) un_mark();
    if (pl_right_lane_map(a1, pos)) return 0;
    if (pl_touch(a1, ev, pos)) return 1;
    if (lasso_touch(a1, ev, pos)) return 1;
    pl_right_lane_unmap(pos);                // 縮んだレーンの座標戻しは自前の処理だけ。標準の処理には元の画面座標を渡す
    if (ev == 0 && lay_of(a1)) {
        void **v = sel_vec(a1);
        if (v[0] != v[1]) sel_snapshot_take(a1);   // 標準処理が選択を解除する直前に控える
        else {   // 空の状態でレーン上をタップ = 明示的な選択解除操作 → 控えを破棄
            int ln = ((int (*)(void *, void *))VA(0x1001B1D30ULL))(a1, pos);
            if (ln >= 0 && ln < *((uint8_t *)a1 + 1700)) sel_snapshot_drop();
        }
    }
    intptr_t r = orig_keytouch(a1, ev, a3, pos);
    ord_track(a1, ev, pos);
    return r;
}
static intptr_t hook_s_touch(void *a1, int ev, void *a3, void *pos) {   // sub_1000C6744 (EditorSlide)
    if (gPreview && a1 == gEditor) return 1;
    if (lay_of(a1)) un_mark();
    if (pl_right_lane_map(a1, pos)) return 0;
    if (pl_touch(a1, ev, pos)) return 1;
    if (lasso_touch(a1, ev, pos)) return 1;
    pl_right_lane_unmap(pos);                // 縮んだレーンの座標戻しは自前の処理だけ。標準の処理には元の画面座標を渡す
    if (ev == 0 && lay_of(a1)) sel_snapshot_take(a1);
    int selBefore = 0; uint8_t *selNote = NULL;
    if (ev == 0) {   // 診断: 選択中のスライドがあるとき、レーンの押下で選択がどうなるか(seg の beat が重複するスライドを解除できない件)
        void **sv = sel_vec(a1);
        selBefore = (int)(((void **)sv[1] - (void **)sv[0]));
        if (selBefore == 1) selNote = VT(*(void **)sv[0], 704, uint8_t *(*)(void *))(*(void **)sv[0]);
    }
    intptr_t r = orig_s_touch(a1, ev, a3, pos);
    if (ev == 0 && selBefore) {
        void **sv = sel_vec(a1);
        int selAfter = (int)(((void **)sv[1] - (void **)sv[0]));
        uint8_t *tb = (uint8_t *)a1;
        if (selNote && (selNote[113] & 8)) {
            uint8_t *sb = *(uint8_t **)(selNote + 136), *se = *(uint8_t **)(selNote + 144);
            os_log(L, "slide-sel: tool=%d sel %d->%d ret=%ld head=%.4f end=%.4f segs=%ld lastSeg=%.4f edit880=%d cur1584=%d", *(int *)(tb + 1368), selBefore, selAfter, (long)r,
                   note_beat(selNote, 64, 72, 76), note_beat(selNote, 88, 96, 100), (long)((se - sb) / 28), se > sb ? note_beat(se - 28, 0, 8, 12) : -1.0, tb[880], *(void **)(tb + 1584) != NULL);
        } else os_log(L, "slide-sel: tool=%d sel %d->%d ret=%ld (non-path)", *(int *)(tb + 1368), selBefore, selAfter, (long)r);
    }
    ord_track(a1, ev, pos);
    return r;
}
static keytouch_fn orig_snote;
// 経路(PathSpriteEditor)の当たり判定は「いずれかの中継点が押された位置に当たるか」(vtable +1128)。seg の beat が重複するスライドでは、中継点の 1 つが
// どこを押しても「当たり」を返す(本家のバグ)ため、押下がそのスライドに奪われ、レーンの処理(選択解除など)に届かず、選択を解除できなかった。
// 押された位置の近く(中継点の大きさの 2 倍強)に中継点が 1 つも無ければ、本当は外れなので、押下を消費せず次の処理(レーン)へ渡す。
static BOOL path_tap_near_point(void *path, const void *pos) {
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const void *))VA(0x100546980ULL))(path, pos);   // 押された位置 → 経路スプライトの座標
    void **cb = *(void ***)((uint8_t *)path + 648), **ce = *(void ***)((uint8_t *)path + 656);
    if (!cb || cb >= ce || (ce - cb) > 4096) return YES;                           // 想定外の構造 = 標準の判定に任せる
    for (void **c = cb; c != ce; c++) {
        float cx = VT(*c, 192, float (*)(void *))(*c), cy = VT(*c, 208, float (*)(void *))(*c);
        if (fabsf(r.v[0] - cx) <= 200.0f && fabsf(r.v[1] - cy) <= 200.0f) return YES;
    }
    return NO;
}
// オプション: Slide の Remove ツール(タグ 83)で経路のスライドを押したら、ノートごとではなく、押した位置より後ろの中継点だけを消す(Malody V と同じ)。
// 頭(始点)を押したとき / 中継点が無いスライド / Wipe は標準どおりノートごと消す。押した位置 = 拍で、中継点の近く(0.1 拍以内)ならその中継点から後ろを消す
static BOOL slide_remove_cut(void *ed, void *spr, void *pos) {
    uint8_t *e = (uint8_t *)ed;
    if (*(int *)(e + 1368) != 83 || !pos) return NO;
    uint8_t *n = VT(spr, 704, uint8_t *(*)(void *))(spr);
    if (!n || !(n[113] & 8)) return NO;
    uint8_t *vb = *(uint8_t **)(n + 136), *ve = *(uint8_t **)(n + 144);
    long cnt = (ve - vb) / 28;
    if (cnt <= 0 || cnt > 4096) return NO;
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const void *))VA(0x100546980ULL))(*(void **)(e + 1472), pos);   // 押した位置 → 容器の座標
    double K = (double)LF(ed, 1716) * (double)LF(ed, 1720);
    if (K <= 0) return NO;
    double tb = r.v[1] / K, head = note_beat(n, 64, 72, 76);
    if (tb <= head + 0.1) return NO;                                  // 頭 = ノートごと消す
    long k = cnt;
    for (long i = 0; i < cnt; i++) if (head + note_beat(vb + 28 * i, 0, 8, 12) > tb) { k = i; break; }
    if (k > 0 && tb - (head + note_beat(vb + 28 * (k - 1), 0, 8, 12)) <= 0.1) k--;   // 中継点そのものを押した
    if (k >= cnt) k = cnt - 1;
    if (k <= 0) return NO;                                            // 中継点が 1 つも残らない = 頭だけのスライドは標準では作れず、押しても消せなくなる → ノートごと消す
    *(uint8_t **)(n + 144) = vb + 28 * k;
    Rat end = rat_of(n + 64);
    if (k > 0) end = rat_add(end, rat_of(vb + 28 * (k - 1)));
    beat_write_rat(n + 88, end);                                      // 終端 = 残った最後の中継点
    void *savedEd = gPlEd; gPlEd = ed;
    pl_seg_touch(n);
    pl_rebuild_dirty();
    gPlEd = savedEd;
    *(e + 883) = 1;
    os_log(L, "remove-cut: kept %ld of %ld segs (touch=%.3f head=%.3f)", k, cnt, tb, head);
    return YES;
}
// ---- #8 オプション: Slide の編集(設定画面 / その他メニューの Editor options で ON) ----
// (1) Slide → Wipe(sub_1000C5E10): 標準は始点〜終点を分割グリッドで刻んで Wipe を作り直す。オプション ON は、始点と各 seg の位置(beat / x)のまま Wipe にする(Malody V と同じ)
typedef void (*s2w_fn)(void *);
static s2w_fn orig_s2wipe;
static void hook_s2wipe(void *ed) {
    const Lay *l = lay_of(ed);
    if (!bm_opt(BMO_S2W) || !l || !l->slide) { orig_s2wipe(ed); return; }
    uint8_t *e = (uint8_t *)ed;
    void **sv = sel_vec(ed);
    NSMutableArray<NSValue *> *slides = [NSMutableArray array];
    for (void **p = (void **)sv[0]; p != (void **)sv[1]; p++) {
        uint8_t *n = VT(*p, 704, uint8_t *(*)(void *))(*p);
        if (n && (n[113] & 8)) [slides addObject:[NSValue valueWithPointer:n]];
    }
    if (!slides.count) { orig_s2wipe(ed); return; }                 // 選択が無い / Slide 以外 = 標準のメッセージ
    typedef void *(*new_fn)(size_t);
    static new_fn nw;
    if (!nw) nw = (new_fn)dlsym(RTLD_DEFAULT, "_Znwm");
    NSMutableArray<NSValue *> *wipes = [NSMutableArray array];
    for (NSValue *sn in slides) {
        uint8_t *n = sn.pointerValue;
        Rat head = rat_of(n + 64);
        uint8_t *sb = *(uint8_t **)(n + 136), *se = *(uint8_t **)(n + 144);
        long cnt = (se - sb) / 28;
        if (cnt < 0 || cnt > 4096) continue;
        for (long i = -1; i < cnt; i++) {                              // -1 = 始点
            Rat at = i < 0 ? head : rat_add(head, rat_of(sb + 28 * i));
            int x = n[128] + (i < 0 ? 0 : seg_x16(sb + 28 * i));
            if (at.n < 0) continue;
            uint8_t *w = nw(0x88);
            memset(w, 0, 0x88);
            ((void (*)(void *))VA(0x1001EBC08ULL))(w);                 // HitObjectWipe の初期化
            *(int32_t *)(w + 112) = 1024;
            w[128] = (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x)); w[129] = n[129];
            beat_write_rat(w + 64, at); beat_write_rat(w + 88, at);
            int ms = (int)time_of(w + 56);
            *(int32_t *)(w + 80) = ms; *(int32_t *)(w + 104) = ms;
            [wipes addObject:[NSValue valueWithPointer:w]];
        }
    }
    for (NSValue *sn in slides) {                                       // 元のスライドを消す(標準の削除と同じ手順)
        uint8_t *n = sn.pointerValue;
        void *sp = sprite_for_note(ed, n);
        if (sp && *(void **)(e + 1584) == sp) *(void **)(e + 1584) = NULL;
        ((void (*)(void *, void *, int, int))VA(0x1000C79F8ULL))(ed, n, 1, 0);
    }
    *(void **)(e + 1568) = *(void **)(e + 1560);                        // 選択を空に
    void *snd = *(void **)(e + 1080), *chart = *(void **)(e + 1592);
    for (NSValue *wv in wipes) {
        uint8_t *w = wv.pointerValue;
        ((void (*)(void *, void *))VA(0x1001C6834ULL))(chart, w);
        ((void (*)(void *, void *))VA(0x1000C8A68ULL))(ed, w);
        ((void (*)(void *, long))VA(0x10006C1ECULL))(snd, *(int32_t *)(w + 80));
        VT(ed, 1648, void (*)(void *, void *))(ed, w);
    }
    ((void (*)(void *))VA(0x10006C288ULL))(snd);
    char b[64]; snprintf(b, sizeof b, "Slide -> Wipe: %lu notes", (unsigned long)wipes.count);
    ed_toast_text(ed, b, 4);
    os_log(L, "slide2wipe: %lu slides -> %lu wipes", (unsigned long)slides.count, (unsigned long)wipes.count);
}
// (2) seg の追加(sub_1000C6CA8): 標準は、選択中(編集中)のスライドの終点 beat 以前をタップすると「選択解除」。オプション ON は、始点より後ろなら、終点より前でも seg を足す(時系列でない seg)。
// 始点以前のタップは標準どおり選択解除。標準は終点が押した位置より前のときだけ seg を足すので、足す間だけ終点を始点に下げて標準の処理を通し、終わったら元の終点へ戻す
typedef void (*segadd_fn)(void *, uint32_t, double);
static segadd_fn orig_segadd;
static void hook_segadd(void *ed, uint32_t x, double beat) {
    const Lay *l = lay_of(ed);
    if (bm_opt(BMO_SEG_ANY) && l && l->slide) {
        uint8_t *e = (uint8_t *)ed;
        void *spr = *(void **)(e + 1584);
        void **sv = sel_vec(ed);
        if (((uint8_t *)sv[1] - (uint8_t *)sv[0]) == 8) { void *pp = cast_to(*(void **)sv[0], 0x100922780ULL); if (pp) spr = pp; }   // 選択が PathSpriteEditor 1 つ
        uint8_t *n = spr ? VT(spr, 704, uint8_t *(*)(void *))(spr) : NULL;
        if (n && (n[113] & 8)) {
            double head = note_beat(n, 64, 72, 76), end = note_beat(n, 88, 96, 100);
            if (beat > head + 1e-9 && beat <= end) {
                uint8_t save[24]; memcpy(save, n + 88, 24);
                beat_write_rat(n + 88, rat_of(n + 64));                 // 終点 = 始点(標準から見て「押した位置が終点より後」)
                orig_segadd(ed, x, beat);
                memcpy(n + 88, save, 24);                               // 終点は元のまま(最も遅い seg の beat)
                os_log(L, "seg-any: added seg at %.4f (end %.4f)", beat, end);
                return;
            }
        }
    }
    orig_segadd(ed, x, beat);
}
// 標準の seg 追加は、押した位置に既にノートがあると(sub_1001C8F60)何もしない。編集中のスライド自身の本体(幅の中)も「ノートあり」と判定されるので、時系列でない seg を
// 自分の本体の上に置こうとしても、置けたり置けなかったりしていた。ツールが slide(81 / 84)で編集中のスライド自身が当たったときは「なし」と答える
typedef void *(*chit_fn)(void *, long, double);
static chit_fn orig_chit;
static void *hook_chit(void *chart, long lane, double beat) {
    void *r = orig_chit(chart, lane, beat);
    if (r && gEditor && bm_opt(BMO_SEG_ANY) && lay_of(gEditor) && lay_of(gEditor)->slide) {
        uint8_t *e = (uint8_t *)gEditor;
        int tool = *(int *)(e + 1368);
        if (tool == 81 || tool == 84) {
            void *spr = *(void **)(e + 1584);
            void **sv = sel_vec(gEditor);
            if (((uint8_t *)sv[1] - (uint8_t *)sv[0]) == 8) { void *pp = cast_to(*(void **)sv[0], 0x100922780ULL); if (pp) spr = pp; }
            uint8_t *n = spr ? VT(spr, 704, uint8_t *(*)(void *))(spr) : NULL;
            if (n && (n[113] & 8) && (void *)n == r) return NULL;
        }
    }
    return r;
}
// (3) seg を縦方向(beat)にも動かす: 標準の seg の移動は横(x)だけ。ドラッグを離した位置の beat(現在の分割に切り捨て)を、動かした seg の beat にする。
// 時系列でない seg を許可していないときは、前後の seg の beat の間に収める。始点より前にはしない。スプライトは、タッチの処理が終わってから作り直す
static void seg_vert_commit(void *ed, void *spr, const void *pos) {
    uint8_t *e = (uint8_t *)ed;
    void *path = cast_to(spr, 0x100922780ULL);
    uint8_t *n = VT(spr, 704, uint8_t *(*)(void *))(spr);
    if (!path || !n || !(n[113] & 8) || !pos) return;
    int idx = *(int *)((uint8_t *)path + 696);                          // 動かした seg の番号(標準が押下で決める。-1 = なし)
    uint8_t *sb = *(uint8_t **)(n + 136), *se = *(uint8_t **)(n + 144);
    long cnt = (se - sb) / 28;
    if (idx < 0 || idx >= cnt || cnt > 4096) return;
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const void *))VA(0x100546980ULL))(*(void **)(e + 1472), pos);
    double K = (double)LF(ed, 1716) * (double)LF(ed, 1720);
    int div = e[1713]; if (div < 1) div = 1;
    if (K <= 0) return;
    int64_t q = (int64_t)floor(r.v[1] / K * div + 1e-9);
    Rat head = rat_of(n + 64), abs = { q, div };
    Rat rel = rat_add(abs, rat_neg(head));
    double headD = rat_dbl(head), absD = rat_dbl(abs);
    if (absD < headD) rel = (Rat){ 0, 1 };                              // 始点より前にはしない
    if (!bm_opt(BMO_SEG_ANY)) {                                         // 時系列を保つ: 前後の seg の間
        double lo = idx > 0 ? note_beat(sb + 28 * (idx - 1), 0, 8, 12) : 0.0, hi = idx + 1 < cnt ? note_beat(sb + 28 * (idx + 1), 0, 8, 12) : 1e9, cur = rat_dbl(rel);
        if (cur < lo) rel = rat_of(sb + 28 * (idx - 1));
        else if (cur > hi) rel = rat_of(sb + 28 * (idx + 1));
    }
    uint8_t *seg = sb + 28 * idx;
    Rat was = rat_of(seg);
    if (was.n * rel.d == rel.n * was.d) return;                         // beat が変わらない(横だけ動かした)
    beat_write_rat(seg, rel);
    {   // 時間(seg の +16 = 始点からの ms)
        uint8_t tmp[40]; memset(tmp, 0, sizeof tmp);
        beat_write_rat(tmp + 8, rat_add(head, rel));
        *(int32_t *)(seg + 16) = (int)time_of(tmp) - *(int32_t *)(n + 80);
    }
    double mx = 0; Rat endr = head;                                     // 終点 = 最も遅い seg
    for (long i = 0; i < cnt; i++) { double b = note_beat(sb + 28 * i, 0, 8, 12); if (b >= mx) { mx = b; endr = rat_add(head, rat_of(sb + 28 * i)); } }
    beat_write_rat(n + 88, endr);
    os_log(L, "seg-vert: seg %d beat %.4f -> %.4f", idx, rat_dbl(was), rat_dbl(rel));
    NSValue *nv = [NSValue valueWithPointer:n];
    dispatch_async(dispatch_get_main_queue(), ^{                        // タッチの処理が終わってからスプライトを作り直す
        if (gEditor != ed) return;
        void *saved = gPlEd; gPlEd = ed;
        pl_seg_touch(nv.pointerValue);
        pl_rebuild_dirty();
        gPlEd = saved;
        *((uint8_t *)ed + 883) = 1;
        un_mark();
    });
}
// (4) seg の選び方: 標準は「押した位置に最初に当たった seg」。オプション ON は、押した位置に最も近い seg(丸)を選ぶ
static void seg_pick_nearest(void *ed, void *spr, const void *pos) {
    void *path = cast_to(spr, 0x100922780ULL);
    if (!path || !pos) return;
    uint8_t *pp = (uint8_t *)path;
    void **cb = *(void ***)(pp + 712), **ce = *(void ***)(pp + 720);    // seg の丸(0 = 始点)
    if (!cb || cb >= ce || (ce - cb) > 4097) return;
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    V2Ret r = ((V2Ret (*)(void *, const void *))VA(0x100546980ULL))(path, pos);
    int best = -1; float bd = 80.0f;
    for (void **c = cb + 1; c != ce; c++) {
        float dx = r.v[0] - VT(*c, 192, float (*)(void *))(*c), dy = r.v[1] - VT(*c, 208, float (*)(void *))(*c);
        float d = sqrtf(dx * dx + dy * dy);
        if (d < bd) { bd = d; best = (int)(c - cb) - 1; }
    }
    if (best >= 0) { os_log(L, "seg-hit: stock idx=%d nearest=%d d=%.1f", *(int *)(pp + 696), best, bd); *(int *)(pp + 696) = best; }
}
static intptr_t hook_s_note(void *a1, int ev, void *a3, void *pos) {   // sub_1000C7224: ノート自身へのタッチ(選択・移動)。順番に選択はここで選択が更新された後に控える
    if (ev == 0 && a3 && bm_opt(BMO_REMOVE_CUT) && slide_remove_cut(a1, a3, pos)) return 0;
    intptr_t r = orig_snote(a1, ev, a3, pos);
    if (a3 && lay_of(a1) && lay_of(a1)->slide && *(int *)((uint8_t *)a1 + 1368) == 84) {   // Slide modify(ツール 84)
        if (ev == 0 && bm_opt(BMO_SEG_HIT)) seg_pick_nearest(a1, a3, pos);
        if (ev == 2 && bm_opt(BMO_SEG_VERT)) seg_vert_commit(a1, a3, pos);
    }
    ord_track(a1, ev, pos);
    return r;
}
// 標準のノートの並び順(Wipe→Slide の seg 順など): 同じ小節内の比較で num * den が 32bit を超えると順序が壊れる(分母 1048576 の wipe)。64bit で比べ直す
static intptr_t hook_notecmp(const int32_t *a, const int32_t *b) {   // sub_1001EB6A0
    if (a[21] != b[21]) return a[21] < b[21];
    if (a[16] != b[16]) return a[16] < b[16];
    return (int64_t)a[18] * (int64_t)(uint32_t)b[19] < (int64_t)(uint32_t)a[19] * (int64_t)b[18];
}
typedef intptr_t (*pathhit_fn)(void *, const void *);
static pathhit_fn orig_pathhit;
static intptr_t hook_pathhit(void *path, const void *pos) {   // sub_100167514: PathSpriteEditor の当たり判定(押した位置が中継点に当たるか)
    intptr_t r = orig_pathhit(path, pos);
    if (r && pos && !path_tap_near_point(path, pos)) return 0;  // どの中継点からも遠いのに「当たり」= 重複 beat の中継点の誤判定
    return r;
}
static void hook_uiinit(void *self, void *layout) {   // sub_100073140: UI初期化後に Effect パネルのノードを結び付ける
    orig_uiinit(self, layout);
    if (layout) { gFxClickRegistered = NO; fx_bind(self, layout); pl_bind(self, layout); sd_bind(self, layout); }
    mk_reset();
    if (layout && lay_of(self)) {          // 情報画面の追加項目(Video / BGA / Skin)のラベルを結び付けて現在値を表示
        gInfoEd = self;
        for (int k = 0; k < 5; k++) {
            char nm[16]; snprintf(nm, sizeof nm, "_infot-%d", 12 + k);
            gInfoLbl[k] = cast_to(fx_find(layout, nm), 0x100928270ULL);       // FieldText
        }
        info_fill_labels(self);
    }
}
static void hook_s_refresh_inner(void *a1, float dt);
// Slide のノーツ幅グリッド(more の Grid lines / Grid align)は、エディタ内の 2 バイト(+1732 = 揃える ON/OFF、+1733 = 分割数)。エディタを抜けると初期値に戻るので、
// アプリが起動している間は保持する: 新しいエディタの最初のフレームで保存値を書き戻し、その後の変更を保存する
static void slide_grid_keep(void *ed) {
    const Lay *l = lay_of(ed);
    if (!l || !l->slide) return;
    static void *lastEd; static BOOL have, armed; static uint8_t sv[2]; static int frames;
    uint8_t *g = (uint8_t *)ed + 1732;
    if (lastEd != ed) { lastEd = ed; armed = NO; frames = 0; }
    if (!armed) {
        if (++frames < 30) return;                      // エディタの初期化が済むまで待つ
        armed = YES;
        if (have) { g[0] = sv[0]; g[1] = sv[1]; ((void (*)(void *))VA(0x1000C63E8ULL))(ed); os_log(L, "grid: restored align=%d lines=%d", g[0], g[1]); }   // sub_1000C63E8 = 設定の反映後の再構築
        else { sv[0] = g[0]; sv[1] = g[1]; have = YES; }
        return;
    }
    if (g[0] != sv[0] || g[1] != sv[1]) { sv[0] = g[0]; sv[1] = g[1]; have = YES; os_log(L, "grid: saved align=%d lines=%d", sv[0], sv[1]); }
}
static void hook_s_refresh(void *a1, float dt) { hook_s_refresh_inner(a1, dt); if (lay_of(a1)) { kb_tick(a1); ovl_update(a1); spec_update(a1); mod_update(a1); un_tick(a1); slide_grid_keep(a1); pl_right_tick(a1); more_sync(a1); { static int f; if ((++f % 10) == 0 && (gOrdNotes.count || gOrdTinted.count)) ord_tint_sync(a1); } } }
static void hook_s_refresh_inner(void *a1, float dt) {   // sub_1000C5408 = EditorSlide update
    if (gPreview) {
        static int cnt; static double lastPos = -1e9;
        if (++cnt % 180 == 0 || fabs(*(double *)((uint8_t *)a1 + 872) - lastPos) > 20) {
            lastPos = *(double *)((uint8_t *)a1 + 872);
            os_log(L, "s_update: a1=%p gEditor=%p flag883=%d pos=%.3f G=%.3f contY=%.1f", a1, gEditor, *((uint8_t *)a1 + 883), lastPos, G(lastPos),
                   VT(*(void **)((uint8_t *)a1 + 1472), 208, float (*)(void *))(*(void **)((uint8_t *)a1 + 1472)));
        }
    }
    if (gPreview && a1 == gEditor) {
        ((void (*)(void *, float))VA(0x100075D2CULL))(a1, dt);
        if (*((uint8_t *)a1 + 883)) { ed_apply_scroll(a1); *((uint8_t *)a1 + 883) = 0; }
        return;
    }
    orig_s_refresh(a1, dt);
}
static intptr_t hook_s_zoom(void *a1, float z) {   // sub_1000C5AB0
    intptr_t r = orig_s_zoom(a1, zm_filter(a1, z));
    if (gPreview && a1 == gEditor) ed_relayout(a1);
    return r;
}


// ---- ファイルアプリ(Documents)へ譜面フォルダを公開 ----
// ゲームの譜面置き場(kind 0 = Library/beatmap/)を、コードパッチで Documents/ 直下へ変更している(tools/make_hook_patch.py の FILES_PATCH)。
// Documents 直下は同梱サンプル曲(_song_*)と同じ階層で、ゲームが曲として走査する。ファイルアプリ(UIFileSharingEnabled)から編集できる。
// ここでは既存データの移行だけを行う(起動時 1 回):
//   ・Library/beatmap/_song_* (実体) → Documents/ へ移動(同名があれば移さず残す)
//   ・旧方式の Documents/beatmap, Documents/skin (中身が移っただけのもの)も戻す/展開する
//   ・Library/beatmap は Documents へのリンクにする(保存済みの絶対パス "…/Library/beatmap/…" を解決できるように)
static void bm_move_children(NSFileManager *fm, NSString *from, NSString *to) {
    for (NSString *it in [fm contentsOfDirectoryAtPath:from error:nil]) {
        if ([it hasPrefix:@"."]) continue;
        NSString *a = [from stringByAppendingPathComponent:it], *b = [to stringByAppendingPathComponent:it];
        if ([fm fileExistsAtPath:b]) { os_log(L, "migrate: skip (exists) %{public}@", it); continue; }
        NSError *er = nil;
        if (![fm moveItemAtPath:a toPath:b error:&er]) os_log(L, "migrate: move failed %{public}@: %{public}@", it, er.localizedDescription);
    }
}
static BOOL bm_is_link(NSFileManager *fm, NSString *p) { return [fm destinationOfSymbolicLinkAtPath:p error:nil] != nil; }
static BOOL bm_is_dir(NSFileManager *fm, NSString *p) { BOOL d = NO; return [fm fileExistsAtPath:p isDirectory:&d] && d; }
// 標準の文字描画(sub_100598B40)は、文字列の寸法が負になる場合に malloc(負) → bzero(NULL, 負) で落ちる
// (Catalyst の sizeWithFont:constrainedToSize: が日本語などで返す値による)。_bzero のスタブの飛び先を差し替えて無害化する。
static void bm_safe_bzero(void *p, size_t n) {
    if (!p || (int64_t)n < 0) return;
    memset(p, 0, n);
}
static void bm_guard_bzero(void) {
    const uint32_t *st = (const uint32_t *)VA(0x1007424E4ULL);   // _bzero のスタブ: adrp x16 / ldr x16,[x16,#off] / br x16
    uint32_t a = st[0], l = st[1];
    if ((a & 0x9F00001F) != 0x90000010 || (l & 0xFFC003FF) != 0xF9400210 || st[2] != 0xD61F0200) { os_log(L, "guard: bzero stub mismatch"); return; }
    int64_t imm = (int64_t)((((a >> 5) & 0x7FFFF) << 2) | ((a >> 29) & 3));
    if (imm & 0x100000) imm -= 0x200000;
    uintptr_t page = ((uintptr_t)st & ~(uintptr_t)0xFFF) + (uintptr_t)(imm << 12);
    uintptr_t slot = page + (uintptr_t)(((l >> 10) & 0xFFF) << 3);
    vm_address_t pg = slot & ~(vm_address_t)0x3FFF;
    kern_return_t kr = vm_protect(mach_task_self(), pg, 0x4000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) { os_log(L, "guard: vm_protect failed %d", kr); return; }
    *(void **)slot = (void *)bm_safe_bzero;
    os_log(L, "guard: bzero slot %p patched", (void *)slot);
}
static void bm_expose_files(void) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *home = NSHomeDirectory();
    NSString *lib = [home stringByAppendingPathComponent:@"Library"], *doc = [home stringByAppendingPathComponent:@"Documents"];
    NSString *libBm = [lib stringByAppendingPathComponent:@"beatmap"], *docBm = [doc stringByAppendingPathComponent:@"beatmap"];
    NSString *libSkin = [lib stringByAppendingPathComponent:@"skin"], *docSkin = [doc stringByAppendingPathComponent:@"skin"];
    // 旧方式(Library/{beatmap,skin} → Documents/{beatmap,skin} へのリンク)の後始末
    if (bm_is_link(fm, libBm)) [fm removeItemAtPath:libBm error:nil];
    if (bm_is_link(fm, libSkin)) [fm removeItemAtPath:libSkin error:nil];
    if (bm_is_dir(fm, docSkin) && !bm_is_link(fm, docSkin)) {                    // 旧方式で作った skin は元の場所へ戻す
        if (![fm fileExistsAtPath:libSkin]) [fm moveItemAtPath:docSkin toPath:libSkin error:nil];
        else { bm_move_children(fm, docSkin, libSkin); [fm removeItemAtPath:docSkin error:nil]; }
    }
    if (bm_is_dir(fm, docBm) && !bm_is_link(fm, docBm)) {                         // 旧方式の Documents/beatmap の中身を直下へ
        bm_move_children(fm, docBm, doc);
        if (![[fm contentsOfDirectoryAtPath:docBm error:nil] count]) [fm removeItemAtPath:docBm error:nil];
    }
    if (bm_is_dir(fm, libBm) && !bm_is_link(fm, libBm)) {                         // Library/beatmap の実体を Documents 直下へ
        bm_move_children(fm, libBm, doc);
        if (![[fm contentsOfDirectoryAtPath:libBm error:nil] count]) [fm removeItemAtPath:libBm error:nil];
    }
    {   // プラグイン置き場: 旧 Documents/Plugins(曲として拾われてしまう)→ export_plugins(曲スキャンの除外名)
        NSString *oldP = [doc stringByAppendingPathComponent:@"Plugins"], *newP = [doc stringByAppendingPathComponent:@"export_plugins"];
        if (bm_is_dir(fm, oldP) && !bm_is_link(fm, oldP)) {
            if (![fm fileExistsAtPath:newP]) [fm moveItemAtPath:oldP toPath:newP error:nil];
            else { bm_move_children(fm, oldP, newP); [fm removeItemAtPath:oldP error:nil]; }
        }
    }
    if (![fm fileExistsAtPath:libBm] && !bm_is_link(fm, libBm)) {
        NSError *er = nil;
        if ([fm createSymbolicLinkAtPath:libBm withDestinationPath:doc error:&er]) os_log(L, "expose: Library/beatmap -> Documents");
        else os_log(L, "expose: link failed %{public}@", er.localizedDescription);
    }
}

// ---- 曲選択画面の「何をしますか?」メニュー(sub_10010872C case 4)に「曲のフォルダを開く」を足す ----
// 元のコードはメニューに lang 101(曲のフォルダを開く)/ id 208 の処理(sub_10010A114)を持つが、ボタンは足されず、
// 実処理(nullsub_48)は空 = デスクトップ版だけの機能。nullsub_48 を実装し、ボタン(id 208)を足す。
typedef struct { uint8_t b[24]; } BMStr24;
typedef BMStr24 (*lang_fn)(void *, unsigned);
typedef intptr_t (*addbtn_fn)(void *dlg, void *text, void *sub, long id);
static lang_fn orig_lang;
static addbtn_fn orig_addbtn;
static void *gLangTable;
static BMStr24 hook_lang(void *lang, unsigned id) {      // 言語表のポインタを控える + MOD 画面の TURBO の説明(id 211 = 190 + タグ 21)
    if (lang) gLangTable = lang;
    if (id == 211) { BMStr24 r; memset(&r, 0, sizeof r); const char *d = "Free judge rules"; r.b[23] = (uint8_t)strlen(d); memcpy(r.b, d, strlen(d)); return r; }
    return orig_lang(lang, id);
}
static intptr_t hook_addbtn(void *dlg, void *text, void *sub, long id) {
    intptr_t r = orig_addbtn(dlg, text, sub, id);
    if (id == 202 && gLangTable) {                        // 曲メニュー: 「ローカルのハイスコアを削除」の直後へ
        BMStr24 t = orig_lang(gLangTable, 101);
        uint8_t empty[24] = {0};
        orig_addbtn(dlg, &t, empty, 208);
        if (t.b[23] & 0x80) {                              // 長い文字列は確保分を解放(operator delete)
            static void (*del)(void *);
            if (!del) del = (void (*)(void *))dlsym(RTLD_DEFAULT, "_ZdlPv");
            if (del) del(*(void **)t.b);
        }
    }
    return r;
}
static void hook_openpath(void *sp) {                    // nullsub_48(std::string *path)
    if (!sp) return;
    const char *c = stdstr_c((uint8_t *)sp);
    NSString *p = c ? [NSString stringWithUTF8String:c] : nil;
    if (!p.length) return;
    if (![p hasPrefix:@"/"]) {                            // 相対(曲フォルダ名)なら Documents 直下とみなす
        NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        p = [docs stringByAppendingPathComponent:p];
    }
    BOOL isDir = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:p isDirectory:&isDir]) { os_log(L, "openpath: missing %{public}@", p); return; }
    if (!isDir) p = p.stringByDeletingLastPathComponent;
    os_log(L, "openpath: %{public}@", p);
    bm_open_path(p, NULL);
}

// ---- アプリ内の Web 表示(WebviewHandle の UIWebView)を、既定のブラウザで開く ----
//   UIWebView の loadRequest: で http(s) を受けたら、読み込まずに UIApplication の openURL で外へ渡し、アプリ内の表示(Back ラベル付きの全面ビュー)は隠す。
//   show は、直前に外へ渡した直後なら出さない。無効化 = defaults write me.mugzone.m bm.web_external -bool NO
static CFAbsoluteTime gWebRedirT;
static void bm_web_external_install(void) {
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    if ([u objectForKey:@"bm.web_external"] && ![u boolForKey:@"bm.web_external"]) return;
    Class uw = NSClassFromString(@"UIWebView");
    Method m = uw ? class_getInstanceMethod(uw, @selector(loadRequest:)) : NULL;
    if (m) {
        __block IMP orig = method_getImplementation(m);
        IMP nu = imp_implementationWithBlock(^(id me, NSURLRequest *req) {
            NSURL *url = req.URL;
            NSString *sch = url.scheme.lowercaseString;
            if ([sch isEqualToString:@"http"] || [sch isEqualToString:@"https"]) {
                gWebRedirT = CFAbsoluteTimeGetCurrent();
                os_log(L, "web: open externally %{public}@", url.absoluteString);
                dispatch_async(dispatch_get_main_queue(), ^{
                    id root = ((id (*)(id, SEL))objc_msgSend)(me, NSSelectorFromString(@"superview"));
                    if (root) ((void (*)(id, SEL, BOOL))objc_msgSend)(root, NSSelectorFromString(@"setHidden:"), YES);
                    id app = ((id (*)(id, SEL))objc_msgSend)((id)NSClassFromString(@"UIApplication"), NSSelectorFromString(@"sharedApplication"));
                    ((void (*)(id, SEL, id, id, id))objc_msgSend)(app, NSSelectorFromString(@"openURL:options:completionHandler:"), url, @{}, nil);
                });
                return;
            }
            ((void (*)(id, SEL, id))orig)(me, @selector(loadRequest:), req);
        });
        method_setImplementation(m, nu);
    }
    Class wh = NSClassFromString(@"WebviewHandle");
    Method sm = wh ? class_getInstanceMethod(wh, NSSelectorFromString(@"show")) : NULL;
    if (sm) {
        __block IMP so = method_getImplementation(sm);
        IMP snu = imp_implementationWithBlock(^(id me) {
            if (CFAbsoluteTimeGetCurrent() - gWebRedirT < 3.0) return;   // 外のブラウザへ渡した直後はアプリ内に出さない
            ((void (*)(id, SEL))so)(me, NSSelectorFromString(@"show"));
        });
        method_setImplementation(sm, snu);
    }
    os_log(L, "web: redirect installed (UIWebView=%d WebviewHandle=%d)", m != NULL, sm != NULL);
}
__attribute__((constructor)) static void bm_init(void) {
    L = os_log_create("BetterMalody", "rt");
    dispatch_async(dispatch_get_main_queue(), ^{ osu_srv_boot(); });
    gSlide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    orig_touch = (touch_fn)VA(BM_ORIG_TOUCH_VA);
    orig_refresh = (refresh_fn)VA(BM_ORIG_REFRESH_VA);
    orig_zoom = (zoom_fn)VA(BM_ORIG_ZOOM_VA);
    orig_keytouch = (keytouch_fn)VA(BM_ORIG_KEYTOUCH_VA);
    void **slots = (void **)VA(BM_SLOT_VA);
    slots[0] = (void *)hook_touch;
    slots[1] = (void *)hook_refresh;
    slots[2] = (void *)hook_zoom;
    slots[3] = (void *)hook_keytouch;
    orig_s_refresh = (refresh_fn)VA(BM_ORIG_S_REFRESH_VA);
    orig_s_zoom = (zoom_fn)VA(BM_ORIG_S_ZOOM_VA);
    orig_s_touch = (keytouch_fn)VA(BM_ORIG_S_TOUCH_VA);
    orig_uiinit = (uiinit_fn)VA(BM_ORIG_UIINIT_VA);
    slots[9] = (void *)hook_uiinit;
    orig_lang = (lang_fn)VA(0x100005088ULL);          // lang の ORIG(cave 0x100005078 + 16)
    orig_addbtn = (addbtn_fn)VA(0x1000050A0ULL);      // addbtn の ORIG(cave 0x100005090 + 16)
    slots[10] = (void *)hook_openpath;
    slots[11] = (void *)hook_lang;
    slots[12] = (void *)hook_addbtn;
    orig_cfgtouch = (cfgtouch_fn)VA(0x1000050B8ULL);     // cfgtouch の ORIG(cave 0x1000050A8 + 16)
    orig_cfgbuild = (cfgbuild_fn)VA(0x1000050D0ULL);     // cfgbuild(cave 0x1000050C0 + 16)
    orig_keydlg = (keydlg_fn)VA(0x1000050E8ULL);         // keydlg(cave 0x1000050D8 + 16)
    orig_setvol = (setvol_fn)VA(0x100005100ULL);         // setvol(cave 0x1000050F0 + 16)
    slots[13] = (void *)hook_cfgtouch;
    slots[14] = (void *)hook_cfgbuild;
    slots[15] = (void *)hook_keydlg;
    slots[16] = (void *)hook_setvol;
    orig_kjudge = (kjudge_fn)VA(0x100005118ULL);         // kjudge(cave 0x100005108 + 16)
    slots[17] = (void *)hook_kjudge;
    orig_kpress = (kpress_fn)VA(0x100005160ULL); slots[20] = (void *)hook_kpress;       // kpress(cave 0x100005150 + 16)
    orig_jrel = (jrel_fn)VA(0x100005178ULL); slots[21] = (void *)hook_jrel;             // jrel(cave 0x100005168 + 16)
    orig_modname = (modname_fn)VA(0x100005190ULL); slots[22] = (void *)hook_modname;    // modname(cave 0x100005180 + 16)
    orig_modsel = (modsel_fn)VA(0x1000051A8ULL); slots[23] = (void *)hook_modsel;       // modsel(cave 0x100005198 + 16)
    orig_modbuild = (modbuild_fn)VA(0x1000051C0ULL); slots[24] = (void *)hook_modbuild; // modbuild(cave 0x1000051B0 + 16)
    orig_playstart = (playstart_fn)VA(0x100005230ULL); slots[25] = (void *)hook_playstart; // playstart(cave 0x100005220 + 16)
    orig_prepkey = (prepkey_fn)VA(0x100005248ULL); slots[26] = (void *)hook_prepkey;       // prepkey(cave 0x100005238 + 16)
    orig_pausetgl = (pausetgl_fn)VA(0x100005260ULL); slots[27] = (void *)hook_pausetgl;    // pausetgl(cave 0x100005250 + 16)
    orig_savescore = (savescore_fn)VA(0x100005278ULL); slots[28] = (void *)hook_savescore; // savescore(cave 0x100005268 + 16)
    orig_localrec = (localrec_fn)VA(0x100005290ULL); slots[29] = (void *)hook_localrec;     // localrec(cave 0x100005280 + 16)
    orig_diffname = (diffname_fn)VA(0x1000052C0ULL); slots[31] = (void *)hook_diffname;   // diffname(cave 0x1000052B0 + 16)
    orig_sprep1 = (sprep_fn)VA(0x1000052D8ULL); slots[32] = (void *)hook_sprep1;          // slideprep(cave 0x1000052C8 + 16)
    orig_sprep2 = (sprep_fn)VA(0x1000052F0ULL); slots[33] = (void *)hook_sprep2;          // slideprep2(cave 0x1000052E0 + 16)
    orig_baseprep = (baseprep_fn)VA(0x100005308ULL); slots[34] = (void *)hook_baseprep;   // baseprep(cave 0x1000052F8 + 16)
    orig_ttouch = (ttouch_fn)VA(0x100005320ULL); slots[35] = (void *)hook_ttouch;          // ttouch(cave 0x100005310 + 16): Taiko の編集領域タッチ(小節線ツール)
    orig_trelayout = (trelayout_fn)VA(0x100005338ULL); slots[36] = (void *)hook_trelayout; // trelayout(cave 0x100005328 + 16): Taiko の再配置(FX プレビュー)
    orig_trefresh = (trefresh_fn)VA(0x100005350ULL); slots[37] = (void *)hook_trefresh;     // trefresh(cave 0x100005340 + 16): Taiko の毎フレーム
    orig_tbars = (tbars_fn)VA(0x100005368ULL); slots[38] = (void *)hook_tbars;              // tbars(cave 0x100005358 + 16): Taiko の小節線スプライト
    orig_tgrid = (tgrid_fn)VA(0x100005380ULL); slots[39] = (void *)hook_tgrid;              // tgrid(cave 0x100005370 + 16): Taiko のグリッド再構築
    orig_tgridpos = (tgridpos_fn)VA(0x100005398ULL); slots[40] = (void *)hook_tgridpos;     // tgridpos(cave 0x100005388 + 16): Taiko のグリッド位置
    orig_pathhit = (pathhit_fn)VA(0x1000053C8ULL); slots[42] = (void *)hook_pathhit;          // pathhit(cave 0x1000053B8 + 16): 重複 beat のスライドの当たり判定の誤判定を外す
    slots[43] = (void *)hook_notecmp;                                                              // notecmp(cave 0x1000053D0): 分数の比較を 64bit に
    orig_chit = (chit_fn)VA(0x100005428ULL); slots[46] = (void *)hook_chit;                           // chit(cave 0x100005418 + 16)
    orig_s2wipe = (s2w_fn)VA(0x1000053F8ULL); slots[44] = (void *)hook_s2wipe;                     // s2wipe(cave 0x1000053E8 + 16)
    orig_segadd = (segadd_fn)VA(0x100005410ULL); slots[45] = (void *)hook_segadd;                  // segadd(cave 0x100005400 + 16)
    orig_snote = (keytouch_fn)VA(0x1000053B0ULL); slots[41] = (void *)hook_s_note;               // snote(cave 0x1000053A0 + 16): Slide のノートへのタッチ
    {
        NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
        gTurboM = (int)[u integerForKey:@"bm.turbo.m"]; gTurboP = (int)[u integerForKey:@"bm.turbo.p"];
        gTurboOn = [u boolForKey:@"bm.turbo.on"];
        NSInteger sp = [u integerForKey:@"bm.turbo.spd"]; gTurboSpd = (sp >= 50 && sp <= 200) ? (int)sp : 100;
    }
    gNoRehold = [NSUserDefaults.standardUserDefaults objectForKey:@"bm.no_rehold"] ? [NSUserDefaults.standardUserDefaults boolForKey:@"bm.no_rehold"] : YES;
    gLateFix = [NSUserDefaults.standardUserDefaults objectForKey:@"bm.skip_late_miss"] ? [NSUserDefaults.standardUserDefaults boolForKey:@"bm.skip_late_miss"] : YES;
    {
        NSInteger mv = [NSUserDefaults.standardUserDefaults objectForKey:@"bm.master_volume"] ? [NSUserDefaults.standardUserDefaults integerForKey:@"bm.master_volume"] : 100;
        gMasterVol = (int)(mv < 0 ? 0 : (mv > 100 ? 100 : mv));
    }
    slots[6] = (void *)hook_s_refresh;
    slots[7] = (void *)hook_s_zoom;
    slots[8] = (void *)hook_s_touch;
    bm_expose_files();
    bm_web_external_install();
    bm_guard_bzero();
    {
        NSInteger a = [NSUserDefaults.standardUserDefaults integerForKey:@"bm.spec.alpha"];
        if (a >= 25 && a <= 255) gSpecAlpha = (int)a;
    }
    os_log(L, "BetterMalody loaded slide=0x%lx", (unsigned long)gSlide);
}
