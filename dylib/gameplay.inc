// ---- プレイ面の改善 ----
// 遅ミスの素通し(Key モード): 遅れて押したとき、既に判定窓を過ぎた音符(遅ミス)が押下を吸ってしまい、
// 次の音符が判定されない「ミスハマり」を防ぐ。過ぎた音符はその場でミスとして確定し、押下は次の音符へ回す。
//   sub_1001197F8(scene, note, type, lane) = 押下/離しの判定入口(type 0 = 押下)
//   判定器 = *(scene+992)(JudgeKey: +32 = レベル補正, +56 = Bad の窓)/ 現在時刻 = *(double *)(scene+1072)(ms)
//   音符の並び = scene+2160 の deque(HitObject*)/ 音符: int[20] = 時刻, int[30] = 状態(bit0 = 判定済み), u8@+128 = レーン
static int gTurboSpd = 100;                   // (S) 音楽速度(%)
static BOOL gTurboOn;                         // TURBO モッド選択中(モッドのビット 11)
// 選択状態の正はプレイ設定のモッド(アプリ再起動後も保持される)。MOD 画面を開かなくても使えるよう、各所で設定から同期する。読み込み画面用に NSUserDefaults へも控える。
static void turbo_sync(const uint8_t *sets) {
    if (!sets) return;
    BOOL on = (*(const int *)(sets + 4) & 0x800) != 0;
    if (on == gTurboOn) return;
    gTurboOn = on;
    [NSUserDefaults.standardUserDefaults setBool:on forKey:@"bm.turbo.on"];
}
static int gTurboM, gTurboP;                  // Turbo の (M) / (P)(turbo.inc)
static BOOL gLateFix = YES;
static BOOL gNoRehold = YES;   // LN を終端より前に離したら、再ホールドを受け付けずにそのまま流す(ミス扱い)
typedef intptr_t (*kjudge_fn)(void *, int *, int, intptr_t);
static kjudge_fn orig_kjudge;

static int *kj_next_unjudged(void *scene, int lane) {   // レーンの最初の未判定音符(標準の選択 sub_100025F18 と同じ走査)
    uint64_t *dq = (uint64_t *)((uint8_t *)scene + 2160);
    if (!dq[1] || dq[2] == dq[1]) return NULL;
    void **map = (void **)dq[1];
    uint64_t start = dq[4], size = dq[5];
    typedef void *(*dc_fn)(const void *, const void *, const void *, long);
    static dc_fn dc;
    if (!dc) dc = (dc_fn)dlsym(RTLD_DEFAULT, "__dynamic_cast");
    for (uint64_t i = 0; i < size && i < 4096; i++) {
        uint64_t idx = start + i;
        void **blk = (void **)map[idx >> 9];
        if (!blk) break;
        void *p = blk[idx & 0x1FF];
        if (!p) continue;
        uint8_t *k = dc ? (uint8_t *)dc(p, (const void *)VA(0x1009340E0ULL), (const void *)VA(0x100934110ULL), 0) : NULL;
        if (!k || k[128] != lane) continue;
        int *n = (int *)k;
        if (n[28] & 0x80) { if ((n[30] & 8) == 0 && (n[30] & 1) == 0) return n; }
        else if ((n[30] & 1) == 0) return n;
    }
    return NULL;
}
static intptr_t hook_kjudge(void *scene, int *note, int type, intptr_t lane) {
    {   // 診断: 最初の 80 回の呼び出し(入口が通っているか / 種別 / 音符の状態)
        static int n;
        if (n < 80) {
            n++;
            os_log(L, "kjudge#%d type=%d lane=%ld note=%p flags=0x%x state=0x%x time=%d end=%d t=%.0f", n, type, (long)lane, note,
                   note ? note[28] : 0, note ? note[30] : 0, note ? note[20] : 0, note ? note[26] : 0, scene ? *(double *)((uint8_t *)scene + 1072) : -1.0);
        }
    }
    if (type != 0 && note && scene && gNoRehold) {
        // 離し: 標準は終端の 130ms より前の離しを「ホールド途切れ」(0x40000)とするだけで、音符は進行中(状態 bit0 のみ)のまま残り、
        // 再び押すと a2[31] = 時刻 + 100 で復帰する(終端でのミスハマりの原因)。早離しは音符を終了済み(bit3)にして復帰させない
        intptr_t r = orig_kjudge(scene, note, type, lane);
        if ((note[28] & 0x80) && (note[30] & 9) == 1) {
            int t = (int)*(double *)((uint8_t *)scene + 1072);
            if (t < note[26] - 130) {
                note[30] |= 8;
                os_log(L, "LN early release: lane=%ld end=%dms release=%dms -> finished (no re-hold)", (long)lane, note[26], t);
            }
        }
        return r;
    }
    if (type != 0 || !note || !gLateFix || !scene) return orig_kjudge(scene, note, type, lane);
    uint8_t *s = (uint8_t *)scene;
    int *judge = *(int **)(s + 992);
    if (!judge) return orig_kjudge(scene, note, type, lane);
    double t = *(double *)(s + 1072);
    int bad = judge[14] + judge[8];
    intptr_t r = 0;
    for (int guard = 0; note && guard < 16; guard++) {
        BOOL overdue = (note[30] & 1) == 0 && (int)t - note[20] > bad;
        if (!overdue) { r = orig_kjudge(scene, note, type, lane); return r; }
        orig_kjudge(scene, note, type, lane);            // 過ぎた音符はミスとして確定(押下は吸わせない)
        os_log(L, "late-miss passthrough: lane=%ld note=%dms tap=%dms late=%dms bad=%d state=%d", (long)lane, note[20], (int)t, (int)t - note[20], bad, note[30]);
        if ((note[30] & 1) == 0) break;                   // 確定できなかった(想定外): 無限ループを避ける
        note = kj_next_unjudged(scene, (int)lane);
    }
    return orig_kjudge(scene, note, type, lane);          // 次の音符(無ければ空押し)へ押下を渡す
}


// ---- Key 本体の押下入口(ModeLayerKey): sub_10005D060(scene, lane, type) — 実プレイで使われる経路(UIS 側のフックは使われない) ----
//   scene+984 = 判定器 / +1064 = 現在時刻(double ms) / +1392.. = 音符の deque(要素 → *(+16) → *(+40) = HitObjectKey)
//   押下の判定は 判定器の関数テーブル +24(型: (judge, note, int 時刻))、結果は scene の関数テーブル +1480 へ渡す
typedef intptr_t (*kpress_fn)(void *, intptr_t, int);
static kpress_fn orig_kpress;
static int *kp_note_at(void *scene, uint64_t idx) {
    uint8_t *s = (uint8_t *)scene;
    void **map = *(void ***)(s + 1392);
    if (!map) return NULL;
    uint64_t *blk = (uint64_t *)map[idx >> 9];
    if (!blk) return NULL;
    uint8_t *x = (uint8_t *)blk[idx & 0x1FF];
    if (!x) return NULL;
    uint8_t *y = *(uint8_t **)(x + 16);
    return y ? *(int **)(y + 40) : NULL;
}
static int *kp_first_candidate(void *scene, int lane) {   // 標準の選択と同じ: レーン内の最初の「押下を受ける」音符
    uint8_t *s = (uint8_t *)scene;
    if (*(void **)(s + 1400) == *(void **)(s + 1392)) return NULL;
    uint64_t start = *(uint64_t *)(s + 1416), size = *(uint64_t *)(s + 1424);
    for (uint64_t i = 0; i < size && i < 4096; i++) {
        int *n = kp_note_at(scene, start + i);
        if (!n || ((uint8_t *)n)[128] != lane) continue;
        if (n[28] & 0x80) { if ((n[30] & 8) == 0) return n; }
        else if ((n[30] & 1) == 0) return n;
    }
    return NULL;
}
// Turbo (M): Key の早押しミスの窓(jd[15])を Bad 窓(jd[14])に揃える = それより早い押下は無視。標準値は JudgeKey ごとに最初に記録する。
//   (判定窓の速度補正は全モード共通で turbo_scale_judge が行う)
static void kp_apply_windows(void *scene) {
    static int *lastJudge; static int origEarly;
    uint8_t *s = (uint8_t *)scene;
    int *jd = *(int **)(s + 984);
    if (!jd) return;
    turbo_sync(*(uint8_t **)(s + 1008));
    if (jd != lastJudge) { lastJudge = jd; origEarly = jd[15]; }
    jd[15] = (gTurboOn && gTurboM) ? jd[14] : origEarly;
}
static void *gKScene; static intptr_t gKLane;   // 直近の kpress(JudgeKey の呼び出し元)
static intptr_t hook_kpress(void *scene, intptr_t lane, int type) {
    gKScene = scene; gKLane = lane;
    if (scene) kp_apply_windows(scene);
    if (type == 0 && gLateFix && scene) {
        uint8_t *s = (uint8_t *)scene;
        void *judge = *(void **)(s + 984);
        if (judge && *(void **)(s + 1400) != *(void **)(s + 1392)) {
            double t = *(double *)(s + 1064);
            int bad = ((int *)judge)[14] + ((int *)judge)[8];
            for (int guard = 0; guard < 16; guard++) {
                int *n = kp_first_candidate(scene, (int)lane);
                if (!n || (n[30] & 1) || (int)t - n[20] <= bad) break;      // 候補が無い / 既に進行中 / 窓内 → 標準の処理へ
                // 窓を過ぎた未判定の音符: 標準の判定(= ミス)をここで確定し、押下は次の音符へ回す
                typedef intptr_t (*tap_fn)(void *, int *, int);
                intptr_t r = ((tap_fn)(*(void ***)judge)[3])(judge, n, (int)t);
                if (r != 0x40000000) ((void (*)(void *, int *, intptr_t))(*(void ***)scene)[1480 / 8])(scene, n, r);
                if (n[28] & 0x80) n[30] |= 8;                                // 頭を逃した LN も押下を吸わない
                os_log(L, "late-miss passthrough: lane=%ld note=%dms tap=%dms late=%dms bad=%d result=0x%lx", (long)lane, n[20], (int)t, (int)t - n[20], bad, (long)r);
                if ((n[30] & 1) == 0) break;
            }
        }
    }
    return orig_kpress(scene, lane, type);
}

// JudgeKey の離し判定(sub_10014C794): 終端の 130ms より前の離し = 0x40000(ホールド途切れ)。標準は音符を進行中のまま残し、再ホールドで復帰する。
//   早離しは音符を終了済み(state bit3)にして、復帰させず、押下を吸わせない
typedef long (*jrel_fn)(void *, int *, int);
static jrel_fn orig_jrel;
static long hook_jrel(void *judge, int *note, int tm) {
    long r = orig_jrel(judge, note, tm);
    if (r == 0x40000 && gNoRehold && note && (note[28] & 0x80)) {
        note[30] |= 8;
        os_log(L, "LN early release: end=%dms release=%dms -> finished (no re-hold)", note[26], tm);
    }
    return r;
}
