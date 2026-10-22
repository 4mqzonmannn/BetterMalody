#include <zlib.h>
#include <sys/mman.h>
#import <CommonCrypto/CommonDigest.h>
// ---- osu! Store: sayobot ミラーで osu! の譜面(mania / taiko / catch)を検索して取り込む ----
// 無印の本体は .osu を直接読めるので変換は不要。.osz を展開して Documents/_song_<10000000+sid>/0/ に置くだけで曲一覧に出る。
// 画面は iOS 標準のウィンドウ(pl_alert)。入口は設定画面のボタン(tag CFG_TAG_OSU)。
//   検索: https://api.sayobot.cn/beatmaplist?0=<件数>&1=<開始>&2=<1 人気 / 4 新着・検索>&3=<検索語>   → { data:[{sid,title,artist,creator,modes,...}], endid }
//   取得: https://dl.sayobot.cn/beatmaps/download/novideo/<sid>   (動画なしの .osz)
//   modes のビット: 1 std / 2 taiko / 4 catch / 8 mania(std は無印に無いので出さない)
#include "zipx.h"
#define OSU_SID_BASE 10000000      // 本家の曲 ID(〜数万)と重ならないよう、フォルダ名にだけ足す
#define OSU_PAGE 8

static NSString *gOsuKeyword = @"";
static BOOL gOsuHot;
static id gOsuBusy;                // 取得中の「待機」ウィンドウ

static NSURLSession *osu_session(void) {
    static NSURLSession *s;
    if (!s) {
        NSURLSessionConfiguration *c = [NSURLSessionConfiguration defaultSessionConfiguration];
        c.timeoutIntervalForRequest = 60;
        c.timeoutIntervalForResource = 600;
        c.HTTPAdditionalHeaders = @{ @"User-Agent": @"BetterMalody/1.0 (osu store)" };
        s = [NSURLSession sessionWithConfiguration:c];
    }
    return s;
}
static NSString *osu_mode_tag(int modes) {
    NSMutableString *s = [NSMutableString string];
    if (modes & 8) [s appendString:@"M"];
    if (modes & 2) [s appendString:@"T"];
    if (modes & 4) [s appendString:@"C"];
    return s;
}
static void osu_msg(NSString *title, NSString *msg) {
    pl_alert(title, msg, nil, @[ @"OK" ], @[ @0 ], ^(NSInteger i, NSString *t) {});
}
static void osu_busy_show(NSString *msg, void (^cancel)(void)) {
    pl_alert(@"osu! Store", msg, nil, @[ @"Cancel" ], @[ @1 ], ^(NSInteger i, NSString *t) { if (cancel) cancel(); });
}
// 待機ウィンドウを閉じる(開いていれば)。完了後に next を呼ぶ
static void osu_busy_close(void (^next)(void)) {
    @try {
        id app = [(id)NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id vc = [[app valueForKey:@"keyWindow"] valueForKey:@"rootViewController"];
        while ([vc valueForKey:@"presentedViewController"]) vc = [vc valueForKey:@"presentedViewController"];
        if (vc && [vc isKindOfClass:NSClassFromString(@"UIAlertController")]) {
            void (^done)(void) = ^{ if (next) next(); };
            ((void (*)(id, SEL, BOOL, id))objc_msgSend)(vc, NSSelectorFromString(@"dismissViewControllerAnimated:completion:"), YES, done);
            return;
        }
    } @catch (NSException *ex) {}
    if (next) next();
}

static void osu_menu(void);
static void osu_results(NSArray<NSDictionary *> *items, NSInteger offset, NSInteger page);
static void osu_search(NSInteger offset, NSInteger page);

static void osu_import(NSDictionary *it) {
    NSInteger sid = [it[@"sid"] integerValue];
    NSString *url = [NSString stringWithFormat:@"https://dl.sayobot.cn/beatmaps/download/novideo/%ld", (long)sid];
    __block BOOL cancelled = NO;
    __block NSURLSessionDownloadTask *task = nil;
    osu_busy_show([NSString stringWithFormat:@"Downloading…\n%@", it[@"title"]], ^{ cancelled = YES; [task cancel]; });
    task = [osu_session() downloadTaskWithURL:[NSURL URLWithString:url] completionHandler:^(NSURL *loc, NSURLResponse *resp, NSError *err) {
        NSData *zd = (loc && !err) ? [NSData dataWithContentsOfURL:loc options:NSDataReadingMappedIfSafe error:nil] : nil;
        NSInteger code = [resp isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)resp).statusCode : 0;
        NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSString *dest = [docs stringByAppendingPathComponent:[NSString stringWithFormat:@"_song_%ld/0", (long)(OSU_SID_BASE + sid)]];
        NSString *msg = nil; BOOL ok = NO;
        if (cancelled) return;
        if (!zd || code != 200 || zd.length < 4 || memcmp(zd.bytes, "PK", 2)) msg = [NSString stringWithFormat:@"Download failed (HTTP %ld)%@", (long)code, err ? [@" " stringByAppendingString:err.localizedDescription] : @""];
        else {
            [[NSFileManager defaultManager] createDirectoryAtPath:dest withIntermediateDirectories:YES attributes:nil error:nil];
            ZipxStats st = {0};
            int rc = zipx_extract(zd.bytes, zd.length, dest.UTF8String, "osu,ogg,mp3,wav,png,jpg,jpeg,gif", &st);
            NSArray *fl = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dest error:nil];
            int osu = 0;
            for (NSString *f in fl) if ([f.pathExtension.lowercaseString isEqualToString:@"osu"]) osu++;
            if (rc != 0 || !osu) msg = [NSString stringWithFormat:@"Import failed: %s (%d chart files)", st.err, osu];
            else { ok = YES; msg = [NSString stringWithFormat:@"%@\n%d charts imported to _song_%ld.\nOpen the song list again (or restart the app) to see it.", it[@"title"], osu, (long)(OSU_SID_BASE + sid)]; }
        }
        os_log(L, "osu: import sid=%ld ok=%d %{public}@", (long)sid, ok, msg);
        dispatch_async(dispatch_get_main_queue(), ^{
            osu_busy_close(^{ pl_alert(@"osu! Store", msg, nil, @[ @"Back to results", @"Close" ], @[ @0, @1 ], ^(NSInteger i, NSString *t) { if (i == 0) osu_menu(); }); });
        });
    }];
    [task resume];
}

static void osu_detail(NSDictionary *it) {
    int m = [it[@"modes"] intValue];
    NSString *msg = [NSString stringWithFormat:@"%@\n%@  /  mapped by %@\nModes: %@ (M=mania T=taiko C=catch)\nsid %@\n\nDownload and add it to the song list?",
                     it[@"title"], it[@"artist"], it[@"creator"], osu_mode_tag(m), it[@"sid"]];
    pl_alert(@"osu! Store", msg, nil, @[ @"Download", @"Back" ], @[ @0, @1 ], ^(NSInteger i, NSString *t) {
        if (i == 0) osu_import(it);
        else osu_menu();
    });
}

static void osu_results(NSArray<NSDictionary *> *items, NSInteger offset, NSInteger page) {
    if (!items.count) {
        pl_alert(@"osu! Store", @"No mania / taiko / catch charts found.", nil, @[ @"Back" ], @[ @0 ], ^(NSInteger i, NSString *t) { osu_menu(); });
        return;
    }
    NSMutableArray *titles = [NSMutableArray array], *styles = [NSMutableArray array];
    NSInteger n = MIN((NSInteger)items.count, OSU_PAGE);
    for (NSInteger i = 0; i < n; i++) {
        NSDictionary *it = items[(NSUInteger)i];
        NSString *t = [NSString stringWithFormat:@"[%@] %@ - %@", osu_mode_tag([it[@"modes"] intValue]), it[@"artist"], it[@"title"]];
        [titles addObject:t.length > 60 ? [[t substringToIndex:58] stringByAppendingString:@"…"] : t]; [styles addObject:@0];
    }
    [titles addObject:@"More…"]; [styles addObject:@0];
    [titles addObject:@"Back"]; [styles addObject:@1];
    pl_alert(@"osu! Store", gOsuHot ? @"Popular" : [NSString stringWithFormat:@"Search: %@", gOsuKeyword.length ? gOsuKeyword : @"(newest)"], nil, titles, styles, ^(NSInteger idx, NSString *t) {
        if (idx < n) osu_detail(items[(NSUInteger)idx]);
        else if (idx == n) osu_search(offset + 20, page + 1);
        else osu_menu();
    });
}

// 検索(20 件取って mania / taiko / catch だけに絞る)
static void osu_search(NSInteger offset, NSInteger page) {
    NSString *kw = [gOsuKeyword stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLQueryAllowedCharacterSet] ?: @"";
    NSString *url = [NSString stringWithFormat:@"https://api.sayobot.cn/beatmaplist?0=20&1=%ld&2=%d&3=%@", (long)offset, gOsuHot ? 1 : 4, kw];
    __block BOOL cancelled = NO;
    __block NSURLSessionDataTask *task = nil;
    osu_busy_show(@"Searching…", ^{ cancelled = YES; [task cancel]; });
    task = [osu_session() dataTaskWithURL:[NSURL URLWithString:url] completionHandler:^(NSData *d, NSURLResponse *resp, NSError *err) {
        if (cancelled) return;
        NSMutableArray *out = [NSMutableArray array];
        NSString *fail = nil;
        id js = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
        if (![js isKindOfClass:NSDictionary.class] || ![js[@"data"] isKindOfClass:NSArray.class]) fail = err ? err.localizedDescription : @"Unexpected response";
        else for (NSDictionary *x in js[@"data"]) if ([x[@"modes"] intValue] & 14) [out addObject:x];
        dispatch_async(dispatch_get_main_queue(), ^{
            osu_busy_close(^{
                if (fail) pl_alert(@"osu! Store", [@"Search failed: " stringByAppendingString:fail], nil, @[ @"Back" ], @[ @0 ], ^(NSInteger i, NSString *t) { osu_menu(); });
                else osu_results(out, offset, page);
            });
        });
    }];
    [task resume];
}

static void osu_menu(void) {
    pl_alert(@"osu! Store", @"osu! の mania / taiko / catch の譜面を、sayobot から検索して取り込みます(ゴールド不要)。\nキーワードを入れて Search、空のままなら新着です。", gOsuKeyword,
             @[ @"Search", @"Popular", @"Close" ], @[ @0, @0, @1 ], ^(NSInteger idx, NSString *text) {
        if (idx == 2) return;
        gOsuKeyword = [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] ?: @"";
        gOsuHot = (idx == 1);
        osu_search(0, 0);
    });
}

// ============================================================================
// ストアの乗っ取り(試作): 無印の旧ストア API を話す小さな HTTP サーバー(127.0.0.1)をアプリ内で動かし、
// サーバー一覧の 1 件の接続先をそこへ向ける。要求は Documents/osu_server.log に全部記録する(応答の形の調整用)。
// ============================================================================
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define OSU_SRV_PORT 18088      // 接続先 URL は std::string の長い形式(自前バッファ)で渡すので桁数は問わない
static void osu_srv_log(NSString *line) {
    static NSString *path;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ path = [[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"osu_server.log"] copy]; });
    os_log(L, "osu-srv: %{public}@", line);
    @synchronized (gOsuKeyword) {
        NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
        if (!fh) { [[NSFileManager defaultManager] createFileAtPath:path contents:nil attributes:nil]; fh = [NSFileHandle fileHandleForWritingAtPath:path]; }
        [fh seekToEndOfFile];
        [fh writeData:[[line stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding]];
        [fh closeFile];
    }
}
// 同期の GET(中継サーバーの処理スレッドから呼ぶ)
static NSData *osu_srv_fetch(NSString *url, NSTimeInterval timeout) {
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url] cachePolicy:NSURLRequestUseProtocolCachePolicy timeoutInterval:timeout];
    __block NSData *out = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [[osu_session() dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        if (d && [r isKindOfClass:NSHTTPURLResponse.class] && ((NSHTTPURLResponse *)r).statusCode == 200) out = d;
        dispatch_semaphore_signal(sem);
    }] resume];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)((timeout + 2) * NSEC_PER_SEC)));
    return out;
}
// 譜面 ID(bid)→ 曲 ID(sid): 一覧(list?type=2)を返したときに控える。ダウンロードの要求は cid しか持たない
static NSMutableDictionary<NSString *, NSDictionary *> *osu_cid_map(void) {
    static NSMutableDictionary *m; static dispatch_once_t once;
    dispatch_once(&once, ^{ m = [NSMutableDictionary dictionary]; });
    return m;
}
// Range 付きの GET(from...to の両端を含む)。応答 URL と全体のサイズ(Content-Range)も返す
static NSData *osu_range(NSURL *url, long long from, long long to, NSURL **finalURL, long long *total) {
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:60];
    [rq setValue:[NSString stringWithFormat:@"bytes=%lld-%lld", from, to] forHTTPHeaderField:@"Range"];
    __block NSData *out = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [[osu_session() dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        NSHTTPURLResponse *h = [r isKindOfClass:NSHTTPURLResponse.class] ? (NSHTTPURLResponse *)r : nil;
        if (d && h && (h.statusCode == 206 || (h.statusCode == 200 && from == 0))) {
            out = d;
            if (finalURL) *finalURL = h.URL;
            NSString *cr = h.allHeaderFields[@"Content-Range"];
            if (total) *total = [cr rangeOfString:@"/"].location != NSNotFound ? [[cr substringFromIndex:[cr rangeOfString:@"/"].location + 1] longLongValue] : (long long)d.length;
        }
        dispatch_semaphore_signal(sem);
    }] resume];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 70 * NSEC_PER_SEC));
    return out;
}
// from から len バイトを 1MB ずつ 4 本並列で取って dst へ(1 本ずつだと遅い回線で大きい音源が時間切れになる)。各チャンクは 3 回まで試す
static BOOL osu_fetch_into(NSURL *url, long long from, long long len, unsigned char *dst) {
    __block BOOL ok = YES;
    dispatch_group_t g = dispatch_group_create();
    dispatch_semaphore_t slots = dispatch_semaphore_create(4);
    for (long long pos = 0; pos < len && ok; pos += 1048576) {
        long long m = MIN(1048576LL, len - pos);
        dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
        dispatch_group_enter(g);
        [NSThread detachNewThreadWithBlock:^{
            BOOL got = NO;
            for (int t = 0; t < 3 && !got; t++) {
                NSData *d = osu_range(url, from + pos, from + pos + m - 1, NULL, NULL);
                if (d && (long long)d.length == m) { memcpy(dst + pos, d.bytes, (size_t)m); got = YES; }
            }
            if (!got) ok = NO;
            dispatch_semaphore_signal(slots);
            dispatch_group_leave(g);
        }];
    }
    dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
    return ok;
}
// 曲(sid)の .osz の目次(末尾の中央ディレクトリだけ Range で取る)。項目 = 基本名 → { 位置, 圧縮後/展開後の大きさ, 方式, 名前長 }。曲ごとに 1 回だけ作る
typedef struct { long long off, cs, us; int method; unsigned nl; } OsuZe;
static NSDictionary *osu_zip_index(int sid) {
    static NSMutableDictionary *cache; static NSObject *lock; static dispatch_once_t once;
    dispatch_once(&once, ^{ cache = [NSMutableDictionary dictionary]; lock = [NSObject new]; });
    @synchronized (lock) {
        NSDictionary *c = cache[@(sid)];
        if (c) return c;
        NSURL *u0 = [NSURL URLWithString:[NSString stringWithFormat:@"https://dl.sayobot.cn/beatmaps/download/novideo/%d", sid]], *fu = nil;
        long long n = 0;
        if (!osu_range(u0, 0, 0, &fu, &n) || n < 22 || !fu) { osu_srv_log(@"zip: size probe failed"); return nil; }
        long long tl = MIN(n, 70000LL);
        NSData *tail = osu_range(fu, n - tl, n - 1, NULL, NULL);
        const unsigned char *t = tail.bytes;
        if (!tail || (long long)tail.length != tl) { osu_srv_log(@"zip: tail failed"); return nil; }
        long long e = tl - 22; while (e >= 0 && *(uint32_t *)(t + e) != 0x06054b50) e--;
        if (e < 0) { osu_srv_log(@"zip: no end record"); return nil; }
        unsigned total = *(uint16_t *)(t + e + 10); long long cdlen = *(uint32_t *)(t + e + 12), cdoff = *(uint32_t *)(t + e + 16);
        if (cdoff + cdlen > n || cdlen <= 0 || cdlen > 8LL * 1024 * 1024) { osu_srv_log(@"zip: bad directory"); return nil; }
        NSMutableData *cd = [NSMutableData dataWithLength:(NSUInteger)cdlen];
        if (!osu_fetch_into(fu, cdoff, cdlen, cd.mutableBytes)) { osu_srv_log(@"zip: cd failed"); return nil; }
        const unsigned char *z = cd.bytes;
        NSMutableDictionary *ents = [NSMutableDictionary dictionary];
        long long p = 0;
        for (unsigned i = 0; i < total && p + 46 <= cdlen; i++) {
            if (*(uint32_t *)(z + p) != 0x02014b50) break;
            unsigned nl = *(uint16_t *)(z + p + 28), xl = *(uint16_t *)(z + p + 30), cl = *(uint16_t *)(z + p + 32);
            OsuZe ze = { *(uint32_t *)(z + p + 42), *(uint32_t *)(z + p + 20), *(uint32_t *)(z + p + 24), *(uint16_t *)(z + p + 10), nl };
            NSString *nm = p + 46 + nl <= cdlen ? [[NSString alloc] initWithBytes:z + p + 46 length:nl encoding:NSUTF8StringEncoding] : nil;
            p += 46 + nl + xl + cl;
            if (nm && !([nm hasSuffix:@"/"]) && (ze.method == 0 || ze.method == 8)) ents[nm.lastPathComponent] = [NSData dataWithBytes:&ze length:sizeof ze];
        }
        c = @{ @"url": fu, @"n": @(n), @"ents": ents };
        cache[@(sid)] = c;
        osu_srv_log([NSString stringWithFormat:@"zip: sid %d total %lld bytes, %lu entries", sid, n, (unsigned long)ents.count]);
        return c;
    }
}
static BOOL osu_send_all(int fd, const void *b, size_t len) {
    const char *p = b;
    while (len) { ssize_t k = send(fd, p, len, 0); if (k <= 0) return NO; p += k; len -= (size_t)k; }
    return YES;
}
// .osz の 1 項目を、取れた分から順にそのまま(圧縮なら伸ばして)送る。応答ヘッダは先に出す(Content-Length は目次の展開後サイズ)ので、遅い回線でも
// クライアントには少しずつ届き続け、待たされて時間切れになりにくい。取得は 512KB ずつ 4 本先読み
static BOOL osu_stream_entry(int fd, int sid, NSString *name, NSString *suffix) {
    NSDictionary *idx = osu_zip_index(sid);
    NSDictionary *ents = idx[@"ents"];
    NSData *zd = name ? ents[name] : nil;
    if (!zd && suffix) for (NSString *k in ents) if ([k hasSuffix:suffix]) { zd = ents[k]; break; }
    if (!zd) { osu_srv_log([NSString stringWithFormat:@"stream: no entry %@ / %@ in sid %d", name, suffix, sid]); return NO; }
    OsuZe ze; memcpy(&ze, zd.bytes, sizeof ze);
    NSURL *fu = idx[@"url"]; long long n = [idx[@"n"] longLongValue];
    NSData *lh = osu_range(fu, ze.off, MIN(ze.off + 30 + ze.nl + 1024, n) - 1, NULL, NULL);
    if (!lh || lh.length < 30 || *(uint32_t *)lh.bytes != 0x04034b50) { osu_srv_log(@"stream: bad local header"); return NO; }
    long long data = ze.off + 30 + *(uint16_t *)((const char *)lh.bytes + 26) + *(uint16_t *)((const char *)lh.bytes + 28);
    NSString *head = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n", ze.us];
    if (!osu_send_all(fd, head.UTF8String, head.length)) return NO;
    const long long CH = 524288;
    long long nch = (ze.cs + CH - 1) / CH;
    NSMutableArray<NSData *> *got = [NSMutableArray array]; NSMutableArray *sems = [NSMutableArray array];
    for (long long i = 0; i < nch; i++) { [got addObject:(NSData *)NSNull.null]; [sems addObject:dispatch_semaphore_create(0)]; }
    dispatch_semaphore_t window = dispatch_semaphore_create(4);
    __block BOOL abort_ = NO;
    [NSThread detachNewThreadWithBlock:^{   // 先読み(順番に 4 本まで)
        for (long long i = 0; i < nch && !abort_; i++) {
            dispatch_semaphore_wait(window, DISPATCH_TIME_FOREVER);
            if (abort_) break;
            [NSThread detachNewThreadWithBlock:^{
                long long a = data + i * CH, m = MIN(CH, ze.cs - i * CH);
                NSData *d = nil;
                for (int t = 0; t < 3 && !d && !abort_; t++) { d = osu_range(fu, a, a + m - 1, NULL, NULL); if (d && (long long)d.length != m) d = nil; }
                if (d) @synchronized (got) { got[(NSUInteger)i] = d; }
                dispatch_semaphore_signal(sems[(NSUInteger)i]);
            }];
        }
    }];
    z_stream zs; memset(&zs, 0, sizeof zs);
    BOOL infl = ze.method == 8;
    if (infl && inflateInit2(&zs, -15) != Z_OK) { abort_ = YES; return NO; }
    static const size_t OB = 262144; unsigned char *ob = malloc(OB);
    BOOL ok = YES; long long sent = 0;
    for (long long i = 0; i < nch && ok; i++) {
        dispatch_semaphore_wait(sems[(NSUInteger)i], DISPATCH_TIME_FOREVER);
        NSData *d; @synchronized (got) { d = got[(NSUInteger)i]; }
        if ((id)d == NSNull.null) { ok = NO; break; }
        if (!infl) { ok = osu_send_all(fd, d.bytes, d.length); sent += d.length; }
        else {
            zs.next_in = (Bytef *)d.bytes; zs.avail_in = (uInt)d.length;
            while (ok && zs.avail_in) {
                zs.next_out = ob; zs.avail_out = (uInt)OB;
                int r = inflate(&zs, Z_NO_FLUSH);
                if (r != Z_OK && r != Z_STREAM_END) { ok = NO; break; }
                size_t k = OB - zs.avail_out;
                if (k) { ok = osu_send_all(fd, ob, k); sent += (long long)k; }
                if (r == Z_STREAM_END) break;
            }
        }
        @synchronized (got) { got[(NSUInteger)i] = (NSData *)NSNull.null; }   // 送り終えた分は手放す
        dispatch_semaphore_signal(window);
    }
    abort_ = YES;
    for (long long i = 0; i < nch; i++) dispatch_semaphore_signal(window);
    if (infl) inflateEnd(&zs);
    free(ob);
    osu_srv_log([NSString stringWithFormat:@"stream: sid %d %@ sent %lld/%lld ok=%d", sid, name ?: suffix, sent, ze.us, ok]);
    return ok && sent == ze.us;
}
static NSString *osu_md5(NSData *d) {
    unsigned char h[CC_MD5_DIGEST_LENGTH]; CC_MD5(d.bytes, (CC_LONG)d.length, h);
    NSMutableString *r = [NSMutableString string];
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) [r appendFormat:@"%02x", h[i]];
    return r;
}
// osu! のモード(modes のビット)→ 無印のモード番号: mania = 0(Key)/ catch = 3 / taiko = 5
static int osu_to_malody_mode(int modes, int want) {   // want = 無印のモード指定(8 = 指定なし)。該当がなければ -1
    static const struct { int bit, mode; } m[] = { { 8, 0 }, { 4, 3 }, { 2, 5 } };
    for (int i = 0; i < 3; i++) if ((modes & m[i].bit) && (want == 8 || want == m[i].mode)) return m[i].mode;
    return -1;
}
// 要求(パスとクエリ)→ 応答。ここを、記録した要求を見ながら育てる(JSON 以外は ctype に種類を入れる)
static NSData *osu_srv_reply(NSString *path, NSDictionary<NSString *, NSString *> *q, NSString **ctype) {
    id obj = nil;   // nil = この中継では扱わない要求(公式サーバーへ素通しする)
    if ([path hasSuffix:@"/config"]) {   // 公式の config をそのまま取り、画像/譜面の置き場だけこの中継へ向ける
        NSData *d = osu_srv_fetch([@"http://m.mugzone.net" stringByAppendingString:path.length ? [NSString stringWithFormat:@"/cgi/config?device=%@&t=%@", [q[@"device"] stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLQueryAllowedCharacterSet] ?: @"", q[@"t"] ?: @"0"] : @""], 5);
        id js = d ? [NSJSONSerialization JSONObjectWithData:d options:NSJSONReadingMutableContainers error:nil] : nil;
        NSMutableDictionary *cfg = ([js isKindOfClass:NSDictionary.class] && [js[@"data"] isKindOfClass:NSMutableDictionary.class]) ? js[@"data"] : [NSMutableDictionary dictionary];
        NSString *me = [NSString stringWithFormat:@"http://127.0.0.1:%d/", OSU_SRV_PORT];
        cfg[@"cdn-img"] = me; cfg[@"cdn-chart"] = me;
        [cfg removeObjectForKey:@"news"];
        obj = @{ @"code": @0, @"data": cfg };
    } else if ([path hasSuffix:@"/list"] && [q[@"type"] isEqualToString:@"1"]) {   // 検索: word=<語>[&mode=<無印のモード>]
        int want = q[@"mode"] ? q[@"mode"].intValue : 8;
        NSString *kw = [q[@"word"] stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLQueryAllowedCharacterSet] ?: @"";
        NSData *d = osu_srv_fetch([NSString stringWithFormat:@"https://api.sayobot.cn/beatmaplist?0=40&1=0&2=%d&3=%@", kw.length ? 4 : 1, kw], 15);
        id js = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
        NSMutableArray *out = [NSMutableArray array];
        if ([js isKindOfClass:NSDictionary.class] && [js[@"data"] isKindOfClass:NSArray.class]) for (NSDictionary *x in js[@"data"]) {
            int mm = osu_to_malody_mode([x[@"modes"] intValue], want);
            if (mm < 0) continue;
            NSString *sid = [x[@"sid"] stringValue], *ti = [x[@"titleU"] length] ? x[@"titleU"] : x[@"title"], *ar = [x[@"artistU"] length] ? x[@"artistU"] : x[@"artist"];
            [out addObject:@{ @"title": ti ?: @"", @"artist": ar ?: @"", @"cover": [@"osu/cover/" stringByAppendingString:sid], @"audio": [@"osu/preview/" stringByAppendingString:sid],
                              @"tag": x[@"creator"] ?: @"", @"id": @([sid intValue]), @"length": @0, @"bpm": @0, @"uptime": x[@"lastupdate"] ?: @0, @"mode": @(mm),
                              @"st": @(([x[@"approved"] intValue] >= 1) ? 1 : 0) }];
        }
        obj = @{ @"code": @0, @"data": out };
    } else if ([path hasSuffix:@"/list"] && [q[@"type"] isEqualToString:@"2"]) {   // 曲(sid)の中の譜面一覧。Malody のストアは安定版(type 2)以外を ID 検索でしか出さないので、すべて 2 とする
        NSData *d = osu_srv_fetch([NSString stringWithFormat:@"https://api.sayobot.cn/v2/beatmapinfo?0=%d", q[@"sid"].intValue], 15);
        id js = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
        NSMutableArray *out = [NSMutableArray array];
        NSDictionary *inf = [js isKindOfClass:NSDictionary.class] ? js[@"data"] : nil;
        if ([inf isKindOfClass:NSDictionary.class]) for (NSDictionary *b in inf[@"bid_data"]) {
            static const int mm[] = { -1, 5, 3, 0 };   // osu! の mode 0 std / 1 taiko / 2 catch / 3 mania → 無印
            int om = [b[@"mode"] intValue];
            if (om < 1 || om > 3) continue;
            @synchronized (osu_cid_map()) { osu_cid_map()[[b[@"bid"] stringValue]] = @{ @"sid": @(q[@"sid"].intValue), @"audio": b[@"audio"] ?: @"", @"bg": b[@"bg"] ?: @"", @"version": b[@"version"] ?: @"" }; }
            [out addObject:@{ @"version": b[@"version"] ?: @"", @"creator": inf[@"creator"] ?: @"", @"cid": b[@"bid"] ?: @0, @"uid": inf[@"creator_id"] ?: @0, @"length": b[@"length"] ?: @0,
                              @"mode": @(mm[om]), @"time": inf[@"last_update"] ?: @0, @"size": @0, @"type": @2, @"pc": b[@"playcount"] ?: @0 }];
        }
        obj = @{ @"code": @0, @"data": out };
    } else if ([path hasSuffix:@"/chart/download"]) {   // cid=<bid>: すぐに返す(.osz は大きいので、実際の取得は最初のファイル要求まで待つ)。.osu / 音源 / 背景の名前は beatmapinfo の値
        NSString *cid = q[@"cid"];
        NSDictionary *inf; @synchronized (osu_cid_map()) { inf = osu_cid_map()[cid]; }
        NSMutableArray *files = [NSMutableArray array];
        int sid = [inf[@"sid"] intValue];
        NSString *base = [NSString stringWithFormat:@"osu/f/%d/%@/", sid, cid];
        if (inf) {
            NSString *on = [NSString stringWithFormat:@"%@.osu", cid];
            [files addObject:@{ @"name": on, @"hash": @"", @"file": [base stringByAppendingString:@"osu"] }];
            if ([inf[@"audio"] length]) [files addObject:@{ @"name": inf[@"audio"], @"hash": @"", @"file": [base stringByAppendingString:@"audio"] }];
            if ([inf[@"bg"] length]) [files addObject:@{ @"name": inf[@"bg"], @"hash": @"", @"file": [base stringByAppendingString:@"bg"] }];
        }
        int dsid = OSU_SID_BASE + sid;
        obj = files.count ? @{ @"code": @0, @"data": @{ @"sid": @(dsid), @"cid": @(cid.intValue), @"uid": @1, @"dsid": @(dsid), @"list": files } } : @{ @"code": @-1 };
    } else if ([path hasPrefix:@"/osu/cover/"] || [path hasPrefix:@"/osu/preview/"]) {   // 画像 / 試聴
        BOOL cv = [path hasPrefix:@"/osu/cover/"];
        NSString *sid = path.lastPathComponent;
        NSData *d = osu_srv_fetch(cv ? [NSString stringWithFormat:@"https://assets.ppy.sh/beatmaps/%@/covers/list@2x.jpg", sid] : [NSString stringWithFormat:@"https://b.ppy.sh/preview/%@.mp3", sid], 15);
        if (ctype) *ctype = cv ? @"image/jpeg" : @"audio/mpeg";
        return d ?: [NSData data];
    }
    return obj ? [NSJSONSerialization dataWithJSONObject:obj options:0 error:nil] : nil;
}
// 公式サーバーへ素通し(ログイン / プロフィール / 同期など、osu! ストアに関係しない要求。key・uid は本物のまま付いてくる)。失敗なら nil
static NSData *osu_srv_proxy(NSString *method, NSString *target, NSData *reqBody, NSString *reqCT, NSString **ctype) {
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[@"http://m.mugzone.net" stringByAppendingString:target]] cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:20];
    rq.HTTPMethod = method;
    if (reqBody.length) { rq.HTTPBody = reqBody; if (reqCT) [rq setValue:reqCT forHTTPHeaderField:@"Content-Type"]; }
    __block NSData *out = nil; __block NSString *oct = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [[osu_session() dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        out = d; oct = [r isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)r).allHeaderFields[@"Content-Type"] : nil;
        dispatch_semaphore_signal(sem);
    }] resume];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 25 * NSEC_PER_SEC));
    if (out && oct && ctype) *ctype = oct;
    return out;
}
static void osu_srv_handle(int fd) {
    NSMutableData *raw = [NSMutableData data];
    char buf[8192];
    NSRange hend = { NSNotFound, 0 };
    NSInteger clen = 0;
    for (int guard = 0; guard < 64; guard++) {   // ヘッダ + 本文(Content-Length 分)を読み切る
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        [raw appendBytes:buf length:(NSUInteger)n];
        if (hend.location == NSNotFound) {
            hend = [raw rangeOfData:[NSData dataWithBytes:"\r\n\r\n" length:4] options:0 range:NSMakeRange(0, raw.length)];
            if (hend.location != NSNotFound) {
                NSString *hs = [[NSString alloc] initWithData:[raw subdataWithRange:NSMakeRange(0, hend.location)] encoding:NSUTF8StringEncoding] ?: @"";
                for (NSString *ln in [hs componentsSeparatedByString:@"\r\n"]) if ([ln.lowercaseString hasPrefix:@"content-length:"]) clen = [[ln substringFromIndex:15] integerValue];
            }
        }
        if (hend.location != NSNotFound && (NSInteger)raw.length >= (NSInteger)(hend.location + 4) + clen) break;
    }
    if (hend.location == NSNotFound) { close(fd); return; }
    NSString *hs = [[NSString alloc] initWithData:[raw subdataWithRange:NSMakeRange(0, hend.location)] encoding:NSUTF8StringEncoding] ?: @"";
    NSData *reqBody = [raw subdataWithRange:NSMakeRange(hend.location + 4, raw.length - hend.location - 4)];
    NSString *reqCT = nil;
    for (NSString *ln in [hs componentsSeparatedByString:@"\r\n"]) if ([ln.lowercaseString hasPrefix:@"content-type:"]) reqCT = [[ln substringFromIndex:13] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    NSArray *parts = [[hs componentsSeparatedByString:@"\r\n"].firstObject componentsSeparatedByString:@" "];
    NSString *method = parts.firstObject ?: @"GET", *target = parts.count > 1 ? parts[1] : @"/";
    NSURLComponents *uc = [NSURLComponents componentsWithString:[@"http://x" stringByAppendingString:target]];
    NSMutableDictionary *q = [NSMutableDictionary dictionary];
    for (NSURLQueryItem *it in uc.queryItems) q[it.name] = it.value ?: @"";
    osu_srv_log([NSString stringWithFormat:@"%@ %@", method, target]);
    NSRange fr = [uc.path rangeOfString:@"/osu/f/"];
    if (fr.location != NSNotFound) {   // (<dsid>/<番号>/)osu/f/<sid>/<cid>/<osu|audio|bg>: .osz の該当項目を、取れた分から順に送る
        NSArray *pc = [[uc.path substringFromIndex:fr.location] componentsSeparatedByString:@"/"];   // "", osu, f, <sid>, <cid>, <kind>
        BOOL okf = NO;
        if (pc.count >= 6) {
            NSDictionary *inf; @synchronized (osu_cid_map()) { inf = osu_cid_map()[pc[4]]; }
            NSString *kind = pc[5];
            int sid = [pc[3] intValue];
            if ([kind isEqualToString:@"osu"]) okf = osu_stream_entry(fd, sid, nil, [NSString stringWithFormat:@"[%@].osu", inf[@"version"] ?: @""]);
            else okf = osu_stream_entry(fd, sid, [kind isEqualToString:@"audio"] ? inf[@"audio"] : inf[@"bg"], nil);
        }
        if (!okf) { NSString *nf = @"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; osu_srv_log(@"stream: failed"); send(fd, nf.UTF8String, nf.length, 0); }
        close(fd);
        return;
    }
    NSString *ct = @"application/json";
    NSData *body = osu_srv_reply(uc.path ?: @"/", q, &ct);
    if (!body) {
        body = osu_srv_proxy(method, target, reqBody, reqCT, &ct);
        if (!body) {   // 公式に届かないとき: ログイン系だけは仮の成功を返す(オフラインでも osu! ストアは使えるように)
            BOOL login = [uc.path hasSuffix:@"/login"] || [uc.path hasSuffix:@"/check"];
            body = [NSJSONSerialization dataWithJSONObject:login ? @{ @"code": @0, @"data": @"osu-local-key", @"uid": @1 } : @{ @"code": @0, @"data": @[] } options:0 error:nil];
            ct = @"application/json";
        }
    }
    NSString *head = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Type: %@\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", ct, (unsigned long)body.length];
    NSData *h = [head dataUsingEncoding:NSUTF8StringEncoding];
    send(fd, h.bytes, h.length, 0);
    send(fd, body.bytes, body.length, 0);
    close(fd);
}
static void osu_srv_start(void) {
    static BOOL started;
    if (started) return;
    started = YES;
    [NSThread detachNewThreadWithBlock:^{
        int s = socket(AF_INET, SOCK_STREAM, 0);
        int on = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        struct sockaddr_in a = { .sin_len = sizeof a, .sin_family = AF_INET, .sin_port = htons(OSU_SRV_PORT) };
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) { os_log(L, "osu-srv: bind/listen failed errno=%d", errno); close(s); return; }
        osu_srv_log(@"server started");
        for (;;) {
            int c = accept(s, NULL, NULL);
            if (c < 0) continue;
            [NSThread detachNewThreadWithBlock:^{ osu_srv_handle(c); }];   // GCD の共有スレッドを塞がない(待ち続ける処理が多く、足りなくなると通信の完了も届かず止まる)
        }
    }];
}
// ストアの接続先の選択(UserDefaults bm.store_osu)。ストア画面の見出しのボタン(tag 9)で切り替える。接続先の差し替えは要求ごと(hook_store)なので再起動は要らない
static BOOL osu_store_on(void) { return [[NSUserDefaults standardUserDefaults] boolForKey:@"bm.store_osu"]; }
static void osu_srv_boot(void) {
    osu_srv_start();
    NSString *flag = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"osu_store_on"];   // テスト用: このファイルがあれば osu! ストアを選んだ状態で起動(取り込んで消す)
    if ([[NSFileManager defaultManager] fileExistsAtPath:flag]) { [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"bm.store_osu"]; [[NSFileManager defaultManager] removeItemAtPath:flag error:nil]; }
}

// ストア API の要求組み立て(sub_1001D7A24(client, type, std::string *arg, userdata))フック: osu! ストア選択時は、この呼び出しの間だけ
// 接続先(client+112)/ key(+136)/ uid(+160)を中継サーバー向けに差し替え、戻ったら元に戻す(URL は呼び出し中に組み立て終わる)。
static BOOL osu_store_on(void);
typedef int64_t (*store_fn)(void *, int64_t, void *, int64_t);
static store_fn orig_store;
static void str_swap(uint8_t *s, uint8_t save[24], const char *text, char *buf, size_t cap) {   // libc++(Apple arm64): 短い形式 = 先頭にデータ、末尾 1 バイトが長さ / 長い形式 = ptr, size, cap|1<<63
    memcpy(save, s, 24);
    size_t n = strlen(text);
    memset(s, 0, 24);
    if (n < 23) { memcpy(s, text, n); s[23] = (uint8_t)n; return; }
    snprintf(buf, cap, "%s", text);
    *(char **)s = buf; *(uint64_t *)(s + 8) = n; *(uint64_t *)(s + 16) = ((uint64_t)(cap) | 0) | (1ULL << 63);
}
static int64_t hook_store(void *cl, int64_t type, void *arg, int64_t ud) {
    if (!osu_store_on() || !cl) return orig_store(cl, type, arg, ud);
    uint8_t *c = (uint8_t *)cl, sb[24], sk[24], su[24];
    static char ubuf[64];
    const char *a = "";
    if (arg) { uint8_t *as = (uint8_t *)arg; a = (as[23] & 0x80) ? *(const char **)as : (const char *)as; }
    osu_srv_log([NSString stringWithFormat:@"client: type=%lld state=%d arg=%s", (long long)type, *(int *)(c + 104), a]);
    str_swap(c + 112, sb, [NSString stringWithFormat:@"http://127.0.0.1:%d/", OSU_SRV_PORT].UTF8String, ubuf, sizeof ubuf);
    // key が空(Malody に未ログイン)のときだけ仮の key / uid を入れる(入れないと認証つきの要求が送られずに -1000 になる)。ログイン済みなら本物のまま公式へ素通しする
    BOOL noKey = (c[136 + 23] & 0x80) ? (*(uint64_t *)(c + 136 + 8) == 0) : (c[136 + 23] == 0);
    static char kbuf[8]; static char ibuf[8];
    if (noKey) { str_swap(c + 136, sk, "osu", kbuf, sizeof kbuf); str_swap(c + 160, su, "1", ibuf, sizeof ibuf); }
    int64_t r = orig_store(cl, type, arg, ud);
    memcpy(c + 112, sb, 24); if (noKey) { memcpy(c + 136, sk, 24); memcpy(c + 160, su, 24); }
    return r;
}
// ストア画面のタッチ(sub_1000ACEA4(store, ev, node)): 見出しの切替ボタン(tag 9)で Malody / osu! ストアを切り替え、「ALL」タブの押下と同じ手順で一覧を取り直す
typedef int64_t (*storetouch_fn)(void *, int, void *);
static storetouch_fn orig_storetouch;
static void *kb_fake_node(int tag);
static int64_t hook_storetouch(void *st, int ev, void *node) {
    int tag = node ? VT(node, 672, int (*)(void *))(node) : 0;
    if (tag != 9) return orig_storetouch(st, ev, node);
    if (ev != 0) return 0;
    BOOL on = !osu_store_on();
    [[NSUserDefaults standardUserDefaults] setBool:on forKey:@"bm.store_osu"];
    os_log(L, "store: switched to %{public}s", on ? "osu!" : "Malody");
    uint8_t g[64];                                                             // 一覧の問い合わせ(sub_10010E7D8)は前回と同じ条件だと取り直さないので、控えを消す
    ((void (*)(void *))VA(0x10011009CULL))(g);
    uint8_t *q = ((uint8_t *(*)(void *))VA(0x10011014CULL))(g);
    *(uint64_t *)(q + 40) = 0;
    ((void (*)(void *))VA(0x1001100DCULL))(g);
    orig_storetouch(st, 0, kb_fake_node(50));                                  // 「ALL」タブの押下
    dispatch_async(dispatch_get_main_queue(), ^{ osu_msg(@"Store", on ? @"osu! store (sayobot, free)" : @"Malody store (official)"); });
    return 0;
}

// 設定画面のボタン: ストアの接続先の切り替え(次回起動から有効)/ osu! 譜面の検索・取り込み
static void osu_settings_menu(void) {
    BOOL on = osu_store_on();
    pl_alert(@"Store", [NSString stringWithFormat:@"Current store: %@\nYou can also switch with the button in the store screen header.", on ? @"osu! (sayobot, free)" : @"Malody (official)"], nil,
             @[ @"Use Malody store", @"Use osu! store (beta)", @"Search / import osu! charts", @"Close" ], @[ @0, @0, @0, @1 ], ^(NSInteger i, NSString *t) {
        if (i == 0 || i == 1) {
            [[NSUserDefaults standardUserDefaults] setBool:(i == 1) forKey:@"bm.store_osu"];
            [[NSUserDefaults standardUserDefaults] synchronize];
            osu_msg(@"Store", i == 1 ? @"osu! store selected." : @"Malody store selected.");
        } else if (i == 2) osu_menu();
    });
}
