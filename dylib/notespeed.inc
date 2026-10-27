// ============================================================================
// ノーツスピード: 譜面ごとの保存をやめ、モードごとの値だけにして、0.01 刻みで持てるようにする
//   元の仕組み(IDA):
//     譜面 MaChart+340 = 譜面ごとに保存されたスピード(値 + 100。0 = 無し)/ +344 = モードのフラグ / +129 = 譜面を保存する印
//     取得 vtable+96  = sub_10019E128(chart): 譜面の保存値があればそれ(-100)、無ければ設定の枠(config+192..212 の int × 6)。フラグが 2 / 8 / 128 のときは譜面の値そのもの(AR など)
//     設定 vtable+104 = sub_10019E264(chart, int): chart+340 = 値+100 と +129 の印を立て、設定の枠にも書く(フラグに 0x8A のビットがあれば何もしない)
//     倍率 = sub_1001DFECC(int, double k) = exp(k × スピード)(Key / Slide 系は k = 0.05、Taiko は 0.0693)。スピードは -20..20 の整数
//   ここでの変更:
//     ・取得は、譜面の保存値を見ずに、枠の値(小数)を返す(整数の丸め値)。設定は、譜面の +340 / +129 を書かず、枠だけに書く(= 譜面ごとの保存を撤去)
//     ・倍率の計算には、直前の取得の小数値をそのまま使う(exp(k × 小数))
//     ・プレイ中の ±1 は、小数部を保ったまま動かす。MOD 画面のバーなど、整数で設定されたときは、その整数にする
//   枠の小数値は NSUserDefaults の bm.speed.<枠> に保存。無ければ設定の整数
// ============================================================================
#define NS_SLOTS 6
static float gNsFine[NS_SLOTS];
static BOOL gNsSet[NS_SLOTS];
static int gNsLastSlot = -1, gNsLastInt;
static float gNsLastFine;

static int ns_slot_of(int f) {   // 元の取得 / 設定と同じ順の判定
    if (f & 0x71) return 0;
    if (f & 0x604) return 1;
    if (f & 0x100) return 2;
    if (f & 0x800) return 3;
    if (f & 0x3000) return 4;
    if (f & 0x4000) return 5;
    return -1;
}
static int *ns_cfg(int slot) {   // 設定の枠(int)
    uint8_t *cfg = ((uint8_t *(*)(void))VA(0x1001DFA40ULL))();
    return (int *)(cfg + 192 + 4 * slot);
}
static float ns_clamp(float v) { return v < -20.0f ? -20.0f : (v > 20.0f ? 20.0f : v); }
static float ns_fine(int slot) {
    if (!gNsSet[slot]) {
        id v = [NSUserDefaults.standardUserDefaults objectForKey:[NSString stringWithFormat:@"bm.speed.%d", slot]];
        gNsFine[slot] = v ? ns_clamp([v floatValue]) : (float)*ns_cfg(slot);
        gNsSet[slot] = YES;
    }
    return gNsFine[slot];
}
static void ns_store(int slot, float v) {   // 小数値を保存し、設定の枠にはその丸め値を書く(標準の画面と矛盾させない)
    v = ns_clamp(roundf(v * 100.0f) / 100.0f);
    gNsFine[slot] = v; gNsSet[slot] = YES;
    [NSUserDefaults.standardUserDefaults setFloat:v forKey:[NSString stringWithFormat:@"bm.speed.%d", slot]];
    *ns_cfg(slot) = (int)lroundf(v);
}
#ifndef BM_PUBLIC
static void ns_log(void *chart, int flags, int slot) {   // 枠とモードの対応を調べるための記録(非公開ビルドのみ)。フラグごとに 1 回。譜面のフォルダも書く
    static NSMutableSet *seen;
    if (!seen) seen = [NSMutableSet set];
    if ([seen containsObject:@(flags)]) return;
    [seen addObject:@(flags)];
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *path = [docs stringByAppendingPathComponent:@"notespeed_log.txt"];
    NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!fh) { [NSFileManager.defaultManager createFileAtPath:path contents:nil attributes:nil]; fh = [NSFileHandle fileHandleForWritingAtPath:path]; }
    [fh seekToEndOfFile];
    typedef struct { uint8_t b[24]; } NsStr24;
    NsStr24 dir = ((NsStr24 (*)(void *))VA(0x10019DD9CULL))(chart);      // 譜面のフォルダ(sub_10019DD9C は文字列を x8 で返す)
    [fh writeData:[[NSString stringWithFormat:@"flags=0x%x slot=%d dir=%s\n", flags, slot, stdstr_c(dir.b)] dataUsingEncoding:NSUTF8StringEncoding]];
    [fh closeFile];
}
#endif

typedef int (*getspd_fn)(void *);
typedef void (*setspd_fn)(void *, int);
typedef float (*expspd_fn)(int, double);
static getspd_fn orig_getspd;
static setspd_fn orig_setspd;
static expspd_fn orig_expspd;

static int hook_getspd(void *chart) {   // sub_10019E128
    int f = *(int *)((uint8_t *)chart + 344);
    int slot = (f == 2 || f == 8 || f == 128) ? -1 : ns_slot_of(f);      // 譜面側で速さが決まるモード(Catch など)は元のまま
#ifndef BM_PUBLIC
    ns_log(chart, f, slot);
#endif
    if (slot < 0) { gNsLastSlot = -1; return orig_getspd(chart); }
    float fine = ns_fine(slot);
    gNsLastSlot = slot; gNsLastFine = fine; gNsLastInt = (int)lroundf(fine);
    return gNsLastInt;
}
static void hook_setspd(void *chart, int v) {   // sub_10019E264
    int f = *(int *)((uint8_t *)chart + 344);
    if (f & 0x8A) return;                                   // 元と同じ: これらのモードでは何もしない
    int slot = ns_slot_of(f);
    if (slot < 0) return;
    if (slot == gNsLastSlot && v == gNsLastInt) return;     // 直前の取得と同じ整数 = 変えていない(小数部を失わせない)
    if (slot == gNsLastSlot && abs(v - gNsLastInt) == 1) ns_store(slot, gNsLastFine + (float)(v - gNsLastInt));   // プレイ中の ±1: 小数部を保つ
    else ns_store(slot, (float)v);                                                                               // MOD 画面のバーなど: 整数
    gNsLastSlot = slot; gNsLastFine = ns_fine(slot); gNsLastInt = (int)lroundf(gNsLastFine);
}
static float hook_expspd(int w0, double k) {   // sub_1001DFECC: exp(k × スピード)。直前の取得の小数値があれば、それで計算する
    float r;
    BOOL fine = gNsLastSlot >= 0 && w0 == gNsLastInt && fabsf(gNsLastFine - (float)w0) > 1e-6f;
    r = fine ? (float)exp(k * (double)gNsLastFine) : orig_expspd(w0, k);
#ifndef BM_PUBLIC
    {   // 確かめ用の記録(非公開ビルドのみ): 最初の 40 回
        static int n;
        if (n++ < 40) {
            NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
            NSString *path = [docs stringByAppendingPathComponent:@"notespeed_log.txt"];
            NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
            if (!fh) { [NSFileManager.defaultManager createFileAtPath:path contents:nil attributes:nil]; fh = [NSFileHandle fileHandleForWritingAtPath:path]; }
            [fh seekToEndOfFile];
            [fh writeData:[[NSString stringWithFormat:@"exp: int=%d k=%.4f slot=%d fine=%.2f -> scale=%.5f%@\n", w0, k, gNsLastSlot, gNsLastFine, r, fine ? @" (fine)" : @""] dataUsingEncoding:NSUTF8StringEncoding]];
            [fh closeFile];
        }
    }
#endif
    return r;
}

// ---- 画面の数字を小数で出す(標準は整数に丸めて出す: sub_1001A02AC = "%d") ----
// プレイ中の小ウィンドウ sub_1000BA71C: this+688 = 譜面 / +696 = バー(**) / +720 = 文字(**)。作った後に、速さの文字とバーの位置を書き換える
// MOD 画面 sub_1000AF494(turbo.inc の modbuild の後): this+688 = モッドの状態(+12 = スピード)/ +720 = 速さの文字 / +728 = バー
static BOOL ns_fine_frac(void) { return gNsLastSlot >= 0 && fabsf(gNsLastFine - roundf(gNsLastFine)) > 1e-6f; }
static void ns_show_fine(void *textNode, void *bar) {
    char b[16]; snprintf(b, sizeof b, "%.2f", gNsLastFine);
    if (textNode) set_label_text(textNode, b);
    if (bar) ((void (*)(void *, float))VA(0x100166D10ULL))(bar, (gNsLastFine + 20.0f) * 2.5f);   // 標準と同じ: (スピード+20)×2.5 %
}
typedef void (*spdwin_fn)(void *);
static spdwin_fn orig_spdwin;
static void hook_spdwin(void *a1) {   // sub_1000BA71C
    orig_spdwin(a1);
    if (!ns_fine_frac()) return;     // 直前の取得(この関数の中で譜面から取っている)の小数値
    void **t = *(void ***)((uint8_t *)a1 + 720), **bar = *(void ***)((uint8_t *)a1 + 696);
    if (t) ns_show_fine(*t, bar ? *bar : NULL);
}
static void ns_modbuild_post(void *scene) {
    uint8_t *ms = *(uint8_t **)((uint8_t *)scene + 688);
    if (!ms || !ns_fine_frac() || *(int *)(ms + 12) != gNsLastInt) return;   // 表示中の整数が、直前に取得した譜面の丸め値と一致するときだけ
    ns_show_fine(*(void **)((uint8_t *)scene + 720), *(void **)((uint8_t *)scene + 728));
}
