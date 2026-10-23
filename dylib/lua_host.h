// Malody V エディタプラグイン(Lua)ホスト。Lua 5.4 を内蔵し、Editor / Review テーブルを bm.m 側のコールバックへ橋渡しする。
// Lua のヘッダは識別子 L を使うため、bm.m(L = os_log)とは別翻訳単位にしてある。
#ifndef LUA_HOST_H
#define LUA_HOST_H
#include <stddef.h>

typedef struct { int m, num, den; } LHBeat;

typedef struct {
    int mode;                                   // 0 Key / 7 Slide(エディタの種別)
    int    (*sel)(int *ids, int max);           // 選択中のノート ID
    int    (*count)(void);                      // ノート総数
    int    (*at)(int idx);                      // 並び順(拍→x)での ID。範囲外は -1
    int    (*type)(int id);                     // NoteType (1 / 128 / 1024 / 2048)
    int    (*x)(int id);
    int    (*width)(int id);
    int    (*flag)(int id);
    int    (*beat)(int id, int head, LHBeat *out);       // 0=見つからない
    double (*time_ms)(int id, int head);
    int    (*set_beat)(int id, int head, LHBeat b);
    int    (*set_x)(int id, int x);
    int    (*set_width)(int id, int w);
    int    (*del)(int id);
    int    (*add)(int type);                    // 新規ノートの ID。失敗は -1
    int    (*select)(int id);
    void   (*sel_clear)(void);                 // BM 拡張: 選択を全解除
    int    (*seg_count)(int id);
    int    (*seg_beat)(int id, int i, LHBeat *out);
    int    (*seg_x)(int id, int i);
    void   (*cur_beat)(LHBeat *out);
    double (*cur_time)(void);
    int    (*cur_div)(void);
    void   (*toast)(const char *msg);
    const char *(*chart_info)(const char *key);  // version/creator/title/artist/bpm/level/key
    int    (*bpm_count)(void);
    int    (*bpm_at)(int i, LHBeat *b, double *bpm, double *delay);
    int    (*fx_count)(void);
    int    (*fx_at)(int i, LHBeat *b, const char **type, double *value);
    int    (*seg_add)(int id, LHBeat rel);                 // AddNoteSlideBody
    int    (*seg_set_x)(int id, int i, int x);
    int    (*seg_clear)(int id);
    const char *(*data_get)(const char *key, const char *psw);   // ReadData / WriteData(プラグイン間共有)
    void   (*data_set)(const char *key, const char *psw, const char *val);
    const char *(*file_read)(const char *name, size_t *len);    // ReadFile / WriteFile(譜面フォルダ)
    int    (*file_write)(const char *name, const char *data, size_t len);
    int    (*click_x)(void);                       // Interact: クリック位置のレーン / x
    void   (*click_beat)(LHBeat *out, int fine);   // Interact: クリック位置の拍(fine=1/32 吸着)
    void   (*fx_add)(LHBeat b, const char *type, double v);    // AddEffect
    void   (*fx_del)(LHBeat b, const char *type);              // DeleteEffect
    void   (*bpm_add)(LHBeat b, double bpm, double delay);     // AddTime
    void   (*bpm_del)(LHBeat b);                               // DeleteTime
    void   (*shortcut)(int id);                                // CallShortcut
    int    (*mod_add)(int kind, const char *name, const char *arg);   // AddText(0) / AddSprite(1)
    void   (*mod_set_num)(const char *name, const char *key, double v);
    void   (*mod_set_str)(const char *name, const char *key, const char *v);
    void   (*mod_set_beat)(const char *name, const char *key, LHBeat b);
    void   (*mod_remove)(const char *name);
    int    (*mod_exists)(const char *name);
    void   (*batch)(int begin);                 // StartBatch / FinishBatch
    void   (*seek_beat)(LHBeat b);
    void   (*seek_ms)(double ms);
    int    (*set_flag)(int id, int flag);       // SetNoteFlag(Taiko: ノートの種類)
    int    (*mod_get_num)(const char *name, const char *key, double *out);   // モジュールの値の読み出し(0 = 無し)
    void   (*mod_set_color)(const char *name, int r, int g, int b);   // module:SetColor(r,g,b)
    void   (*log)(const char *text);                      // print / デバッグ出力
    const char *(*custom_get)(const char *name);          // ReadChartCustom
    void   (*custom_set)(const char *name, const char *v);// WriteChartCustom
    void   (*request)(void *sess, const char *tag, const char *url, const char *method, const char *data);   // DoRequest(結果は lh_response で返す)
    void   (*choice)(void *sess, int id, int kind, const char *title, const char *const *opts, int n);   // kind 1=GetUserSelect(はい/いいえ) 2=GetUserOption(最大 32)
    void   (*mod_set_parent)(const char *name, const char *parent);
    void   (*slider)(void *sess, int id, int n, const char *const *titles, const double *mins, const double *maxs, const double *defs, const double *steps);   // GetUserSlider(n=1) / GetUserSlider2(n=2)
    void   (*pick_bytes)(void *sess, int id, const char *prompt);   // ReadBytesSelect: ファイル選択 → lh_bytes_deliver
    void   (*tip)(const char *text);                       // ShowTip: 常設の 1 行表示(空文字で消す)
    void   (*input)(void *sess, int id, const char *title, const char *def);   // GetUserInput: 入力待ちを登録(結果は lh_input_deliver で返す)
    int    (*order)(int *ids, int max);                  // GetOrderNotes: 「順番に選択」でタップした順のノート ID
    void   (*order_clear)(void);                        // ClearOrderNotes
} LHHost;

typedef struct {
    char name[96];
    int mode, type;           // -1 = 未指定
    char require[24];
    int has_run, has_tool;
    char err[200];            // 空でなければ読み込みエラー
    char icon[96];            // PluginIcon(プラグインと同じフォルダの画像名)
} LHInfo;

// プラグインのヘッダ変数(PluginName など)だけを調べる(関数は実行しない)。0=成功
int lh_inspect(const char *src, size_t len, const char *chunkname, LHInfo *out);
typedef struct LHSession LHSession;
// Run() を実行する。0=成功。失敗時 err に文章。入力待ち(GetUserInput)が残っていれば *keep に状態を返す(呼び出し側が lh_close)
int lh_run(const char *src, size_t len, const char *chunkname, const LHHost *host, char *err, size_t errn, LHSession **keep);
// 入力結果をコールバックへ渡す。text=NULL ならキャンセル(コールバックは呼ばない)
int lh_input_deliver(LHSession *s, int id, const char *text, char *err, size_t errn);
int lh_pending(LHSession *s);
// スライダーの結果(n 個の数値)をコールバックへ渡す
int lh_slider_deliver(LHSession *s, int id, int n, const double *vals, char *err, size_t errn);
// 選択の結果。kind 1 は boolean(value!=0)、kind 2 は整数(0 始まり)をコールバックへ渡す
int lh_choice_deliver(LHSession *s, int id, int kind, int value, char *err, size_t errn);
// ReadBytesSelect の結果。data=NULL ならキャンセル(コールバックへ nil を渡す)
int lh_bytes_deliver(LHSession *s, int id, const unsigned char *data, size_t n, char *err, size_t errn);
// DoRequest の結果。OnResponse(tag, content) を呼ぶ(未定義なら何もしない)
int lh_response(LHSession *s, const char *tag, const char *content, char *err, size_t errn);

// Interact 型: 状態(グローバル変数)を保ったまま、イベントごとに関数を呼ぶ
LHSession *lh_open(const char *src, size_t len, const char *chunkname, const LHHost *host, char *err, size_t errn);
// fn が未定義なら 0(成功)。ints は整数引数
int lh_call(LHSession *s, const char *fn, const int *ints, int nints, char *err, size_t errn);
// OnKey(key, down): down は boolean。未定義なら 0
int lh_call_key(LHSession *s, int key, int down, char *err, size_t errn);
void lh_close(LHSession *s);

#endif
