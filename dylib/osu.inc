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
#define OSU_SRV_PORT 18088
#define OSU_SRV_SLOT 0            // サーバー一覧(sub_1000077A4)の先頭 = Main - Global(config の chan[0]=0 が使う枠)を、osu 接続のときだけ中継サーバーへ向ける
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
// 要求(パスとクエリ)→ 応答の JSON。ここを、記録した要求を見ながら育てる
static NSData *osu_srv_reply(NSString *path, NSDictionary<NSString *, NSString *> *q) {
    id obj = @{ @"code": @0, @"data": @[] };
    if ([path hasSuffix:@"/login"] || [path hasSuffix:@"/check"]) obj = @{ @"code": @0, @"data": @"osu-local-key", @"uid": @1 };
    return [NSJSONSerialization dataWithJSONObject:obj options:0 error:nil];
}
static void osu_srv_handle(int fd) {
    char buf[8192];
    ssize_t n = recv(fd, buf, sizeof buf - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = 0;
    NSString *req = [NSString stringWithUTF8String:buf] ?: @"";
    NSString *first = [req componentsSeparatedByString:@"\r\n"].firstObject ?: @"";
    NSArray *parts = [first componentsSeparatedByString:@" "];
    NSString *target = parts.count > 1 ? parts[1] : @"/";
    NSURLComponents *uc = [NSURLComponents componentsWithString:[@"http://x" stringByAppendingString:target]];
    NSMutableDictionary *q = [NSMutableDictionary dictionary];
    for (NSURLQueryItem *it in uc.queryItems) q[it.name] = it.value ?: @"";
    osu_srv_log([NSString stringWithFormat:@"%@ %@", parts.firstObject ?: @"?", target]);
    NSData *body = osu_srv_reply(uc.path ?: @"/", q);
    NSString *head = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)body.length];
    NSData *h = [head dataUsingEncoding:NSUTF8StringEncoding];
    send(fd, h.bytes, h.length, 0);
    send(fd, body.bytes, body.length, 0);
    close(fd);
}
static void osu_srv_start(void) {
    static BOOL started;
    if (started) return;
    started = YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        int s = socket(AF_INET, SOCK_STREAM, 0);
        int on = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        struct sockaddr_in a = { .sin_len = sizeof a, .sin_family = AF_INET, .sin_port = htons(OSU_SRV_PORT) };
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) { os_log(L, "osu-srv: bind/listen failed errno=%d", errno); close(s); return; }
        osu_srv_log(@"server started");
        for (;;) {
            int c = accept(s, NULL, NULL);
            if (c < 0) continue;
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ osu_srv_handle(c); });
        }
    });
}
// サーバー一覧の 1 件(OSU_SRV_SLOT)を、この中継サーバーへ向ける。一覧(sub_1000077A4)の各項目 = { std::string name; std::string url; int id }
static BOOL osu_store_on(void) { return [[NSUserDefaults standardUserDefaults] boolForKey:@"bm.store_osu"]; }
static void osu_srv_patch_list(void) {
    if (!osu_store_on()) return;
    void *mgr = ((void *(*)(void))VA(0x100007750ULL))();
    if (!mgr) return;
    void **b = *(void ***)mgr, **e = *(void ***)((uint8_t *)mgr + 8);
    if (!b || e <= b || (e - b) <= OSU_SRV_SLOT) { os_log(L, "osu-srv: server list not ready"); return; }
    uint8_t *ent = (uint8_t *)b[OSU_SRV_SLOT];
    un_assign(ent, "osu!");
    un_assign(ent + 24, [NSString stringWithFormat:@"http://127.0.0.1:%d/", OSU_SRV_PORT].UTF8String);
    os_log(L, "osu-srv: server list slot %d -> local", OSU_SRV_SLOT);
}
static void osu_srv_boot(void) {      // アプリ起動の少し後(サーバー一覧が作られてから)に呼ぶ
    osu_srv_start();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ osu_srv_patch_list(); });
}

// 設定画面のボタン: ストアの接続先の切り替え(次回起動から有効)/ osu! 譜面の検索・取り込み
static void osu_settings_menu(void) {
    BOOL on = osu_store_on();
    pl_alert(@"Store", [NSString stringWithFormat:@"Current store: %@\nChanges apply after restarting the app.", on ? @"osu! (sayobot, free)" : @"Malody (official)"], nil,
             @[ @"Use Malody store", @"Use osu! store (beta)", @"Search / import osu! charts", @"Close" ], @[ @0, @0, @0, @1 ], ^(NSInteger i, NSString *t) {
        if (i == 0 || i == 1) {
            [[NSUserDefaults standardUserDefaults] setBool:(i == 1) forKey:@"bm.store_osu"];
            [[NSUserDefaults standardUserDefaults] synchronize];
            osu_msg(@"Store", i == 1 ? @"osu! store selected. Restart the app to apply." : @"Malody store selected. Restart the app to apply.");
        } else if (i == 2) osu_menu();
    });
}
