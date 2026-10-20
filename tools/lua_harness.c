// Lua プラグインをゲーム無しで通すハーネス(モックの譜面)。未対応 API・実行時エラーの洗い出し用。
// usage: lua_harness <mode 0|7> plugin.lua ...    ビルド: tools/lua_harness.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua_host.h"

static int gMode = 7, gAdded = 0, gCalls = 0;
static int NN = 6;
static int m_sel(int *ids, int max) {   // 既定は {1,2}。環境変数 HARNESS_SEL="3,0" のように指定すると差し替え(mode 7 は id%3==0 が Slide)
    const char *e = getenv("HARNESS_SEL"); int n = 0;
    if (e) { for (const char *p = e; *p && n < max; ) { ids[n++] = atoi(p); while (*p && *p != ',') p++; if (*p == ',') p++; } return n; }
    ids[0] = 1; ids[1] = 2; return 2;
}
static int m_count(void) { return NN + gAdded; }
static int m_at(int i) { return i >= 0 && i < NN + gAdded ? i : -1; }
static int m_type(int id) { return gMode == 7 ? (id % 3 == 0 ? 2048 : (id % 3 == 2 ? 1024 : 1)) : (id % 3 == 0 ? 128 : 1); }   // Slide = 2048 / Wipe = 1024 / Tap = 1(Key の Hold = 128)
static int m_x(int id) { return gMode == 7 ? id * 40 : id % 4; }
static int m_w(int id) { (void)id; return 60; }
static int m_flag(int id) { (void)id; return 0; }
static int m_beat(int id, int head, LHBeat *o) { *o = (LHBeat){ id + (head ? 0 : 1), 0, 4 }; return 1; }
static double m_time(int id, int head) { return id * 500.0 + (head ? 0 : 250); }
static int m_setbeat(int id, int head, LHBeat b) { (void)id; (void)head; (void)b; gCalls++; return 1; }
static int m_setx(int id, int x) { (void)id; (void)x; gCalls++; return 1; }
static int m_setw(int id, int w) { (void)id; (void)w; gCalls++; return 1; }
static int m_del(int id) { (void)id; gCalls++; return 1; }
static int m_add(int type) { (void)type; return NN + gAdded++; }
static int m_select(int id) { (void)id; return 1; }
static int m_segc(int id) { (void)id; return gMode == 7 ? 3 : 0; }
static int m_segb(int id, int i, LHBeat *o) { (void)id; *o = (LHBeat){ i, 0, 1 }; return 1; }
static int m_segx(int id, int i) { (void)id; return i * 30; }
static void m_curbeat(LHBeat *o) { *o = (LHBeat){ 2, 0, 1 }; }
static double m_curtime(void) { return 1000; }
static int m_curdiv(void) { return 4; }
static int gQuiet;
static void m_toast(const char *s) { if (!gQuiet) printf("    toast: %s\n", s); }
static const char *m_info(const char *k) { return !strcmp(k, "key") ? "4" : !strcmp(k, "bpm") ? "120" : "x"; }
static int m_bpmc(void) { return 1; }
static int m_bpmat(int i, LHBeat *b, double *bpm, double *dl) { (void)i; *b = (LHBeat){ 0, 0, 1 }; *bpm = 120; *dl = 0; return 1; }
static int m_fxc(void) { return 0; }
static int m_fxat(int i, LHBeat *b, const char **t, double *v) { (void)i; (void)b; (void)t; (void)v; return 0; }
static int m_segadd(int id, LHBeat r) { (void)id; if (getenv("HARNESS_V")) printf("    seg_add %d:%d/%d\n", r.m, r.num, r.den); gCalls++; return 1; }
static int m_segsetx(int id, int i, int x) { (void)id; if (getenv("HARNESS_V")) printf("    seg_x[%d]=%d\n", i, x); gCalls++; return 1; }
static int m_segclear(int id) { (void)id; gCalls++; return 1; }
// データ / ファイルはプラグイン間で保持する(ES3 の Draw → Edit のような受け渡しを再現)
typedef struct { char k[64]; char *v; size_t n; } KV;
static KV gData[32], gFiles[32];
static KV *kv_find(KV *t, const char *k, int create) {
    for (int i = 0; i < 32; i++) if (t[i].k[0] && !strcmp(t[i].k, k)) return &t[i];
    if (create) for (int i = 0; i < 32; i++) if (!t[i].k[0]) { snprintf(t[i].k, 64, "%s", k); return &t[i]; }
    return NULL;
}
static const char *m_dget(const char *k, const char *p) { (void)p; KV *e = kv_find(gData, k, 0); return e ? e->v : NULL; }
static void m_dset(const char *k, const char *p, const char *v) { (void)p; KV *e = kv_find(gData, k, 1); if (e) { free(e->v); e->v = strdup(v); } }
static const char *m_fread(const char *n, size_t *len) { KV *e = kv_find(gFiles, n, 0); if (!e) { *len = 0; return NULL; } *len = e->n; return e->v; }
static int m_fwrite(const char *n, const char *d, size_t len) {
    KV *e = kv_find(gFiles, n, 1); if (!e) return 0;
    free(e->v); e->v = malloc(len + 1); memcpy(e->v, d, len); e->v[len] = 0; e->n = len; return 1;
}
static int gClk[4] = { -1, 3, 1, 4 };   // HARNESS_CLICK="x,m,num,den" で差し替え
static int m_clickx(void) { return gClk[0] >= 0 ? gClk[0] : (gMode == 7 ? 200 : 2); }
static void m_clickbeat(LHBeat *o, int fine) { (void)fine; *o = (LHBeat){ gClk[1], gClk[2], gClk[3] }; }
static void m_fxadd(LHBeat b, const char *t, double v) { (void)b; printf("    AddEffect %s %g\n", t, v); }
static void m_fxdel(LHBeat b, const char *t) { (void)b; printf("    DeleteEffect %s\n", t); }
static void m_bpmadd(LHBeat b, double bpm, double d) { (void)b; (void)d; printf("    AddTime %g\n", bpm); }
static void m_bpmdel(LHBeat b) { (void)b; printf("    DeleteTime\n"); }
static void m_short(int id) { printf("    CallShortcut %d\n", id); }
static int m_modadd(int k, const char *n, const char *a) { printf("    AddModule kind=%d %s %.30s\n", k, n, a); return 1; }
static void m_modnum(const char *n, const char *k, double v) { (void)n; (void)k; (void)v; }
static void m_modstr(const char *n, const char *k, const char *v) { (void)n; (void)k; (void)v; }
static void m_modbeat(const char *n, const char *k, LHBeat b) { (void)n; (void)k; (void)b; }
static void m_log(const char *t) { if (!gQuiet) printf("    log: %s\n", t); }
static const char *m_cget(const char *n) { KV *e = kv_find(gData, n, 0); (void)e; return !strcmp(n, "k") ? "v" : NULL; }
static void m_cset(const char *n, const char *v) { printf("    WriteChartCustom %s=%s\n", n, v); }
typedef struct { void *s; char tag[64]; } Rq;
static Rq gRq[8]; static int gRqn;
static void m_req(void *s, const char *tag, const char *url, const char *m, const char *d) {
    printf("    DoRequest %s %s %s %.20s\n", tag, url, m, d);
    if (gRqn < 8) { gRq[gRqn].s = s; snprintf(gRq[gRqn].tag, 64, "%s", tag); gRqn++; }
}
static void m_tip(const char *t) { if (!gQuiet) printf("    tip: %s\n", t); }
static void m_modcol(const char *n, int r, int g, int b) { (void)n; (void)r; (void)g; (void)b; }
static void m_modrm(const char *n) { (void)n; }
static int m_modex(const char *n) { (void)n; return 0; }
static void m_batch(int b) { (void)b; }
static void m_seekb(LHBeat b) { (void)b; printf("    SeekTo beat\n"); }
static void m_seekms(double ms) { printf("    SeekTo %g\n", ms); }
static char gInputAnswer[64] = "";
typedef struct { void *s; int id; char t[96], d[96]; } In;
static In gQ[8]; static int gQn;
static void m_input(void *s, int id, const char *t, const char *d) {
    printf("    GetUserInput(\"%.40s\", \"%.20s\")\n", t, d);
    if (gQn < 8) { gQ[gQn].s = s; gQ[gQn].id = id; snprintf(gQ[gQn].t, 96, "%s", t); snprintf(gQ[gQn].d, 96, "%s", d); gQn++; }
}

static void m_choice(void *s, int id, int kind, const char *t, const char *const *o, int n) {
    printf("    GetUser%s(\"%s\", %d options: %s%s)\n", kind == 1 ? "Select" : "Option", t, n, o[0], n > 1 ? "|..." : "");
    if (gQn < 8) { gQ[gQn].s = s; gQ[gQn].id = id; gQ[gQn].t[0] = 0; snprintf(gQ[gQn].d, 96, "#%d", kind); gQn++; }
}
static void m_slider(void *s, int id, int n, const char *const *t, const double *mn, const double *mx, const double *df, const double *st) {
    (void)st;
    printf("    GetUserSlider%s(\"%s\", %g..%g def %g)\n", n == 2 ? "2" : "", t[0], mn[0], mx[0], df[0]);
    if (gQn < 8) { gQ[gQn].s = s; gQ[gQn].id = id; gQ[gQn].t[0] = 0; snprintf(gQ[gQn].d, 96, "$%d", n); gQn++; }
}
static void m_par(const char *n, const char *p) { printf("    parent %s -> %s\n", n, p); }
static LHHost mk(void) {
    LHHost h = { 0 };
    h.mode = gMode; h.sel = m_sel; h.count = m_count; h.at = m_at; h.type = m_type; h.x = m_x; h.width = m_w; h.flag = m_flag;
    h.beat = m_beat; h.time_ms = m_time; h.set_beat = m_setbeat; h.set_x = m_setx; h.set_width = m_setw; h.del = m_del;
    h.add = m_add; h.select = m_select; h.seg_count = m_segc; h.seg_beat = m_segb; h.seg_x = m_segx; h.cur_beat = m_curbeat;
    h.cur_time = m_curtime; h.cur_div = m_curdiv; h.toast = m_toast; h.chart_info = m_info; h.bpm_count = m_bpmc;
    h.bpm_at = m_bpmat; h.fx_count = m_fxc; h.fx_at = m_fxat; h.seg_add = m_segadd; h.seg_set_x = m_segsetx;
    h.seg_clear = m_segclear; h.data_get = m_dget; h.data_set = m_dset; h.file_read = m_fread; h.file_write = m_fwrite;
    h.click_x = m_clickx; h.click_beat = m_clickbeat; h.fx_add = m_fxadd; h.fx_del = m_fxdel; h.bpm_add = m_bpmadd;
    h.bpm_del = m_bpmdel; h.shortcut = m_short; h.mod_add = m_modadd; h.mod_set_num = m_modnum; h.mod_set_str = m_modstr;
    h.mod_set_beat = m_modbeat; h.mod_remove = m_modrm; h.mod_exists = m_modex; h.batch = m_batch; h.seek_beat = m_seekb;
    h.seek_ms = m_seekms; h.input = m_input; h.mod_set_color = m_modcol; h.tip = m_tip; h.choice = m_choice; h.slider = m_slider; h.mod_set_parent = m_par; h.log = m_log; h.custom_get = m_cget; h.custom_set = m_cset; h.request = m_req;
    return h;
}
static void drain(LHSession *s, const LHHost *h) {
    (void)h; char err[300];
    for (int g = 0; g < 8 && gQn; g++) {
        In e = gQ[0]; memmove(gQ, gQ + 1, sizeof(In) * (size_t)(--gQn));
        err[0] = 0;
        if (e.d[0] == '$') {
            double v[2] = { 7, 9 };
            if (lh_slider_deliver(e.s, e.id, e.d[1] - '0', v, err, sizeof err)) printf("    ERROR(slider cb): %s\n", err);
            continue;
        }
        if (e.d[0] == '#') {
            if (lh_choice_deliver(e.s, e.id, e.d[1] - '0', 1, err, sizeof err)) printf("    ERROR(choice cb): %s\n", err);
            continue;
        }
        const char *ans = gInputAnswer[0] ? gInputAnswer : e.d[0] ? e.d : "1";
        if (lh_input_deliver(e.s, e.id, ans, err, sizeof err)) printf("    ERROR(input cb): %s\n", err);
    }
    for (int g = 0; g < 8 && gRqn; g++) {
        Rq q = gRq[0]; memmove(gRq, gRq + 1, sizeof(Rq) * (size_t)(--gRqn));
        err[0] = 0;
        if (lh_response(q.s, q.tag, "{\"mock\":1}", err, sizeof err)) printf("    ERROR(OnResponse): %s\n", err);
    }
    (void)s;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s mode plugin.lua...\n", argv[0]); return 2; }
    gMode = atoi(argv[1]);
    { const char *c = getenv("HARNESS_CLICK"); if (c) sscanf(c, "%d,%d,%d,%d", &gClk[0], &gClk[1], &gClk[2], &gClk[3]); }
    int bad = 0;
    for (int i = 2; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { printf("%s: cannot open\n", argv[i]); continue; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        char *src = malloc((size_t)n + 1); fread(src, 1, (size_t)n, f); fclose(f);
        LHInfo in; lh_inspect(src, (size_t)n, argv[i], &in);
        printf("== %s | name=%s mode=%d type=%d req=%s run=%d tool=%d %s%s\n", argv[i], in.name, in.mode, in.type, in.require,
               in.has_run, in.has_tool, in.err[0] ? "ERR:" : "", in.err);
        if (in.err[0]) { bad++; free(src); continue; }
        if (in.mode >= 0 && in.mode != gMode) { printf("   (mode mismatch, skipped)\n"); free(src); continue; }
        LHHost h = mk();
        char err[300] = {0};
        gQn = 0; gAdded = 0; gCalls = 0;
        int interact = in.type == 2 || (!in.has_run && in.has_tool);
        if (!interact) {
            LHSession *keep = NULL;
            int rc = lh_run(src, (size_t)n, argv[i], &h, err, sizeof err, &keep);
            if (rc) { printf("   RUN ERROR: %s\n", err); bad++; }
            for (int g = 0; g < 6 && (gQn || gRqn) && !rc; g++) {
                drain(keep, &h);
                if (keep && !lh_pending(keep)) { lh_close(keep); keep = NULL; }
            }
            if (keep) lh_close(keep);
            printf("   run ok, edits=%d adds=%d\n", gCalls, gAdded);
        } else {
            LHSession *s = lh_open(src, (size_t)n, argv[i], &h, err, sizeof err);
            if (!s) { printf("   OPEN ERROR: %s\n", err); bad++; free(src); continue; }
            const char *evs[] = { "OnActive", "OnClick", "OnDragStart", "OnDragMove", "OnDragMove", "OnDragEnd", "OnClick", "OnDeactive" };
            for (unsigned k = 0; k < sizeof evs / sizeof *evs; k++) {
                err[0] = 0;
                if (lh_call(s, evs[k], NULL, 0, err, sizeof err)) { printf("   %s ERROR: %s\n", evs[k], err); bad++; }
                drain(s, &h);
            }
            lh_close(s);
            printf("   interact ok, edits=%d adds=%d\n", gCalls, gAdded);
        }
        free(src);
    }
    printf("--- errors: %d\n", bad);
    return bad != 0;
}
