// ---- Turbo モッド(Malody V の Turbo モードの再現): MOD 画面の 12 個目(id 11)+ 選択中に出るメニュー ----
// 選択肢(V の言語表 1120〜1124): (M) 早すぎるミスを無視 / (P) 一時停止を無効 / (S) 音楽速度を自由に。
// 「複数の指」は不要(ユーザー指示)。(M)は判定器の早押しミス窓を Bad 窓に揃える。
// MOD 画面: 構築 sub_1000AF494 / タッチ sub_1000B0078(a1=画面, a1+688=プレイ設定(+4=選択中モッドのビット), a1+696=右の大きなアイコン)
#define TURBO_ID 11
static struct { void *scene, *panel, *lbl[3], *val; } gTurbo;

static const char *hook_modname_impl(int id, const char *(*orig)(int)) { return id == TURBO_ID ? "TURBO" : orig(id); }
typedef const char *(*modname_fn)(int);
static modname_fn orig_modname;
static const char *hook_modname(int id) { return hook_modname_impl(id, orig_modname); }

static void turbo_labels(void) {
    char b[80];
    const char *m = gTurboM ? "ON" : "OFF", *p = gTurboP ? "ON" : "OFF";
    if (gTurbo.lbl[1]) { snprintf(b, sizeof b, "(M) Ignore early miss: %s", m); set_label_text(gTurbo.lbl[1], b); }
    if (gTurbo.lbl[2]) { snprintf(b, sizeof b, "(P) Disable pause: %s", p); set_label_text(gTurbo.lbl[2], b); }
    if (gTurbo.val) { snprintf(b, sizeof b, "x%.2f", gTurboSpd / 100.0); set_label_text(gTurbo.val, b); }
}
static void turbo_save(void) {
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    [u setInteger:gTurboM forKey:@"bm.turbo.m"];
    [u setInteger:gTurboP forKey:@"bm.turbo.p"]; [u setInteger:gTurboSpd forKey:@"bm.turbo.spd"];
}
static void turbo_walk(void *n, int depth, int *count) {
    if (!n || depth > 12 || *count > 4000) return;
    (*count)++;
    int t = cfg_tag(n);
    if (t == 40 && !gTurbo.panel) gTurbo.panel = n;
    else if (t >= 51 && t <= 53 && !gTurbo.lbl[t - 51]) gTurbo.lbl[t - 51] = n;
    else if (t == 55 && !gTurbo.val) gTurbo.val = n;
    nvec *ch = node_children(n);
    if (!ch) return;
    for (void **p = ch->begin; p != ch->end; p++) turbo_walk(*p, depth + 1, count);
}
static void turbo_refresh(void *scene) {   // 選択状態(ビット 11)と、右に表示中のモッド(= TURBO ならメニューを出す)
    uint8_t *s = (uint8_t *)scene;
    uint8_t *sets = *(uint8_t **)(s + 688);
    void *big = *(void **)(s + 696);
    turbo_sync(sets);
    int shown = big ? cfg_tag(big) : -1;
    if (gTurbo.panel) VT(gTurbo.panel, 336, void (*)(void *, int))(gTurbo.panel, shown == TURBO_ID ? 1 : 0);
    static int dn;
    if (gTurbo.panel && shown == TURBO_ID && dn < 6) {   // 診断: パネルの状態(表示 / 大きさ / 位置 / 親 / 子の数)
        dn++;
        void *pn = gTurbo.panel;
        float *sz = VT(pn, 328, float *(*)(void *))(pn);
        void *par = VT(pn, 584, void *(*)(void *))(pn);
        nvec *ch = node_children(pn);
        os_log(L, "turbo: panel vis=%d size=(%.0f,%.0f) pos=(%.0f,%.0f) parent=%p pvis=%d kids=%zu", VT(pn, 344, int (*)(void *))(pn), sz ? sz[0] : -1, sz ? sz[1] : -1,
               VT(pn, 192, float (*)(void *))(pn), VT(pn, 208, float (*)(void *))(pn), par, par ? VT(par, 344, int (*)(void *))(par) : -1, ch ? (size_t)(ch->end - ch->begin) : 0);
        if (par) { float *ps = VT(par, 328, float *(*)(void *))(par); os_log(L, "turbo: parent size=(%.0f,%.0f) tag=%d", ps ? ps[0] : -1, ps ? ps[1] : -1, cfg_tag(par)); }
    }
}

typedef intptr_t (*modbuild_fn)(void *);
static modbuild_fn orig_modbuild;
static intptr_t hook_modbuild(void *scene) {
    intptr_t r = orig_modbuild(scene);
    ns_modbuild_post(scene);                                           // 速さの文字を小数で(notespeed.inc)
    memset(&gTurbo, 0, sizeof gTurbo);
    gTurbo.scene = scene;
    int cnt = 0;
    turbo_walk(scene, 0, &cnt);
    turbo_labels();
    turbo_refresh(scene);
    os_log(L, "turbo: bound panel=%p lbl=%p/%p/%p val=%p (visited %d) on=%d", gTurbo.panel, gTurbo.lbl[0], gTurbo.lbl[1], gTurbo.lbl[2], gTurbo.val, cnt, gTurboOn);
    return r;
}
typedef intptr_t (*modsel_fn)(void *, unsigned, void *);
static modsel_fn orig_modsel;
static intptr_t hook_modsel(void *scene, unsigned ev, void *node) {   // sub_1000B0078
    if (node && scene == gTurbo.scene) {
        int tag = cfg_tag(node);
        if (tag >= 40 && tag <= 46) {
            if (ev == 0) {
                switch (tag) {
                    case 42: gTurboM = !gTurboM; break;
                    case 43: gTurboP = !gTurboP; break;
                    case 45: if (gTurboSpd > 50) gTurboSpd -= 5; break;
                    case 46: if (gTurboSpd < 200) gTurboSpd += 5; break;
                }
                turbo_labels(); turbo_save();
            }
            return 1;                                   // 背景(暗幕)も含めて下へ通さない
        }
    }
    intptr_t r = orig_modsel(scene, ev, node);
    if (scene == gTurbo.scene) {
        turbo_refresh(scene);
        static int n;
        if (ev != 7 && n < 40 && node) { n++; void *big = *(void **)((uint8_t *)scene + 696); os_log(L, "turbo: modsel ev=%u tag=%d shown=%d on=%d panel=%p", ev, cfg_tag(node), big ? cfg_tag(big) : -1, gTurboOn, gTurbo.panel); }
    }
    return r;
}

// ---- (S) 音楽速度 / (P) 一時停止の無効 ----
// 再生開始 sub_10007D658(a1=再生管理: a1+712 = プレイ設定(+4 = モッド), a1+704 = 音楽チャンネル, a1+808 = 既定の再生速度(float), a1+880 = 状態 / 8 = 一時停止中)。
// DASH(0x10)/RUSH(0x20)/SLOW(0x100)は a1+808 を 1.2 / 1.5 / 0.8 に上書きして sub_1001F5A84(音楽チャンネル, 速度)へ渡す。それ以外は a1+808 をそのまま使うので、ここを差し替える。
// ノーツの流れる速さ(Key の a1+1236)は、DASH 等では 1/速度 に合わせている(見た目の速さを保つ)。Turbo でも同じ補正を入れて、実時間の流れる速さを一定にする(補正は 1 回分だけ。turbo_scroll_comp)。
#define TURBO_SPEED_MODS (0x10 | 0x20 | 0x100)
typedef intptr_t (*playstart_fn)(void *, float);
static playstart_fn orig_playstart;
static int gPlayMods;   // 直近のプレイのモッド(結果画面の保存可否の判定用)
static int gPlaySpd = 100;   // 直近のプレイの Turbo 音楽速度(%)(結果画面の譜面名に付ける)
static intptr_t hook_playstart(void *a1, float a2) {
    gPlaySpd = 100;
    uint8_t *sets = a1 ? *(uint8_t **)((uint8_t *)a1 + 712) : NULL;
    if (sets) { gPlayMods = *(int *)(sets + 4); turbo_sync(sets); }
    os_log(L, "turbo: playstart on=%d spd=%d mods=0x%x", (int)gTurboOn, gTurboSpd, sets ? *(int *)(sets + 4) : -1);
    if (sets && gTurboOn) {
        // Turbo の (S) は DASH/RUSH/SLOW より優先: 元の処理がモッドから速度を決めないよう、いったん外して呼ぶ
        int saved = *(int *)(sets + 4);
        *(int *)(sets + 4) = saved & ~TURBO_SPEED_MODS;
        *(float *)((uint8_t *)a1 + 808) = gTurboSpd / 100.0f;
        gPlaySpd = gTurboSpd;
        intptr_t r = orig_playstart(a1, a2);
        *(int *)(sets + 4) = saved;
        return r;
    }
    return orig_playstart(a1, a2);
}
// 流れる速さの補正(1/速度)。準備(vtable の関数)は同じ画面で複数回呼ばれうるので、「掛ける」と呼ぶたびに累積して遅くなる。
// 補正前の値を画面ごとに控え、毎回「補正前の値 × 100/速度」を書く(idx 0 = Key / 1 = Slide)。
static void *gScrollScene[2]; static float gScrollBase[2], gScrollApplied[2];
static void turbo_scroll_comp(int idx, void *a1, int off, int mods) {
    float *f = (float *)((uint8_t *)a1 + off), cur = *f;
    float base = (gScrollScene[idx] == a1 && cur == gScrollApplied[idx]) ? gScrollBase[idx] : cur;
    *f = base * 100.0f / gTurboSpd;
    gScrollScene[idx] = a1; gScrollBase[idx] = base; gScrollApplied[idx] = *f;
    os_log(L, "turbo: scroll factor[%d] %.3f -> %.3f (base %.3f, mods=0x%x)", idx, cur, *f, base, mods);
}
typedef intptr_t (*prepkey_fn)(void *, void *);
static prepkey_fn orig_prepkey;
static intptr_t hook_prepkey(void *a1, void *chart) {
    uint8_t *sets = a1 ? *(uint8_t **)((uint8_t *)a1 + 1008) : NULL;
    if (sets) turbo_sync(sets);
    int saved = sets ? *(int *)(sets + 4) : 0, ovr = sets && gTurboOn;
    if (ovr) *(int *)(sets + 4) = saved & ~TURBO_SPEED_MODS;      // 流れる速さの補正も DASH 等ではなく Turbo の速度で決める
    intptr_t r = orig_prepkey(a1, chart);
    if (ovr) *(int *)(sets + 4) = saved;
    if (a1) kp_apply_windows(a1);
    if (r && ovr && gTurboSpd != 100) turbo_scroll_comp(0, a1, 1236, saved);
    return r;
}
typedef intptr_t (*pausetgl_fn)(void *);
static pausetgl_fn orig_pausetgl;
static intptr_t hook_pausetgl(void *a1) {   // sub_10007FBA8: 一時停止 / 再開の切替。「一時停止にする側」だけを止める(開始時の再開などは通す)
    if (a1) turbo_sync(*(uint8_t **)((uint8_t *)a1 + 712));
    if (a1 && gTurboOn && gTurboP) {
        uint8_t *s = (uint8_t *)a1;
        long playing = ((long (*)(void *))VA(0x1001F55B8ULL))(*(void **)(s + 704)) & 1;
        if (playing || s[889] || *(int *)(s + 880) != 8) {
            return 0;
        }
    }
    return orig_pausetgl(a1);
}

// 結果の保存+送信 sub_1001F78DC(a1, a2=結果, a3=譜面ハッシュ, a4=プレイ設定): 標準も a4+54 / a4+40 によるスコア非対象の分岐を持つ("Result -4" で何もせず戻る)。同じ扱いにする。
typedef intptr_t (*savescore_fn)(void *, void *, void *, void *);
static savescore_fn orig_savescore;
static intptr_t hook_savescore(void *a1, void *a2, void *a3, void *a4) {
    if (a4 && (*(int *)((uint8_t *)a4 + 4) & 0x800)) {
        os_log(L, "turbo: score not saved/submitted (mods=0x%x)", *(int *)((uint8_t *)a4 + 4));
        return 0;
    }
    return orig_savescore(a1, a2, a3, a4);
}

// ローカル記録 sub_1001458DC(mgr, 譜面ハッシュ, モード, 記録): 結果画面 sub_10002E1F0 の 0x10002E4A0 からの呼び出し(戻り先 0x10002E4A4)だけを止める。
//   順位表の取得結果の保存など他の呼び出しは通す。
typedef intptr_t (*localrec_fn)(void *, void *, int, void *);
static localrec_fn orig_localrec;
static intptr_t hook_localrec(void *mgr, void *hash, int mode, void *rec) {
    if ((gPlayMods & 0x800) && __builtin_return_address(0) == (void *)VA(0x10002E4A4ULL)) {
        os_log(L, "turbo: local record not saved (mods=0x%x)", gPlayMods);
        return 0;
    }
    return orig_localrec(mgr, hash, mode, rec);
}

// 譜面名(sub_1001B5638: x0=譜面, x8=結果の std::string)。読み込み画面 sub_10007206C(0x10007257C に戻る)とリザルト sub_100034EF8(0x100035774 に戻る)からの呼び出しだけ、
// Turbo の音楽速度を末尾に付ける(" 1.2x")。曲選択など他の呼び出しは触らない。x8 の戻り値は 24B 構造体の返却として受ける(std::string は再配置可能)。
typedef struct { uint64_t w[3]; } Str24;
// 読み込み画面 sub_10007206C は、次のプレイ情報(v6: [0] = 譜面, +8 = モッド)を sub_1001EA438 で取り出す。その戻り値を控え、譜面名の速度表示は実際のモッドで決める
// (gTurboOn は MOD 画面を経ずに解除されたときなどに古いままになり、解除後も「2x」が残った)
typedef void *(*playinfo_fn)(void *);
static playinfo_fn orig_playinfo;
static void *gPendingPlay;
static void *hook_playinfo(void *mgr) { void *r = orig_playinfo(mgr); gPendingPlay = r; return r; }
typedef Str24 (*diffname_fn)(void *);
static diffname_fn orig_diffname;
static Str24 hook_diffname(void *chart) {
    void *ret = __builtin_return_address(0);
    Str24 r = orig_diffname(chart);
    int spd = 100;
    if (ret == (void *)VA(0x100035774ULL)) spd = gPlaySpd;
    else if (ret == (void *)VA(0x10007257CULL)) spd = (gPendingPlay && (*(int *)((uint8_t *)gPendingPlay + 8) & 0x800)) ? gTurboSpd : 100;
    if (spd != 100) {
        char b[24]; snprintf(b, sizeof b, " %gx", spd / 100.0);
        ((void *(*)(void *, const char *))VA(0x100741D7CULL))(&r, b);   // std::string::append(const char *)
    }
    return r;
}

// Slide 系(ModeLayerSlide: sub_1000D0BD8 / 同構造の sub_1001163C8): this+1016 = プレイ設定、this+0x5e8 = 流れる速さの補正(DASH 等が 1.2/1.5/0.8 の逆数を入れる)。Key の sub_1000DD464 と同じ扱い。
typedef intptr_t (*sprep_fn)(void *, void *);
static sprep_fn orig_sprep1, orig_sprep2;
static intptr_t slide_prep(void *a1, void *a2, sprep_fn orig) {
    uint8_t *sets = a1 ? *(uint8_t **)((uint8_t *)a1 + 1016) : NULL;
    if (sets) turbo_sync(sets);
    int saved = sets ? *(int *)(sets + 4) : 0, ovr = sets && gTurboOn;
    if (ovr) *(int *)(sets + 4) = saved & ~TURBO_SPEED_MODS;
    intptr_t r = orig(a1, a2);
    if (ovr) *(int *)(sets + 4) = saved;
    if (r && ovr && gTurboSpd != 100) turbo_scroll_comp(1, a1, 0x5e8, saved);
    return r;
}
static intptr_t hook_sprep1(void *a1, void *a2) { return slide_prep(a1, a2, orig_sprep1); }
static intptr_t hook_sprep2(void *a1, void *a2) { return slide_prep(a1, a2, orig_sprep2); }

// ---- 判定窓を全モードで一定に(実時間) ----
// 判定窓は譜面時間(ms)で固定なので、音楽速度を上げると実時間の窓が狭くなる(下げると広がる)。Turbo は等速と同じ実時間の窓を保つため、速度に比例して窓を広げる/狭める。
// 判定器(JudgeBase 派生)のメンバ int[10..] が窓(ms)。Key/Slide(Fair 含む)= 10..15, Pad = 10..19, Jubeat = 10..17, IIDX = 10..16, Taiko(Fair 含む)= 10..14, LoveLive = 10..11。
// 全モード共通の準備 sub_10010C1C4(this: +984 = 判定器, +1008 = プレイ設定)の後に、判定器の窓を補正する。
typedef intptr_t (*baseprep_fn)(void *, void *);
static baseprep_fn orig_baseprep;
static int judge_win_count(void *vptr) {
    static const struct { uint64_t vt; int n; } T[] = {
        {0x10091fd30ULL, 6}, {0x10091fd70ULL, 6}, {0x10091ff30ULL, 6}, {0x10091ff70ULL, 6},   // Key / KeyFair / Slide / SlideFair
        {0x10091fdb0ULL, 10}, {0x10091fdf0ULL, 8}, {0x10091fe30ULL, 7},                       // Pad / Jubeat / IIDX
        {0x10091fe70ULL, 5}, {0x10091feb0ULL, 5}, {0x10091fef0ULL, 2},                        // Taiko / TaikoFair / LoveLive
    };
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) if (vptr == (void *)VA(T[k].vt)) return T[k].n;
    return 0;
}
static void turbo_scale_judge(void *scene) {
    static int *lastJ; static void *lastV; static int orig[10];
    uint8_t *s = (uint8_t *)scene;
    int *jd = *(int **)(s + 984);
    if (!jd) return;
    void *vp = *(void **)jd;
    int n = judge_win_count(vp);
    if (!n) return;
    if (jd != lastJ || vp != lastV) { lastJ = jd; lastV = vp; for (int i = 0; i < n; i++) orig[i] = jd[10 + i]; }
    uint8_t *sets = *(uint8_t **)(s + 1008);
    turbo_sync(sets);
    double sc = (gTurboOn && gTurboSpd != 100) ? gTurboSpd / 100.0 : 1.0;
    for (int i = 0; i < n; i++) jd[10 + i] = (int)(orig[i] * sc + 0.5);
    if (sc != 1.0) os_log(L, "turbo: judge windows x%.2f (n=%d, bad=%d)", sc, n, n > 4 ? jd[14] : jd[10 + n - 1]);
}
static intptr_t hook_baseprep(void *a1, void *a2) {
    intptr_t r = orig_baseprep(a1, a2);
    if (r && a1) turbo_scale_judge(a1);
    return r;
}
