// Malody V エディタプラグイン API → bm.m コールバック。Lua 5.4(third_party/lua)を使う。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua_host.h"

static const LHHost *H;           // 実行中のホスト(メインスレッド同期実行のため単一でよい)
static clock_t gDeadline;
struct LHSession { lua_State *L; const LHHost *host; int pending; };
static LHSession *gCur;           // 実行中のセッション(GetUserInput がコールバックを紐づける)

// ---- 安全策: 標準ライブラリは base/table/string/math/utf8/coroutine のみ。実行時間は 3 秒で打ち切る ----
static void hook_timeout(lua_State *L, lua_Debug *ar) {
    (void)ar;
    if (clock() > gDeadline) luaL_error(L, "plugin timed out (3s)");
}
// ---- print / os(安全な部分だけ) ----
static int l_print(lua_State *L) {
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1) luaL_addchar(&b, '\t');
        lua_pop(L, 1);
        luaL_addlstring(&b, s, len);
    }
    luaL_pushresult(&b);
    if (H && H->log) H->log(lua_tostring(L, -1));
    return 0;
}
static int os_time_f(lua_State *L) {
    if (lua_isnoneornil(L, 1)) { lua_pushinteger(L, (lua_Integer)time(NULL)); return 1; }
    luaL_checktype(L, 1, LUA_TTABLE);
    struct tm t; memset(&t, 0, sizeof t);
    #define F(k, d) (lua_getfield(L, 1, k), (int)luaL_optinteger(L, -1, d))
    t.tm_year = F("year", 1970) - 1900; t.tm_mon = F("month", 1) - 1; t.tm_mday = F("day", 1);
    t.tm_hour = F("hour", 12); t.tm_min = F("min", 0); t.tm_sec = F("sec", 0);
    #undef F
    lua_pop(L, 6);
    t.tm_isdst = -1;
    lua_pushinteger(L, (lua_Integer)mktime(&t));
    return 1;
}
static int os_clock_f(lua_State *L) { lua_pushnumber(L, (lua_Number)clock() / CLOCKS_PER_SEC); return 1; }
static int os_difftime_f(lua_State *L) { lua_pushnumber(L, (lua_Number)(luaL_checkinteger(L, 1) - luaL_optinteger(L, 2, 0))); return 1; }
static int os_date_f(lua_State *L) {
    const char *f = luaL_optstring(L, 1, "%c");
    time_t t = lua_isnoneornil(L, 2) ? time(NULL) : (time_t)luaL_checkinteger(L, 2);
    int utc = 0;
    if (*f == '!') { utc = 1; f++; }
    struct tm tm;
    if (utc) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
    if (!strcmp(f, "*t")) {
        lua_createtable(L, 0, 9);
        #define S(k, v) (lua_pushinteger(L, v), lua_setfield(L, -2, k))
        S("year", tm.tm_year + 1900); S("month", tm.tm_mon + 1); S("day", tm.tm_mday); S("hour", tm.tm_hour);
        S("min", tm.tm_min); S("sec", tm.tm_sec); S("yday", tm.tm_yday + 1); S("wday", tm.tm_wday + 1);
        #undef S
        lua_pushboolean(L, tm.tm_isdst > 0); lua_setfield(L, -2, "isdst");
        return 1;
    }
    char buf[256];
    size_t n = strftime(buf, sizeof buf, f, &tm);
    lua_pushlstring(L, buf, n);
    return 1;
}
static void install_extras(lua_State *L) {
    lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
    static const luaL_Reg os[] = { { "time", os_time_f }, { "clock", os_clock_f }, { "difftime", os_difftime_f }, { "date", os_date_f }, { NULL, NULL } };
    lua_newtable(L);
    luaL_setfuncs(L, os, 0);
    lua_setglobal(L, "os");
}
static lua_State *new_state(void) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 }, { LUA_COLIBNAME, luaopen_coroutine },
        { NULL, NULL } };
    for (const luaL_Reg *l = libs; l->func; l++) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
    static const char *const bad[] = { "dofile", "loadfile", "require", "collectgarbage", NULL };
    for (int i = 0; bad[i]; i++) { lua_pushnil(L); lua_setglobal(L, bad[i]); }
    install_extras(L);
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_sethook(L, hook_timeout, LUA_MASKCOUNT, 100000);
    return L;
}

// ---- Beat テーブル {beat, numor, denom} ----
static void push_beat(lua_State *L, LHBeat b) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, b.m);   lua_setfield(L, -2, "beat");
    lua_pushinteger(L, b.num); lua_setfield(L, -2, "numor");
    lua_pushinteger(L, b.den); lua_setfield(L, -2, "denom");
}
static LHBeat check_beat(lua_State *L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    LHBeat b;
    lua_getfield(L, idx, "beat");  b.m = (int)lua_tointeger(L, -1);
    lua_getfield(L, idx, "numor"); b.num = (int)lua_tointeger(L, -1);
    lua_getfield(L, idx, "denom"); b.den = (int)lua_tointeger(L, -1);
    lua_pop(L, 3);
    if (b.den <= 0) b.den = 1;
    return b;
}
static void push_array0(lua_State *L, const int *v, int n) {   // C# 配列互換: [0..n-1] と Length
    lua_createtable(L, n, 1);
    for (int i = 0; i < n; i++) { lua_pushinteger(L, v[i]); lua_rawseti(L, -2, i); }
    lua_pushinteger(L, n); lua_setfield(L, -2, "Length");
}

// Editor:Method(...) — 先頭は self。ID は整数
#define ARG(n) ((n) + 1)
static int e_GetSelectNotes(lua_State *L) {
    int ids[4096];
    int n = H->sel ? H->sel(ids, 4096) : 0;
    push_array0(L, ids, n);
    return 1;
}
static int e_GetOrderNotes(lua_State *L) {
    int ids[4096];
    int n = H->order ? H->order(ids, 4096) : 0;
    push_array0(L, ids, n);
    return 1;
}
static int e_ClearOrderNotes(lua_State *L) { (void)L; if (H->order_clear) H->order_clear(); return 0; }
static int e_GetNoteCount(lua_State *L) { lua_pushinteger(L, H->count()); return 1; }
static int e_GetNoteAt(lua_State *L) { lua_pushinteger(L, H->at((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetCurrentTime(lua_State *L) { lua_pushnumber(L, H->cur_time()); return 1; }
static int e_GetCurrentBeat(lua_State *L) { LHBeat b; H->cur_beat(&b); push_beat(L, b); return 1; }
static int e_GetCurrentDivide(lua_State *L) { lua_pushinteger(L, H->cur_div()); return 1; }
static int head_arg(lua_State *L, int idx) { return lua_isnoneornil(L, idx) ? 1 : lua_toboolean(L, idx); }
static int e_GetNoteTime(lua_State *L) {
    lua_pushnumber(L, H->time_ms((int)luaL_checkinteger(L, ARG(1)), head_arg(L, ARG(2)))); return 1;
}
static int e_GetNoteBeat(lua_State *L) {
    LHBeat b;
    if (!H->beat((int)luaL_checkinteger(L, ARG(1)), head_arg(L, ARG(2)), &b)) b = (LHBeat){ -1, -1, -1 };
    push_beat(L, b);
    return 1;
}
static int e_GetNoteX(lua_State *L) { lua_pushinteger(L, H->x((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetNoteType(lua_State *L) { lua_pushinteger(L, H->type((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetNoteFlag(lua_State *L) { lua_pushinteger(L, H->flag((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetNoteWidth(lua_State *L) { lua_pushinteger(L, H->width((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetNoteSlideBodyCount(lua_State *L) { lua_pushinteger(L, H->seg_count((int)luaL_checkinteger(L, ARG(1)))); return 1; }
static int e_GetNoteSlideBodyBeat(lua_State *L) {
    LHBeat b;
    if (!H->seg_beat((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2)), &b)) b = (LHBeat){ -1, -1, -1 };
    push_beat(L, b);
    return 1;
}
static int e_GetNoteSlideBodyX(lua_State *L) {
    lua_pushinteger(L, H->seg_x((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2)))); return 1;
}
static int e_GetNoteGroup(lua_State *L) { (void)L; lua_pushinteger(L, 0); return 1; }
static int e_ChartInfo(lua_State *L) {
    const char *v = H->chart_info ? H->chart_info(luaL_checkstring(L, ARG(1))) : NULL;
    if (v) lua_pushstring(L, v); else lua_pushstring(L, "");
    return 1;
}
static int e_GetSystemTime(lua_State *L) { lua_pushinteger(L, (lua_Integer)time(NULL)); return 1; }
static int e_GetClickX(lua_State *L) { lua_pushinteger(L, H->click_x ? H->click_x() : -1); return 1; }
static int click_beat_common(lua_State *L, int fine) {
    LHBeat b;
    if (H->click_beat) H->click_beat(&b, fine); else H->cur_beat(&b);
    push_beat(L, b);
    return 1;
}
static int e_GetClickBeat(lua_State *L) { return click_beat_common(L, 0); }
static int e_GetClickBeatFree(lua_State *L) { return click_beat_common(L, 1); }

static int e_GetTimeCount(lua_State *L) { lua_pushinteger(L, H->bpm_count()); return 1; }
static int e_GetTimeAt(lua_State *L) {
    LHBeat b; double bpm = 0, dl = 0;
    if (!H->bpm_at((int)luaL_checkinteger(L, ARG(1)), &b, &bpm, &dl)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    push_beat(L, b); lua_setfield(L, -2, "beat");
    lua_pushnumber(L, bpm); lua_setfield(L, -2, "bpm");
    lua_pushnumber(L, dl); lua_setfield(L, -2, "delay");
    return 1;
}
static int e_GetEffectCount(lua_State *L) { lua_pushinteger(L, H->fx_count()); return 1; }
static int e_GetEffectAt(lua_State *L) {
    LHBeat b; const char *t = ""; double v = 0;
    if (!H->fx_at((int)luaL_checkinteger(L, ARG(1)), &b, &t, &v)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    push_beat(L, b); lua_setfield(L, -2, "beat");
    lua_pushstring(L, t); lua_setfield(L, -2, "type");
    lua_pushnumber(L, v); lua_setfield(L, -2, "value");
    return 1;
}

// ---- 編集 ----
static int e_StartBatch(lua_State *L) { (void)L; if (H->batch) H->batch(1); return 0; }
static int e_FinishBatch(lua_State *L) { (void)L; if (H->batch) H->batch(0); return 0; }
static int e_DeleteNote(lua_State *L) { H->del((int)luaL_checkinteger(L, ARG(1))); return 0; }
static int e_AddNote(lua_State *L) {
    int id = H->add ? H->add((int)luaL_checkinteger(L, ARG(1))) : -1;
    if (id < 0) return luaL_error(L, "AddNote: not supported for this note type");
    lua_pushinteger(L, id);
    return 1;
}
static int e_SetNoteBeat(lua_State *L) {
    int id = (int)luaL_checkinteger(L, ARG(1));
    LHBeat b = check_beat(L, ARG(2));
    H->set_beat(id, head_arg(L, ARG(3)), b);
    return 0;
}
static int e_SetNoteX(lua_State *L) { H->set_x((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2))); return 0; }
static int e_SetNoteFlag(lua_State *L) { if (H->set_flag) H->set_flag((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2))); return 0; }
static int e_SetNoteWidth(lua_State *L) { H->set_width((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2))); return 0; }
static int e_SelectNote(lua_State *L) { if (H->select) H->select((int)luaL_checkinteger(L, ARG(1))); return 0; }
static int e_AddNoteSlideBody(lua_State *L) {
    if (H->seg_add) H->seg_add((int)luaL_checkinteger(L, ARG(1)), check_beat(L, ARG(2)));
    return 0;
}
static int e_SetNoteSlideBodyX(lua_State *L) {
    if (H->seg_set_x) H->seg_set_x((int)luaL_checkinteger(L, ARG(1)), (int)luaL_checkinteger(L, ARG(2)), (int)luaL_checkinteger(L, ARG(3)));
    return 0;
}
static int e_DeleteNoteSlideBody(lua_State *L) {
    if (H->seg_clear) H->seg_clear((int)luaL_checkinteger(L, ARG(1)));
    return 0;
}
static int e_ReadData(lua_State *L) {
    const char *v = H->data_get ? H->data_get(luaL_checkstring(L, ARG(1)), luaL_optstring(L, ARG(2), "")) : NULL;
    if (v) lua_pushstring(L, v); else lua_pushnil(L);
    return 1;
}
static int e_WriteData(lua_State *L) {
    if (H->data_set) H->data_set(luaL_checkstring(L, ARG(1)), luaL_optstring(L, ARG(2), ""), luaL_optstring(L, ARG(3), ""));
    return 0;
}
static int e_ReadFile(lua_State *L) {
    size_t n = 0;
    const char *v = H->file_read ? H->file_read(luaL_checkstring(L, ARG(1)), &n) : NULL;
    if (v) lua_pushlstring(L, v, n); else lua_pushnil(L);
    return 1;
}
static int e_WriteFile(lua_State *L) {
    size_t n = 0;
    const char *d = luaL_checklstring(L, ARG(2), &n);
    if (H->file_write) H->file_write(luaL_checkstring(L, ARG(1)), d, n);
    return 0;
}
// モジュール: プロキシのテーブル。代入(m.X = 10 など)をホストへ中継する。読み出しは代入済みの値
static int mod_newindex(lua_State *L) {       // upvalue1 = 名前, upvalue2 = 値の保管テーブル / 引数: (proxy, key, value)
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    lua_pushvalue(L, 2); lua_pushvalue(L, 3);
    lua_rawset(L, lua_upvalueindex(2));
    if (lua_type(L, 2) != LUA_TSTRING || !H) return 0;
    const char *key = lua_tostring(L, 2);
    switch (lua_type(L, 3)) {
        case LUA_TNUMBER: if (H->mod_set_num) H->mod_set_num(name, key, lua_tonumber(L, 3)); break;
        case LUA_TBOOLEAN: if (H->mod_set_num) H->mod_set_num(name, key, lua_toboolean(L, 3) ? 1 : 0); break;
        case LUA_TSTRING: if (H->mod_set_str) H->mod_set_str(name, key, lua_tostring(L, 3)); break;
        case LUA_TTABLE: {
            lua_getfield(L, 3, "beat");
            int isbeat = lua_isnumber(L, -1);
            lua_pop(L, 1);
            if (isbeat && H->mod_set_beat) H->mod_set_beat(name, key, check_beat(L, 3));
            break;
        }
    }
    return 0;
}
static int mod_setcolor(lua_State *L) {       // module:SetColor(r, g, b) / upvalue1 = 名前
    if (H && H->mod_set_color)
        H->mod_set_color(lua_tostring(L, lua_upvalueindex(1)), (int)luaL_optinteger(L, 2, 255), (int)luaL_optinteger(L, 3, 255), (int)luaL_optinteger(L, 4, 255));
    return 0;
}
static int mod_index(lua_State *L) {         // upvalue1 = 名前, upvalue2 = 値の保管テーブル / 引数: (proxy, key)。ホストの値が優先
    if (H && H->mod_get_num && lua_type(L, 2) == LUA_TSTRING) {
        double v;
        if (H->mod_get_num(lua_tostring(L, lua_upvalueindex(1)), lua_tostring(L, 2), &v)) { lua_pushnumber(L, v); return 1; }
    }
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(2));
    return 1;
}
static void push_module(lua_State *L, const char *name) {
    lua_newtable(L);                              // proxy
    lua_newtable(L);                              // metatable
    lua_newtable(L);                              // store
    lua_pushstring(L, name);
    lua_pushvalue(L, -2);                         // store を upvalue2 へ
    lua_pushcclosure(L, mod_index, 2);
    lua_setfield(L, -3, "__index");
    lua_pushstring(L, name);
    lua_pushcclosure(L, mod_setcolor, 1);
    lua_setfield(L, -2, "SetColor");
    lua_pushstring(L, name);
    lua_pushvalue(L, -2);                         // store を upvalue2 へ
    lua_pushcclosure(L, mod_newindex, 2);
    lua_setfield(L, -3, "__newindex");
    lua_pop(L, 1);                                // store を除去(metatable に保持済み)
    lua_setmetatable(L, -2);
    lua_pushstring(L, name); lua_setfield(L, -2, "Name");
}
static int e_AddText(lua_State *L) {
    const char *n = luaL_checkstring(L, ARG(1));
    if (H->mod_add) H->mod_add(0, n, luaL_optstring(L, ARG(2), ""));
    if (H->mod_set_parent && lua_isstring(L, ARG(3))) H->mod_set_parent(n, lua_tostring(L, ARG(3)));
    push_module(L, n);
    return 1;
}
static int e_AddSprite(lua_State *L) {
    const char *n = luaL_checkstring(L, ARG(1));
    if (H->mod_add) H->mod_add(1, n, luaL_optstring(L, ARG(2), ""));
    if (H->mod_set_parent && lua_isstring(L, ARG(3))) H->mod_set_parent(n, lua_tostring(L, ARG(3)));
    push_module(L, n);
    return 1;
}
static int e_FindModule(lua_State *L) {
    const char *n = luaL_checkstring(L, ARG(1));
    if (!H->mod_exists || !H->mod_exists(n)) { lua_pushnil(L); return 1; }
    push_module(L, n);
    return 1;
}
static int e_RemoveModule(lua_State *L) {
    if (H->mod_remove) H->mod_remove(luaL_checkstring(L, ARG(1)));
    return 0;
}
static int e_AddEffect(lua_State *L) {
    if (H->fx_add) H->fx_add(check_beat(L, ARG(1)), luaL_checkstring(L, ARG(2)), luaL_checknumber(L, ARG(3)));
    return 0;
}
static int e_DeleteEffect(lua_State *L) {
    if (H->fx_del) H->fx_del(check_beat(L, ARG(1)), luaL_checkstring(L, ARG(2)));
    return 0;
}
static int e_AddTime(lua_State *L) {
    if (H->bpm_add) H->bpm_add(check_beat(L, ARG(1)), luaL_checknumber(L, ARG(2)), luaL_optnumber(L, ARG(3), 0));
    return 0;
}
static int e_DeleteTime(lua_State *L) {
    if (H->bpm_del) H->bpm_del(check_beat(L, ARG(1)));
    return 0;
}
static int e_CallShortcut(lua_State *L) {
    if (H->shortcut) H->shortcut((int)luaL_checkinteger(L, ARG(1)));
    return 0;
}
static int e_Unsupported(lua_State *L) {   // 未対応 API は例外にせず無視する(プラグイン全体が止まらないように)。呼ばれたことはログへ出す
    const char *nm = lua_tostring(L, lua_upvalueindex(1));
    if (nm && H && H->log) { char b[96]; snprintf(b, sizeof b, "unsupported API ignored: Editor:%s", nm); H->log(b); }
    return 0;
}

// ---- 補助 ----
static int e_ShowTip(lua_State *L) {
    const char *t = lua_isstring(L, ARG(1)) ? lua_tostring(L, ARG(1)) : "";
    if (H->tip) H->tip(t); else if (*t) H->toast(t);
    return 0;
}
static int e_ShowMessage(lua_State *L) { H->toast(luaL_tolstring(L, ARG(1), NULL)); return 0; }
static int e_MakeBeat(lua_State *L) {
    LHBeat b = { (int)luaL_checkinteger(L, ARG(1)), (int)luaL_optinteger(L, ARG(2), 0), (int)luaL_optinteger(L, ARG(3), 1) };
    if (b.den <= 0) b.den = 1;
    push_beat(L, b);
    return 1;
}
static void norm(long long *m, long long *n, long long *d) {
    while (*n >= *d) { *n -= *d; (*m)++; }
    while (*n < 0) { *n += *d; (*m)--; }
}
static long long gcdll(long long a, long long b) { if (a < 0) a = -a; while (b) { long long t = a % b; a = b; b = t; } return a; }
static int beat_arith(lua_State *L, int sign) {
    LHBeat a = check_beat(L, ARG(1)), b = check_beat(L, ARG(2));
    long long d = (long long)a.den / gcdll(a.den, b.den) * b.den;
    long long n = (long long)a.num * (d / a.den) + sign * (long long)b.num * (d / b.den);
    long long m = (long long)a.m + sign * (long long)b.m;
    norm(&m, &n, &d);
    long long g = gcdll(n, d); if (g > 1) { n /= g; d /= g; }
    push_beat(L, (LHBeat){ (int)m, (int)n, (int)d });
    return 1;
}
static int e_BeatAdd(lua_State *L) { return beat_arith(L, +1); }
static int e_BeatMinus(lua_State *L) { return beat_arith(L, -1); }
static int e_Beat(lua_State *L) { if (H->seek_beat) H->seek_beat(check_beat(L, ARG(1))); return 0; }
static int e_SeekTo(lua_State *L) {
    if (lua_istable(L, ARG(1))) { if (H->seek_beat) H->seek_beat(check_beat(L, ARG(1))); }
    else if (H->seek_ms) H->seek_ms(luaL_checknumber(L, ARG(1)));
    return 0;
}
// 選択肢の入力(見出しに "(0/1)" のような 2〜8 個の選択肢が書かれている)は、文字入力ではなく選択ダイアログにして、選んだ文字列を返す
static int input_choice_wrap(lua_State *L) {      // upvalue1 = 元のコールバック, upvalue2 = 選択肢の表 / 引数: 0 始まりの番号
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_rawgeti(L, lua_upvalueindex(2), luaL_optinteger(L, 1, 0) + 1);
    lua_call(L, 1, 0);
    return 0;
}
static int title_choices(const char *t, char tok[8][24], int *n) {
    const char *open = strrchr(t, '(');
    if (!open) return 0;
    const char *p = open + 1;
    int cnt = 0;
    while (*p && *p != ')' && cnt < 8) {
        int k = 0;
        while (*p && *p != '/' && *p != ')') { if (k < 23) tok[cnt][k++] = *p; p++; }
        tok[cnt][k] = 0;
        if (k == 0) return 0;
        for (int i = 0; i < k; i++) if (tok[cnt][i] == ' ' || tok[cnt][i] == '.' || tok[cnt][i] == ',') return 0;
        cnt++;
        if (*p == '/') p++;
    }
    if (*p != ')' || cnt < 2) return 0;
    *n = cnt;
    return 1;
}
static int e_GetUserInput(lua_State *L) {   // GetUserInput(title [, default], callback): ゲーム内入力欄に出し、確定時にコールバックを呼ぶ
    int fi = lua_isfunction(L, ARG(3)) ? ARG(3) : ARG(2);
    if (!lua_isfunction(L, fi)) return 0;
    // 公式: GetUserInput(default, cb) (5.4.72〜) / GetUserInput(title, default, cb) (6.0.52〜)。
    // 2 引数形の文字列が数値なら既定値("4" など)、文章なら見出しとして扱う(配布プラグインは "输入缩放中心(0/1)" のように案内文を渡すため)
    const char *title = "Input", *def = "";
    if (fi == ARG(3)) {
        if (lua_isstring(L, ARG(1))) title = lua_tostring(L, ARG(1));
        if (lua_isstring(L, ARG(2))) def = lua_tostring(L, ARG(2));
    } else if (lua_isstring(L, ARG(1))) {
        const char *a = lua_tostring(L, ARG(1)), *q = a;
        int numeric = *q != 0;
        for (; *q; q++) if (!((*q >= '0' && *q <= '9') || *q == '.' || *q == '-' || *q == '/' || *q == ',' || *q == ' ')) { numeric = 0; break; }
        if (numeric) def = a; else title = a;
    }
    {
        char tok[8][24]; int tn = 0;
        if (gCur && H->choice && title_choices(title, tok, &tn)) {
            const char *opts[8];
            lua_createtable(L, tn, 0);
            for (int i = 0; i < tn; i++) { opts[i] = tok[i]; lua_pushstring(L, tok[i]); lua_rawseti(L, -2, i + 1); }
            lua_pushvalue(L, fi);
            lua_insert(L, -2);                       // [cb, table]
            lua_pushcclosure(L, input_choice_wrap, 2);
            int id = luaL_ref(L, LUA_REGISTRYINDEX);
            gCur->pending++;
            H->choice(gCur, id, 2, title, opts, tn);
            return 0;
        }
    }
    if (!gCur || !H->input) {            // 入力欄が無い環境: 既定値で即コールバック
        lua_pushvalue(L, fi); lua_pushstring(L, def); lua_call(L, 1, 0);
        return 0;
    }
    lua_pushvalue(L, fi);
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    H->input(gCur, id, title, def);
    return 0;
}
static int e_GetUserSelect(lua_State *L) {   // GetUserSelect(title, callback(bool))
    if (!lua_isfunction(L, ARG(2))) return 0;
    const char *title = lua_isstring(L, ARG(1)) ? lua_tostring(L, ARG(1)) : "";
    if (!gCur || !H->choice) { lua_pushvalue(L, ARG(2)); lua_pushboolean(L, 1); lua_call(L, 1, 0); return 0; }
    lua_pushvalue(L, ARG(2));
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    static const char *const yn[2] = { "Yes", "No" };
    H->choice(gCur, id, 1, title, yn, 2);
    return 0;
}
// GetUserOption(title, opt1, opt2, opt3, opt4, callback(int)) / 拡張: GetUserOption(title, {opt1, opt2, ...}, callback)(最大 32 択)
static int e_GetUserOption(lua_State *L) {
    int fi = 0, top = lua_gettop(L);
    for (int i = ARG(2); i <= top; i++) if (lua_isfunction(L, i)) { fi = i; break; }
    if (!fi) return 0;
    const char *title = lua_isstring(L, ARG(1)) ? lua_tostring(L, ARG(1)) : "";
    const char *opts[32];
    int n = 0;
    if (lua_istable(L, ARG(2))) {
        int len = (int)luaL_len(L, ARG(2));
        for (int i = 1; i <= len && n < 32; i++) {
            lua_rawgeti(L, ARG(2), i);
            const char *s = lua_tostring(L, -1);
            lua_pop(L, 1);                       // 文字列は表が保持している間は有効
            if (s) opts[n++] = s;
        }
    } else {
        for (int k = ARG(2); k < fi && n < 32; k++) if (lua_isstring(L, k) && *lua_tostring(L, k)) opts[n++] = lua_tostring(L, k);
    }
    if (!n) opts[n++] = "OK";
    if (!gCur || !H->choice) { lua_pushvalue(L, fi); lua_pushinteger(L, 0); lua_call(L, 1, 0); return 0; }
    lua_pushvalue(L, fi);
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    H->choice(gCur, id, 2, title, opts, n);      // ホストは即座に文字列を複製する
    return 0;
}
// GetUserSlider(title, min, max, default [, step], callback(number)) — BM 拡張: ゲームの設定ダイアログと同じゲージで数値を受ける
static double auto_step(double mn, double mx, double df) {
    if (mn == floor(mn) && mx == floor(mx) && df == floor(df)) return 1;
    return (mx - mn) / 100.0;
}
static int e_GetUserSlider(lua_State *L) {
    int fi = 0, top = lua_gettop(L);
    for (int i = ARG(4); i <= top; i++) if (lua_isfunction(L, i)) { fi = i; break; }
    if (!fi) return 0;
    const char *title = lua_isstring(L, ARG(1)) ? lua_tostring(L, ARG(1)) : "";
    double mn = luaL_checknumber(L, ARG(2)), mx = luaL_checknumber(L, ARG(3));
    double df = fi > ARG(4) && lua_isnumber(L, ARG(4)) ? lua_tonumber(L, ARG(4)) : mn;
    double st = (fi > ARG(5) && lua_isnumber(L, ARG(5))) ? lua_tonumber(L, ARG(5)) : auto_step(mn, mx, df);
    if (mx < mn) { double t = mn; mn = mx; mx = t; }
    if (!gCur || !H->slider) { lua_pushvalue(L, fi); lua_pushnumber(L, df); lua_call(L, 1, 0); return 0; }
    lua_pushvalue(L, fi);
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    H->slider(gCur, id, 1, &title, &mn, &mx, &df, &st);
    return 0;
}
// GetUserSlider2(title1, min1, max1, def1, title2, min2, max2, def2, callback(number, number))
static int e_GetUserSlider2(lua_State *L) {
    int fi = 0, top = lua_gettop(L);
    for (int i = ARG(8); i <= top; i++) if (lua_isfunction(L, i)) { fi = i; break; }
    if (!fi) return 0;
    const char *titles[2];
    double mn[2], mx[2], df[2], st[2];
    for (int k = 0; k < 2; k++) {
        int b = ARG(1) + k * 4;
        titles[k] = lua_isstring(L, b) ? lua_tostring(L, b) : "";
        mn[k] = luaL_checknumber(L, b + 1); mx[k] = luaL_checknumber(L, b + 2);
        df[k] = luaL_optnumber(L, b + 3, mn[k]);
        if (mx[k] < mn[k]) { double t = mn[k]; mn[k] = mx[k]; mx[k] = t; }
        st[k] = auto_step(mn[k], mx[k], df[k]);
    }
    if (!gCur || !H->slider) { lua_pushvalue(L, fi); lua_pushnumber(L, df[0]); lua_pushnumber(L, df[1]); lua_call(L, 2, 0); return 0; }
    lua_pushvalue(L, fi);
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    H->slider(gCur, id, 2, titles, mn, mx, df, st);
    return 0;
}
static int r_SetResult(lua_State *L) {
    char b[200];
    snprintf(b, sizeof b, "Review: %s %s", lua_toboolean(L, ARG(1)) ? "OK" : "NG", luaL_optstring(L, ARG(2), ""));
    H->toast(b);
    if (H->tip) H->tip(b);       // 結果は常設の 1 行にも残す
    return 0;
}
static int e_ReadBytes(lua_State *L) {       // ReadBytes(name): 譜面フォルダのファイルを 0 始まりの配列で返す(C# 配列互換)
    size_t n = 0;
    const char *v = H->file_read ? H->file_read(luaL_checkstring(L, ARG(1)), &n) : NULL;
    if (!v || n > 8u * 1024 * 1024) { lua_pushnil(L); return 1; }
    lua_createtable(L, (int)n, 1);
    for (size_t i = 0; i < n; i++) { lua_pushinteger(L, (unsigned char)v[i]); lua_rawseti(L, -2, (lua_Integer)i); }
    lua_pushinteger(L, (lua_Integer)n); lua_setfield(L, -2, "Length");
    return 1;
}
static int e_ReadBytesSelect(lua_State *L) {   // ReadBytesSelect(prompt, callback(bytes | nil))
    int fi = lua_isfunction(L, ARG(2)) ? ARG(2) : (lua_isfunction(L, ARG(1)) ? ARG(1) : 0);
    if (!fi) return 0;
    if (!gCur || !H->pick_bytes) { lua_pushvalue(L, fi); lua_pushnil(L); lua_call(L, 1, 0); return 0; }
    lua_pushvalue(L, fi);
    int id = luaL_ref(L, LUA_REGISTRYINDEX);
    gCur->pending++;
    H->pick_bytes(gCur, id, lua_isstring(L, ARG(1)) ? lua_tostring(L, ARG(1)) : "");
    return 0;
}
static int e_ReadChartCustom(lua_State *L) {
    const char *v = H->custom_get ? H->custom_get(luaL_checkstring(L, ARG(1))) : NULL;
    if (v) lua_pushstring(L, v); else lua_pushnil(L);
    return 1;
}
static int e_WriteChartCustom(lua_State *L) {
    if (H->custom_set) H->custom_set(luaL_checkstring(L, ARG(1)), luaL_optstring(L, ARG(2), ""));
    return 0;
}
static int e_DoRequest(lua_State *L) {       // DoRequest(tag, url, method, data): 結果は OnResponse(tag, content)
    if (!gCur || !H->request) return 0;
    gCur->pending++;
    H->request(gCur, luaL_checkstring(L, ARG(1)), luaL_checkstring(L, ARG(2)), luaL_optstring(L, ARG(3), ""), luaL_optstring(L, ARG(4), ""));
    return 0;
}

static const luaL_Reg kEditor[] = {
    { "GetOrderNotes", e_GetOrderNotes }, { "ClearOrderNotes", e_ClearOrderNotes },
    { "GetSelectNotes", e_GetSelectNotes }, { "SelectNotes", e_GetSelectNotes },
    { "GetNoteCount", e_GetNoteCount }, { "GetNoteAt", e_GetNoteAt }, { "GetCurrentTime", e_GetCurrentTime },
    { "GetCurrentBeat", e_GetCurrentBeat }, { "GetCurrentDivide", e_GetCurrentDivide },
    { "GetNoteTime", e_GetNoteTime }, { "GetNoteBeat", e_GetNoteBeat }, { "GetNoteX", e_GetNoteX },
    { "GetNoteType", e_GetNoteType }, { "GetNoteFlag", e_GetNoteFlag }, { "GetNoteWidth", e_GetNoteWidth },
    { "GetNoteSlideBodyCount", e_GetNoteSlideBodyCount }, { "GetNoteSlideBodyBeat", e_GetNoteSlideBodyBeat },
    { "GetNoteSlideBodyX", e_GetNoteSlideBodyX }, { "GetNoteGroup", e_GetNoteGroup }, { "ChartInfo", e_ChartInfo },
    { "GetSystemTime", e_GetSystemTime }, { "GetClickX", e_GetClickX }, { "GetClickBeat", e_GetClickBeat },
    { "GetClickBeatFree", e_GetClickBeatFree },
    { "GetTimeCount", e_GetTimeCount }, { "GetTimeAt", e_GetTimeAt },
    { "GetEffectCount", e_GetEffectCount }, { "GetEffectAt", e_GetEffectAt },
    { "StartBatch", e_StartBatch }, { "FinishBatch", e_FinishBatch }, { "DeleteNote", e_DeleteNote },
    { "AddNote", e_AddNote }, { "CreateNote", e_AddNote }, { "SetNoteBeat", e_SetNoteBeat }, { "SetNoteX", e_SetNoteX },
    { "SetNoteWidth", e_SetNoteWidth }, { "SelectNote", e_SelectNote },
    { "ShowMessage", e_ShowMessage }, { "ShowTip", e_ShowTip }, { "MakeBeat", e_MakeBeat },
    { "BeatAdd", e_BeatAdd }, { "BeatMinus", e_BeatMinus }, { "Beat", e_Beat }, { "SeekTo", e_SeekTo },
    { "GetUserInput", e_GetUserInput }, { "GetUserSelect", e_GetUserSelect }, { "GetUserSlider", e_GetUserSlider }, { "GetUserSlider2", e_GetUserSlider2 }, { "GetUserOption", e_GetUserOption },
    // 未対応(無視): 描画系・ファイル系・グループなど
    { "SetNoteFlag", e_SetNoteFlag }, { "SetNoteGroup", e_Unsupported }, { "AddNoteSlideBody", e_AddNoteSlideBody },
    { "SetNoteSlideBodyX", e_SetNoteSlideBodyX }, { "DeleteNoteSlideBody", e_DeleteNoteSlideBody },
    { "AddTime", e_AddTime }, { "DeleteTime", e_DeleteTime }, { "AddEffect", e_AddEffect }, { "DeleteEffect", e_DeleteEffect },
    { "AddSprite", e_AddSprite }, { "AddText", e_AddText }, { "RemoveModule", e_RemoveModule },
    { "AddSpriteUndo", e_AddSprite }, { "AddTextUndo", e_AddText }, { "RemoveModuleUndo", e_RemoveModule },
    { "FindModule", e_FindModule }, { "CallShortcut", e_CallShortcut }, { "WriteFile", e_WriteFile },
    { "ReadFile", e_ReadFile }, { "ReadBytes", e_ReadBytes }, { "ReadBytesSelect", e_ReadBytesSelect }, { "ReadData", e_ReadData }, { "WriteData", e_WriteData },
    { "ReadChartCustom", e_ReadChartCustom }, { "WriteChartCustom", e_WriteChartCustom }, { "DoRequest", e_DoRequest },
    { "SetGridLine", e_Unsupported },
    { NULL, NULL } };

static void install(lua_State *L) {
    lua_newtable(L);
    luaL_setfuncs(L, kEditor, 0);
    for (const luaL_Reg *r = kEditor; r->name; r++)
        if (r->func == e_Unsupported) { lua_pushstring(L, r->name); lua_pushcclosure(L, e_Unsupported, 1); lua_setfield(L, -2, r->name); }
    lua_setglobal(L, "Editor");
    lua_newtable(L);
    lua_pushcfunction(L, r_SetResult); lua_setfield(L, -2, "SetResult");
    lua_setglobal(L, "Review");
}

// ソースを読み込んで実行(ヘッダ変数・関数定義の評価)
static int load_run(lua_State *L, const char *src, size_t len, const char *name, char *err, size_t errn) {
    if (luaL_loadbufferx(L, src, len, name, "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        snprintf(err, errn, "%s", lua_tostring(L, -1));
        return -1;
    }
    return 0;
}

int lh_inspect(const char *src, size_t len, const char *chunkname, LHInfo *o) {
    memset(o, 0, sizeof *o);
    o->mode = o->type = -1;
    H = NULL;                  // 調査中は print などを外へ出さない
    lua_State *L = new_state();
    if (!L) { snprintf(o->err, sizeof o->err, "out of memory"); return -1; }
    // 調査中は Editor / Review へのアクセスをすべて無害な空関数にする(ヘッダ部で呼ばれても何も起きない)
    luaL_dostring(L, "local nop=function() end local m={__index=function() return nop end} Editor=setmetatable({},m) Review=setmetatable({},m)");
    int rc = load_run(L, src, len, chunkname, o->err, sizeof o->err);
    if (rc == 0) {
        lua_getglobal(L, "PluginName");
        if (lua_isstring(L, -1)) snprintf(o->name, sizeof o->name, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getglobal(L, "PluginMode"); if (lua_isnumber(L, -1)) o->mode = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "PluginType"); if (lua_isnumber(L, -1)) o->type = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "PluginIcon");
        if (lua_isstring(L, -1)) snprintf(o->icon, sizeof o->icon, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getglobal(L, "PluginRequire");
        if (lua_isstring(L, -1)) snprintf(o->require, sizeof o->require, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getglobal(L, "Run"); o->has_run = lua_isfunction(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "OnClick"); int c = lua_isfunction(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "OnActive"); int a = lua_isfunction(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "OnDragStart"); int d = lua_isfunction(L, -1); lua_pop(L, 1);
        o->has_tool = c || a || d;
    }
    lua_close(L);
    return rc;
}

int lh_run(const char *src, size_t len, const char *chunkname, const LHHost *host, char *err, size_t errn, LHSession **keep) {
    H = host;
    lua_State *L = new_state();
    if (!L) { snprintf(err, errn, "out of memory"); return -1; }
    LHSession *s = calloc(1, sizeof *s);
    s->L = L; s->host = host;
    gCur = s;
    install(L);
    int rc = load_run(L, src, len, chunkname, err, errn);
    if (rc == 0) {
        lua_getglobal(L, "Run");
        if (!lua_isfunction(L, -1)) { snprintf(err, errn, "Run() not defined"); rc = -1; }
        else if (lua_pcall(L, 0, 0, 0) != LUA_OK) { snprintf(err, errn, "%s", lua_tostring(L, -1)); rc = -1; }
    }
    gCur = NULL;
    if (keep && s->pending > 0) { *keep = s; return rc; }
    if (keep) *keep = NULL;
    lua_close(L);
    free(s);
    return rc;
}

// ---- Interact 型セッション ----
LHSession *lh_open(const char *src, size_t len, const char *chunkname, const LHHost *host, char *err, size_t errn) {
    H = host;
    lua_State *L = new_state();
    if (!L) { snprintf(err, errn, "out of memory"); return NULL; }
    install(L);
    LHSession *s = calloc(1, sizeof *s);
    s->L = L; s->host = host;
    gCur = s;
    int rc = load_run(L, src, len, chunkname, err, errn);
    gCur = NULL;
    if (rc != 0) { lua_close(L); free(s); return NULL; }
    return s;
}
int lh_call(LHSession *s, const char *fn, const int *ints, int nints, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_getglobal(s->L, fn);
    if (!lua_isfunction(s->L, -1)) { lua_pop(s->L, 1); gCur = NULL; return 0; }
    for (int i = 0; i < nints; i++) lua_pushinteger(s->L, ints[i]);
    int prc = lua_pcall(s->L, nints, 0, 0);
    gCur = NULL;
    if (prc != LUA_OK) {
        snprintf(err, errn, "%s", lua_tostring(s->L, -1));
        lua_pop(s->L, 1);
        return -1;
    }
    return 0;
}
int lh_call_key(LHSession *s, int key, int down, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_getglobal(s->L, "OnKey");
    if (!lua_isfunction(s->L, -1)) { lua_pop(s->L, 1); gCur = NULL; return 0; }
    lua_pushinteger(s->L, key);
    lua_pushboolean(s->L, down);
    int prc = lua_pcall(s->L, 2, 0, 0);
    gCur = NULL;
    if (prc != LUA_OK) {
        snprintf(err, errn, "%s", lua_tostring(s->L, -1));
        lua_pop(s->L, 1);
        return -1;
    }
    return 0;
}
void lh_close(LHSession *s) {
    if (!s) return;
    lua_close(s->L);
    free(s);
}

int lh_pending(LHSession *s) { return s ? s->pending : 0; }
int lh_input_deliver(LHSession *s, int id, const char *text, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, id);
    luaL_unref(s->L, LUA_REGISTRYINDEX, id);
    if (s->pending > 0) s->pending--;
    int rc = 0;
    if (lua_isfunction(s->L, -1) && text) {
        lua_pushstring(s->L, text);
        if (lua_pcall(s->L, 1, 0, 0) != LUA_OK) {
            snprintf(err, errn, "%s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
            rc = -1;
        }
    } else lua_pop(s->L, 1);
    gCur = NULL;
    return rc;
}

int lh_response(LHSession *s, const char *tag, const char *content, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    if (s->pending > 0) s->pending--;
    int rc = 0;
    lua_getglobal(s->L, "OnResponse");
    if (lua_isfunction(s->L, -1)) {
        lua_pushstring(s->L, tag);
        lua_pushstring(s->L, content);
        if (lua_pcall(s->L, 2, 0, 0) != LUA_OK) {
            snprintf(err, errn, "%s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
            rc = -1;
        }
    } else lua_pop(s->L, 1);
    gCur = NULL;
    return rc;
}

int lh_bytes_deliver(LHSession *s, int id, const unsigned char *data, size_t n, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, id);
    luaL_unref(s->L, LUA_REGISTRYINDEX, id);
    if (s->pending > 0) s->pending--;
    int rc = 0;
    if (lua_isfunction(s->L, -1)) {
        if (!data || n > 16u * 1024 * 1024) lua_pushnil(s->L);
        else {
            lua_createtable(s->L, (int)n, 1);
            for (size_t i = 0; i < n; i++) { lua_pushinteger(s->L, data[i]); lua_rawseti(s->L, -2, (lua_Integer)i); }
            lua_pushinteger(s->L, (lua_Integer)n); lua_setfield(s->L, -2, "Length");
        }
        if (lua_pcall(s->L, 1, 0, 0) != LUA_OK) {
            snprintf(err, errn, "%s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
            rc = -1;
        }
    } else lua_pop(s->L, 1);
    gCur = NULL;
    return rc;
}

int lh_choice_deliver(LHSession *s, int id, int kind, int value, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, id);
    luaL_unref(s->L, LUA_REGISTRYINDEX, id);
    if (s->pending > 0) s->pending--;
    int rc = 0;
    if (lua_isfunction(s->L, -1)) {
        if (kind == 1) lua_pushboolean(s->L, value != 0); else lua_pushinteger(s->L, value);
        if (lua_pcall(s->L, 1, 0, 0) != LUA_OK) {
            snprintf(err, errn, "%s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
            rc = -1;
        }
    } else lua_pop(s->L, 1);
    gCur = NULL;
    return rc;
}

int lh_slider_deliver(LHSession *s, int id, int n, const double *vals, char *err, size_t errn) {
    if (!s) return -1;
    H = s->host;
    gCur = s;
    gDeadline = clock() + 3 * CLOCKS_PER_SEC;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, id);
    luaL_unref(s->L, LUA_REGISTRYINDEX, id);
    if (s->pending > 0) s->pending--;
    int rc = 0;
    if (lua_isfunction(s->L, -1) && vals && n > 0) {
        for (int i = 0; i < n; i++) lua_pushnumber(s->L, vals[i]);
        if (lua_pcall(s->L, n, 0, 0) != LUA_OK) {
            snprintf(err, errn, "%s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
            rc = -1;
        }
    } else lua_pop(s->L, 1);
    gCur = NULL;
    return rc;
}
