// ---- Lua プラグインの描画(AddText / AddSprite): エディタのレーン上に表示するモジュール ----
// Malody V の Editor-Specific Module 相当。X = 編集領域の幅に対する % / Beat = 縦位置(拍) / Width = 幅の % /
// Height = 高さ(px) / HeightBeat = 高さ(拍)。プラグインの実行が終わっても残り、RemoveModule で消える。
#import <ImageIO/ImageIO.h>
@interface BMMod : NSObject {
    @public
    NSString *name, *text, *file, *parent;
    int kind;                   // 0 = text, 1 = sprite
    double x, width, height, beat, heightBeat, opacity, scaleX, scaleY, rotate, fontMul;   // fontMul = テキストの大きさの倍率(0 = 1 倍)
    BOOL visible, dirtyTex, dirtyText, widthSet, heightSet, fallback, rotApplied;   // fallback = 画像が見つからず透明で代替
    void *node;
    float texW, texH;
    uint8_t *rgba;              // sprite のピクセル(RGBA8888)
    uint8_t cr, cg, cb;         // SetColor(255,255,255 = 既定)
}
@end
@implementation BMMod
- (void)dealloc { free(rgba); }
@end
static NSMutableArray<BMMod *> *gMods;
static void *gModEd;

static int mod_is_node(void *node) {
    if (!gMods.count) return -1;
    for (BMMod *m in gMods) if (m->node == node) return 1;
    return 0;
}
static BMMod *mod_find(NSString *n) { for (BMMod *m in gMods) if ([m->name isEqualToString:n]) return m; return nil; }
static void mod_drop_node(BMMod *m) {
    if (m->node) VT(m->node, 608, void (*)(void *, int))(m->node, 1);   // removeFromParentAndCleanup
    m->node = NULL;
}
static void mod_remove(const char *cn) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    if (!m) return;
    mod_drop_node(m);
    [gMods removeObject:m];
}
static int mod_add(int kind, const char *cn, const char *arg) {
    if (gModEd != gPlEd) { for (BMMod *m in gMods) m->node = NULL; gMods = nil; gModEd = gPlEd; }   // 別のエディタ: 旧ノードは破棄済み
    if (!gMods) gMods = [NSMutableArray array];
    NSString *n = [NSString stringWithUTF8String:cn];
    BMMod *old = mod_find(n);
    if (old) { mod_drop_node(old); [gMods removeObject:old]; }
    BMMod *m = [BMMod new];
    m->name = n; m->kind = kind; m->x = 50; m->width = 100; m->height = 0; m->beat = 0; m->heightBeat = 0; m->opacity = 1; m->scaleX = m->scaleY = 1; m->visible = YES; m->cr = m->cg = m->cb = 255;
    if (kind == 0) m->text = [NSString stringWithUTF8String:arg ?: ""];
    else {
        m->file = [NSString stringWithUTF8String:arg ?: ""];
        // 画像は 譜面フォルダ → プラグインフォルダ(export_plugins)の順に探す。無ければ白の 4x4(塗りつぶし用の素材が無くても帯は見える)
        NSString *rel = m->file;
        NSMutableArray<NSString *> *cands = [NSMutableArray array];
        if (rel.length && ![rel hasPrefix:@"/"] && ![rel containsString:@".."]) {
            NSString *dir = pl_chart_dir();
            if (dir) [cands addObject:[dir stringByAppendingPathComponent:rel]];
            if (gPlCurPath) [cands addObject:[gPlCurPath.stringByDeletingLastPathComponent stringByAppendingPathComponent:rel]];   // プラグイン自身のフォルダ
            [cands addObject:[pl_dir() stringByAppendingPathComponent:rel]];
        }
        CGImageRef img = NULL;
        NSString *path = nil;
        for (NSString *c in cands) {
            if (![NSFileManager.defaultManager fileExistsAtPath:c]) continue;
            CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:c], NULL);
            img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
            if (src) CFRelease(src);
            if (img) { path = c; break; }
        }
        if (img) {
            size_t w = CGImageGetWidth(img), h = CGImageGetHeight(img);
            if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                m->rgba = calloc(w * h, 4);
                CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                CGContextRef cx = CGBitmapContextCreate(m->rgba, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
                CGContextDrawImage(cx, CGRectMake(0, 0, w, h), img);
                CGContextRelease(cx); CGColorSpaceRelease(cs);
                m->texW = (float)w; m->texH = (float)h;
            }
            CGImageRelease(img);
        } else {
            os_log(L, "module: cannot load %{public}@ (transparent)", rel);
            m->rgba = calloc(4 * 4, 4);   // 画像が無ければ透明(何も表示しない)
            m->texW = m->texH = 4;
            m->fallback = YES;
        }
        (void)path;
    }
    [gMods addObject:m];
    return 1;
}
static void mod_set_num(const char *cn, const char *ck, double v) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    if (!m) return;
    NSString *k = [[NSString stringWithUTF8String:ck] lowercaseString];
    if ([k isEqualToString:@"x"]) m->x = v;
    else if ([k isEqualToString:@"width"]) { m->width = v; m->widthSet = YES; }
    else if ([k isEqualToString:@"height"]) { m->height = v; m->heightBeat = 0; m->heightSet = YES; }    // 最後に代入した方(Height / HeightBeat)が有効
    else if ([k isEqualToString:@"heightbeat"]) { m->heightBeat = v; m->height = 0; m->heightSet = YES; }
    else if ([k isEqualToString:@"rotate"]) m->rotate = v;
    else if ([k isEqualToString:@"beat"]) m->beat = v;
    else if ([k isEqualToString:@"alpha"]) m->opacity = v < 0 ? 0 : (v > 100 ? 1 : v / 100.0);       // Alpha は 0〜100
    else if ([k isEqualToString:@"opacity"]) m->opacity = v > 1 ? v / 255.0 : v;
    else if ([k isEqualToString:@"scale"]) m->scaleX = m->scaleY = v;
    else if ([k isEqualToString:@"scalex"]) m->scaleX = v;
    else if ([k isEqualToString:@"scaley"]) m->scaleY = v;
    else if ([k isEqualToString:@"visible"] || [k isEqualToString:@"show"]) m->visible = v != 0;
}
// Height の単位: Malody V は「画面の高さ = 1080」のユニット。Slide は Bezier 系プラグインの前提(レーン幅 100% = 1080)に合わせる
static float mod_hunit(void *ed) {
    const Lay *l = lay_of(ed);
    if (!l) return is_taiko(ed) ? LF(ed, 128) / 1080.0f : 1.0f;      // Taiko: 画面の高さ = 1080
    float H0 = LF(ed, 128), xw = l->slide ? LF(ed, 1696) : 0.0f;
    float u = (xw > 0 ? xw : H0) / 1080.0f;
    return u > 0.01f ? u : 1.0f;
}
// モジュールの値の読み出し(FindModule / HeightBeat 設定後の Height など)。0 = ホストでは持たない(Lua 側の保管値を使う)
static int mod_get_num(const char *cn, const char *ck, double *out) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    if (!m) return 0;
    NSString *k = [[NSString stringWithUTF8String:ck] lowercaseString];
    if ([k isEqualToString:@"x"]) *out = m->x;
    else if ([k isEqualToString:@"width"]) *out = m->width;
    else if ([k isEqualToString:@"height"]) {
        void *ed = gPlEd; const Lay *l = ed ? lay_of(ed) : NULL;
        if (m->heightBeat > 0 && l) *out = (double)(LF(ed, l->zoom) * LF(ed, l->base)) * m->heightBeat / mod_hunit(ed);
        else if (m->height > 0) *out = m->height;
        else if (m->kind == 1 && m->texW > 0 && m->texH > 0) *out = (double)m->texH * 0.3 * m->scaleY;   // 画像の元の大きさ(描画と同じ既定)
        else return 0;
    }
    else if ([k isEqualToString:@"alpha"]) *out = m->opacity * 100.0;
    else if ([k isEqualToString:@"rotate"]) *out = m->rotate;
    else if ([k isEqualToString:@"scale"] || [k isEqualToString:@"scalex"]) *out = m->scaleX;
    else if ([k isEqualToString:@"scaley"]) *out = m->scaleY;
    else return 0;
    return 1;
}
static void mod_set_color(const char *cn, int r, int g, int b) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    if (!m) return;
    m->cr = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); m->cg = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g); m->cb = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
}
static void mod_set_parent(const char *cn, const char *pn) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    if (m) m->parent = [NSString stringWithUTF8String:pn] ?: nil;
}
static void mod_set_beat(const char *cn, const char *ck, LHBeat b) {
    if (b.den <= 0) return;
    mod_set_num(cn, ck, b.m + (double)b.num / b.den);
}
static void mod_set_str(const char *cn, const char *ck, const char *v) {
    BMMod *m = mod_find([NSString stringWithUTF8String:cn]);
    NSString *k = [[NSString stringWithUTF8String:ck] lowercaseString];
    if (m && m->kind == 0 && ([k isEqualToString:@"text"] || [k isEqualToString:@"content"])) { m->text = [NSString stringWithUTF8String:v] ?: @""; m->dirtyText = YES; }
}
static int mod_exists(const char *cn) { return mod_find([NSString stringWithUTF8String:cn]) ? 1 : 0; }

static void *mod_make_sprite(BMMod *m) {
    struct { void *addr; size_t len; } mip = { m->rgba, (size_t)m->texW * (size_t)m->texH * 4 };
    void *tex = calloc(1, 0x30);
    ((void (*)(void *))VA(0x1005783E8ULL))(tex);
    long ok = ((long (*)(void *, void *, int, int, long, long))VA(0x100578504ULL))(tex, &mip, 1, 2, (long)m->texW, (long)m->texH);
    if (!ok) return NULL;
    void *sp = calloc(1, 0x3E0);
    ((void *(*)(void *))VA(0x10056ADCCULL))(sp);
    if (!VT(sp, 1344, long (*)(void *, void *))(sp, tex)) return NULL;
    ((void (*)(void *))VA(0x100588938ULL))(sp);
    return sp;
}
// ShowTip: 編集領域の上端に出す常設の 1 行(トーストを積まない)
static NSString *gTipText;
static void *gTipNode, *gTipEd;
static BOOL gTipDirty;
static void tip_set(const char *t) {
    gTipText = [NSString stringWithUTF8String:t ?: ""] ?: @"";
    if (gTipEd != gPlEd) gTipNode = NULL;     // 別のエディタ: 旧ノードは破棄済み
    gTipEd = gPlEd;
    gTipDirty = YES;
}
static void tip_update(void *ed) {
    if (!gTipEd || gTipEd != ed || (!gTipDirty && !gTipNode)) return;
    const Lay *l = lay_of(ed);
    BOOL tk = is_taiko(ed);
    if (!l && !tk) return;
    uint8_t *e = (uint8_t *)ed;
    BOOL show = gTipText.length > 0;
    if (!gTipNode) {
        if (!show) { gTipDirty = NO; return; }
        float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(e + 672), "NormalFont_H");
        gTipNode = ((void *(*)(float))VA(0x1001F3608ULL))(fontH * 0.9f);
        VT(ed, 504, void (*)(void *, void *, int))(ed, gTipNode, 89001);
        float a[2] = { 0.0f, 0.5f };
        VT(gTipNode, 296, void (*)(void *, float *))(gTipNode, a);
        gTipDirty = YES;
    }
    if (gTipDirty) {
        if (show) set_label_text(gTipNode, gTipText.UTF8String);
        gTipDirty = NO;
    }
    void *cont = *(void **)(e + 1472);
    float contX = tk ? 0.0f : VT(cont, 192, float (*)(void *))(cont);
    float H0 = LF(ed, 128);
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(e + 672), "NormalFont_H");
    float px[2] = { contX + 6.0f, H0 - fontH * 1.4f };
    VT(gTipNode, 136, void (*)(void *, float *))(gTipNode, px);
    VT(gTipNode, 336, void (*)(void *, int))(gTipNode, show ? 1 : 0);
}
// 親モジュール: 子の X / Width は親の幅に対する %、Beat は親の拍からの相対、Alpha は親と掛け合わせ、親が非表示なら非表示
typedef struct { double beat, alpha; float cx, w; BOOL vis; } ModEff;
static ModEff mod_eff(BMMod *m, float x0, float x1, int depth) {
    ModEff e;
    BMMod *p = (m->parent.length && depth < 4) ? mod_find(m->parent) : nil;
    if (p && p != m) {
        ModEff pe = mod_eff(p, x0, x1, depth + 1);
        float left = pe.cx - pe.w * 0.5f;
        e.cx = left + (float)(m->x / 100.0) * pe.w;
        e.w = (float)(m->width / 100.0) * pe.w * (float)m->scaleX;
        e.beat = pe.beat + m->beat;
        e.alpha = pe.alpha * m->opacity;
        e.vis = pe.vis && m->visible;
    } else {
        e.cx = x0 + (float)(m->x / 100.0) * (x1 - x0);
        e.w = (float)(m->width / 100.0) * (x1 - x0) * (float)m->scaleX;
        e.beat = m->beat;
        e.alpha = m->opacity;
        e.vis = m->visible;
    }
    return e;
}
static void mod_update_taiko(void *ed);
static void mod_update(void *ed) {
    tip_update(ed);
    if (!gMods.count || gModEd != ed) return;
    const Lay *l = lay_of(ed);
    if (!l) { if (is_taiko(ed)) mod_update_taiko(ed); return; }
    uint8_t *e = (uint8_t *)ed;
    void *cont = *(void **)(e + 1472);
    float contX = VT(cont, 192, float (*)(void *))(cont), contY = VT(cont, 208, float (*)(void *))(cont);
    float K = LF(ed, l->zoom) * LF(ed, l->base);
    float off = l->off >= 0 ? LF(ed, l->off) : 0.0f;
    float x0, x1;
    if (!l->slide) {
        int cnt = e[1700]; if (cnt < 1) cnt = 1;
        x0 = contX + LF(ed, 1632);
        x1 = contX + LF(ed, 1632 + 4 * (cnt - 1)) + LF(ed, 1672);
    } else { x0 = contX; x1 = contX + LF(ed, 1696); }
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(e + 672), "NormalFont_H");
    for (BMMod *m in gMods) {
        if (!m->node) {
            if (m->kind == 0) {
                m->node = ((void *(*)(float))VA(0x1001F3608ULL))(fontH * (m->fontMul > 0 ? (float)m->fontMul : 1.0f));
                set_label_text(m->node, m->text.UTF8String);
            } else if (m->rgba) m->node = mod_make_sprite(m);
            if (!m->node) { m->visible = NO; continue; }
            VT(ed, 504, void (*)(void *, void *, int))(ed, m->node, 89000);
            float a[2] = { 0.5f, 0.5f };               // Beat / X は図形の中心(ES3 は Beat = 帯の中央、HeightBeat = 帯の長さ)
            VT(m->node, 296, void (*)(void *, float *))(m->node, a);
        }
        if (m->kind == 0 && m->dirtyText) { set_label_text(m->node, m->text.UTF8String); m->dirtyText = NO; }
        if (m->node) *(int *)((uint8_t *)m->node + 492) = 105;   // etag(+492)= 編集領域(105)にして、描画物の上のタッチも編集領域の処理(プラグインの OnClick / OnDrag)へ届ける
        ModEff ef = mod_eff(m, x0, x1, 0);
        float W = ef.w;
        // Width / Height を指定しないモジュールは画像の元の大きさで出す(元の画像は透明な余白つきの大きなキャンバスが多いので 0.3 倍)。
                float nat = LF(ed, 128) / 1080.0f * 0.3f;
        BOOL unsized = m->kind == 1 && m->texW > 0 && !m->widthSet;
        if (unsized) W = m->texW * nat * (float)m->scaleX;
        float y = contY + K * (float)(G(ef.beat) - gG0) - off;
        if (gPlShrink.applied && ed == gPl.editor) {   // 一覧を開いてレーンを縮めている間は、モジュールも同じ倍率で縮める
            ef.cx = gPlShrink.cx + gPlShrink.s * (ef.cx - gPlShrink.cx);
            W *= gPlShrink.s;
        }
        float px[2] = { ef.cx, y };
        VT(m->node, 136, void (*)(void *, float *))(m->node, px);
        if (m->kind == 1 && m->texW > 0) {
            float h = m->heightBeat > 0 ? K * (float)m->heightBeat : (m->height > 0 ? (float)m->height * mod_hunit(ed) : W * m->texH / m->texW);
            if (m->heightBeat <= 0 && m->height <= 0 && m->widthSet && W * m->texH / m->texW > LF(ed, 128) * 0.5f) h = m->texH * nat * (float)m->scaleY;   // Width だけ指定した大きな正方形画像が画面を覆わないよう、高さは元の大きさにする
            if (unsized && m->heightBeat <= 0 && m->height <= 0) h = m->texH * nat * (float)m->scaleY;
            ((float (*)(void *, float, float))VA(0x10054471CULL))(m->node, W, h * (float)m->scaleY);
        }
        if (m->rotate != 0 || m->rotApplied) {   // Rotate(度・時計回り): Node::setRotation(float)(非仮想。+40/+44 と回転 quaternion を更新)
            ((void (*)(void *, float))VA(0x100543EBCULL))(m->node, (float)m->rotate);
            m->rotApplied = m->rotate != 0;
        }
        VT(m->node, 1032, void (*)(void *, int))(m->node, (int)(ef.alpha * 255));
        if (m->cr != 255 || m->cg != 255 || m->cb != 255) mk_set_color(m->node, m->cr, m->cg, m->cb);
        BOOL hide = m->fallback;   // 画像なしは透明なので描画自体を省く
        VT(m->node, 336, void (*)(void *, int))(m->node, (ef.vis && !hide) ? 1 : 0);
    }
}

// ---- Taiko: 描画物(AddText / AddSprite)。時間軸が横なので、Beat / HeightBeat = 横、X / Width = 帯の縦(X 0% = 上端、100% = 下端)
//   描画物はノート用コンテナ(+1792)の親に載せる(位置はコンテナの座標系)。FX プレビューの hs は考慮しない(scroll / jump は G で反映)
static void mod_update_taiko(void *ed) {
    uint8_t *e = (uint8_t *)ed;
    void *cont = *(void **)(e + 1792);
    static void *gParCache, *gParEd;
    if (gParEd != ed) { gParEd = ed; gParCache = NULL; }
    if (!gParCache) gParCache = t_parent_of(ed, cont);
    void *par = gParCache;
    if (!cont || !par) return;
    float contX = VT(cont, 192, float (*)(void *))(cont);
    float K = *(float *)(e + 2580) * *(float *)(e + 2584);
    float band = *(float *)(e + 2080), yc = *(float *)(e + 2072);
    typedef struct { float v[2]; uint64_t pad[2]; } V2Ret;
    float wp[2] = { 0.0f, yc };
    V2Ret r = ((V2Ret (*)(void *, const float *))VA(0x100546980ULL))(par, wp);   // 帯の中心 y を親の座標へ
    float ycl = r.v[1];
    float y0 = ycl + band * 0.5f, y1 = ycl - band * 0.5f;
    float fontH = ((float (*)(void *, const char *))VA(0x1000B2CC0ULL))(*(void **)(e + 672), "NormalFont_H");
    float H0 = LF(ed, 128), nat = H0 / 1080.0f * 0.3f, unit = mod_hunit(ed);
    for (BMMod *m in gMods) {
        if (!m->node) {
            if (m->kind == 0) {
                m->node = ((void *(*)(float))VA(0x1001F3608ULL))(fontH * (m->fontMul > 0 ? (float)m->fontMul : 1.0f));
                set_label_text(m->node, m->text.UTF8String);
            } else if (m->rgba) m->node = mod_make_sprite(m);
            if (!m->node) { m->visible = NO; continue; }
            VT(par, 504, void (*)(void *, void *, int))(par, m->node, 89000);
            float a[2] = { 0.5f, 0.5f };
            VT(m->node, 296, void (*)(void *, float *))(m->node, a);
        }
        if (m->kind == 0 && m->dirtyText) { set_label_text(m->node, m->text.UTF8String); m->dirtyText = NO; }
        *(int *)((uint8_t *)m->node + 492) = 105;
        ModEff ef = mod_eff(m, y0, y1, 0);              // cx = 縦位置、w = 縦の長さ(上→下で符号が負になる)
        float px[2] = { contX + K * (float)G(ef.beat), ef.cx };
        VT(m->node, 136, void (*)(void *, float *))(m->node, px);
        if (m->kind == 1 && m->texW > 0) {
            BOOL unsized = !m->widthSet;
            float vext = unsized ? m->texH * nat * (float)m->scaleY : fabsf(ef.w);
            float hext = m->heightBeat > 0 ? K * (float)m->heightBeat : (m->height > 0 ? (float)m->height * unit : (unsized ? m->texW * nat * (float)m->scaleX : vext * m->texW / m->texH));
            ((float (*)(void *, float, float))VA(0x10054471CULL))(m->node, hext, vext);
        }
        if (m->rotate != 0 || m->rotApplied) {
            ((void (*)(void *, float))VA(0x100543EBCULL))(m->node, (float)m->rotate);
            m->rotApplied = m->rotate != 0;
        }
        VT(m->node, 1032, void (*)(void *, int))(m->node, (int)(ef.alpha * 255));
        if (m->cr != 255 || m->cg != 255 || m->cb != 255) mk_set_color(m->node, m->cr, m->cg, m->cb);
        VT(m->node, 336, void (*)(void *, int))(m->node, (ef.vis && !m->fallback) ? 1 : 0);
    }
}
