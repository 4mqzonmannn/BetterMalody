// ============================================================================
// 曲を共有リンクにする(曲選択の「何をしますか?」メニューの「Share link」)
//   標準の「共有」(id 209)と同じ .mcz の書き出し(sub_10004A9F0)を使い、iOS の共有シート(sub_100020124)を出す代わりに、
//   ファイル共有サービスへ上げて、ダウンロード用のリンクをクリップボードへ入れる。受け取る側は、リンクを Safari などで開いて保存 →
//   共有メニューから Malody で開く(標準の取り込み口)。ボタンは押したときだけ動き、アップロード前に毎回確認を出す。
//   既定は確認なしで即アップロード(少人数運用の判断)。確認ダイアログ(UIKit)を出す = defaults write me.mugzone.m bm.share.confirm -bool YES。進捗・結果はゲーム標準のトースト。
//   サービスの既定は x0.at(直リンク・日本から速い)。defaults write me.mugzone.m bm.share.svc -int 1 で litterbox(72 時間で自動削除)。
// ============================================================================
#define BM_SHARE_BTN_ID 215
static BOOL gShareLinkPending;     // 「Share link」(id 215)を標準の「共有」(id 209)の経路で処理している間だけ YES

static NSDictionary *share_service(void) {
    if ([NSUserDefaults.standardUserDefaults integerForKey:@"bm.share.svc"] == 1)
        return @{ @"name": @"litterbox", @"url": @"https://litterbox.catbox.moe/resources/internals/api.php", @"file": @"fileToUpload",
                  @"fields": @{ @"reqtype": @"fileupload", @"time": @"72h" }, @"keep": @"The link expires in 72 hours." };
    return @{ @"name": @"x0.at", @"url": @"https://x0.at/", @"file": @"file",
              @"fields": @{ @"id_length": @"16" },          // 短い ID は総当たりで見つかるので長くする
              @"keep": @"The link stays valid for days to months (set by the service) and cannot be deleted." };
}
// 送るファイル名は ASCII だけにする(日本語などはヘッダを壊す恐れがあるので "song.mcz")
static NSString *share_ascii_name(NSString *path) {
    NSString *stem = path.lastPathComponent.stringByDeletingPathExtension;
    NSMutableString *o = [NSMutableString string];
    for (NSUInteger i = 0; i < stem.length; i++) {
        unichar c = [stem characterAtIndex:i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '.' || c == '_' || c == '-' || c == '(' || c == ')') [o appendFormat:@"%C", c];
    }
    NSString *t = [o stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    return [(t.length ? t : @"song") stringByAppendingString:@".mcz"];
}
// multipart の本体を一時ファイルへ書く(曲が大きくてもメモリに載せない)
static NSString *share_body_file(NSString *path, NSString *boundary, NSDictionary *svc) {
    NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"bm_share_body.bin"];
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm removeItemAtPath:tmp error:nil];
    if (![fm createFileAtPath:tmp contents:nil attributes:nil]) return nil;
    @try {
        NSFileHandle *w = [NSFileHandle fileHandleForWritingAtPath:tmp], *r = [NSFileHandle fileHandleForReadingAtPath:path];
        if (!w || !r) return nil;
        NSDictionary *fields = svc[@"fields"];
        for (NSString *k in fields) [w writeData:[[NSString stringWithFormat:@"--%@\r\nContent-Disposition: form-data; name=\"%@\"\r\n\r\n%@\r\n", boundary, k, fields[k]] dataUsingEncoding:NSUTF8StringEncoding]];
        [w writeData:[[NSString stringWithFormat:@"--%@\r\nContent-Disposition: form-data; name=\"%@\"; filename=\"%@\"\r\nContent-Type: application/octet-stream\r\n\r\n", boundary, svc[@"file"], share_ascii_name(path)] dataUsingEncoding:NSUTF8StringEncoding]];
        for (;;) {
            @autoreleasepool {
                NSData *d = [r readDataOfLength:1 << 20];
                if (!d.length) break;
                [w writeData:d];
            }
        }
        [w writeData:[[NSString stringWithFormat:@"\r\n--%@--\r\n", boundary] dataUsingEncoding:NSUTF8StringEncoding]];
        [w closeFile]; [r closeFile];
    } @catch (NSException *ex) { os_log(L, "share: body failed %{public}@", ex.reason); return nil; }
    return tmp;
}
// ゲーム標準のトースト(UIKit のダイアログを使わない)。アプリ全体の共有オブジェクトから出すので、画面が切り替わっても使える。メインスレッドで呼ぶ
static void share_toast(NSString *s, int ms) {
    uint8_t str[48] = {0};
    ((void (*)(void *, const char *))VA(0x10007309CULL))(str, s.UTF8String);
    void *m = ((void *(*)(void *))VA(0x100055C00ULL))(((void *(*)(void))VA(0x10005304CULL))());
    ((void (*)(void *, void *, int, int))VA(0x1001D15D0ULL))(m, str, 2, ms);
}
static void share_upload(NSString *path, NSDictionary *svc, NSString *human) {
    NSString *boundary = [NSString stringWithFormat:@"----BM%08x%08x", arc4random(), arc4random()];
    NSString *body = share_body_file(path, boundary, svc);
    if (!body) { share_toast(@"Share link: could not prepare the upload", 4000); return; }
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:svc[@"url"]] cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:90];
    rq.HTTPMethod = @"POST";
    [rq setValue:[@"multipart/form-data; boundary=" stringByAppendingString:boundary] forHTTPHeaderField:@"Content-Type"];
    [rq setValue:@"BetterMalody" forHTTPHeaderField:@"User-Agent"];
    NSURLSessionConfiguration *cfg = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    cfg.timeoutIntervalForResource = 600;
    NSURLSession *ses = [NSURLSession sessionWithConfiguration:cfg];
    __block BOOL done = NO;
    NSURLSessionUploadTask *task = [ses uploadTaskWithRequest:rq fromFile:[NSURL fileURLWithPath:body] completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        [NSFileManager.defaultManager removeItemAtPath:body error:nil];
        NSString *txt = d.length ? [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] : @"";
        NSString *url = [[[txt stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] componentsSeparatedByString:@"\n"].firstObject stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        long code = [r isKindOfClass:NSHTTPURLResponse.class] ? (long)((NSHTTPURLResponse *)r).statusCode : 0;
        dispatch_async(dispatch_get_main_queue(), ^{
            done = YES;
            if (!e && code / 100 == 2 && ([url hasPrefix:@"https://"] || [url hasPrefix:@"http://"])) {
                id pb = [(id)NSClassFromString(@"UIPasteboard") valueForKey:@"generalPasteboard"];
                ((void (*)(id, SEL, id))objc_msgSend)(pb, NSSelectorFromString(@"setString:"), url);
                os_log(L, "share: uploaded %{public}@", url);
                share_toast([NSString stringWithFormat:@"Link copied: %@", url], 7000);
            } else {
                os_log(L, "share: failed code=%ld err=%{public}@ body=%{public}@", code, e.localizedDescription, [txt substringToIndex:MIN(txt.length, 120)]);
                share_toast([NSString stringWithFormat:@"Share link: upload failed (%@)", e ? e.localizedDescription : [NSString stringWithFormat:@"HTTP %ld", code]], 6000);
            }
        });
    }];
    [ses finishTasksAndInvalidate];
    [task resume];
    __block void (^tick)(void);                          // 2 秒ごとに進み具合をトーストで出す
    tick = ^{
        if (done) { tick = nil; return; }
        int64_t tot = task.countOfBytesExpectedToSend, sent = task.countOfBytesSent;
        share_toast(tot > 0 ? [NSString stringWithFormat:@"Uploading... %d%% (%@ to %@)", (int)(100.0 * sent / tot), human, svc[@"name"]] : [NSString stringWithFormat:@"Uploading... (%@ to %@)", human, svc[@"name"]], 2300);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2000 * NSEC_PER_MSEC), dispatch_get_main_queue(), tick);
    };
    tick();
}
// 標準の共有の出口(sub_100020124)から来る。pathStr = 書き出した .mcz のパス(std::string *)
static void share_link_start(void *pathStr) {
    const char *c = pathStr ? stdstr_c((uint8_t *)pathStr) : NULL;
    NSString *path = c ? [NSString stringWithUTF8String:c] : nil;
    NSDictionary *at = path ? [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil] : nil;
    if (!at) { share_toast(@"Share link: the exported file was not found", 4000); return; }
    unsigned long long size = at.fileSize;
    NSString *human = size >= 1048576 ? [NSString stringWithFormat:@"%.1f MB", size / 1048576.0] : [NSString stringWithFormat:@"%llu KB", (size + 1023) / 1024];
    NSDictionary *svc = share_service();
    NSString *msg = [NSString stringWithFormat:@"Upload \"%@\" (%@) to %@ and copy the download link?\n\nAnyone with the link can download the song. %@\nMake sure you are allowed to share it.",
                     path.lastPathComponent.stringByDeletingPathExtension, human, svc[@"name"], svc[@"keep"]];
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"bm.share.confirm"]) { share_upload(path, svc, human); return; }   // 既定は確認なし(押したら即アップロード)。確認を出す = defaults write me.mugzone.m bm.share.confirm -bool YES
    pl_alert(@"Share link", msg, nil, @[ @"Upload", @"Cancel" ], @[ @0, @1 ], ^(NSInteger i, NSString *t) { if (i == 0) share_upload(path, svc, human); });
}
typedef void (*dlgbtn_fn)(void *scene, int id, float a3);
typedef void *(*sharepres_fn)(void *path);
static dlgbtn_fn orig_dlgbtn;
static sharepres_fn orig_sharepres;
static void hook_dlgbtn(void *scene, int id, float a3) {      // sub_10010A114: 曲メニューのボタン処理。id 215 は標準の「共有」(209)の経路を通す
    if (id == BM_SHARE_BTN_ID) { gShareLinkPending = YES; orig_dlgbtn(scene, 209, a3); gShareLinkPending = NO; return; }
    orig_dlgbtn(scene, id, a3);
}
static void *hook_sharepres(void *p) {                        // sub_100020124: 書き出した .mcz の共有シート。リンク作成中なら共有シートの代わりにアップロードへ
    if (gShareLinkPending) { gShareLinkPending = NO; share_link_start(p); return NULL; }
    return orig_sharepres(p);
}
