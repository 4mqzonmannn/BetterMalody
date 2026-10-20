#include "zipx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <iconv.h>
#include <zlib.h>

#define MAX_FILES 600
#define MAX_ONE   (16u * 1024 * 1024)
#define MAX_TOTAL (96u * 1024 * 1024)

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static int valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
        if (n < 0) return 0;
        p++;
        while (n--) { if ((*p & 0xC0) != 0x80) return 0; p++; }
    }
    return 1;
}
static void to_utf8(char *name, size_t cap) {      // UTF-8 でなければ GB18030 とみなして変換する
    if (valid_utf8(name)) return;
    iconv_t cd = iconv_open("UTF-8", "GB18030");
    if (cd == (iconv_t)-1) return;
    char out[1024];
    char *in = name, *o = out;
    size_t il = strlen(name), ol = sizeof out - 1;
    if (iconv(cd, &in, &il, &o, &ol) != (size_t)-1) { *o = 0; snprintf(name, cap, "%s", out); }
    iconv_close(cd);
}
static int mkdirs(char *path) {                     // path のディレクトリ部分を作る
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(path, 0755);
        *p = '/';
    }
    return 0;
}
static int ext_ok(const char *name, const char *allow) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    char e[16]; size_t i = 0;
    for (dot++; *dot && i < sizeof e - 1; dot++) e[i++] = (char)((*dot >= 'A' && *dot <= 'Z') ? *dot + 32 : *dot);
    e[i] = 0;
    if (!i) return 0;
    const char *a = allow;
    while (*a) {
        const char *c = strchr(a, ',');
        size_t l = c ? (size_t)(c - a) : strlen(a);
        if (l == i && !strncmp(a, e, l)) return 1;
        if (!c) break;
        a = c + 1;
    }
    return 0;
}
static int safe_path(const char *s) {
    if (!s[0] || s[0] == '/' || strstr(s, "..") || strchr(s, '\\') || strstr(s, "__MACOSX")) return 0;
    for (const char *p = s; *p; p++) if (*p == '/' && p[1] == '.') return 0;   // 隠しファイル
    return s[0] != '.';
}

typedef struct { char name[1024]; unsigned method, csize, usize, off, flags; } Ent;

int zipx_extract(const unsigned char *z, size_t n, const char *dest, const char *allow, ZipxStats *st) {
    memset(st, 0, sizeof *st);
    if (n < 22) { snprintf(st->err, sizeof st->err, "not a zip"); return -1; }
    size_t e = n - 22, lim = n > 65557 ? n - 65557 : 0;
    while (e > lim && rd32(z + e) != 0x06054b50) e--;
    if (rd32(z + e) != 0x06054b50) { snprintf(st->err, sizeof st->err, "no end record"); return -1; }
    unsigned total = rd16(z + e + 10), cdoff = rd32(z + e + 16);
    if (cdoff >= n) { snprintf(st->err, sizeof st->err, "bad directory"); return -1; }
    Ent *ents = calloc(total < MAX_FILES * 4 ? total + 1 : MAX_FILES * 4 + 1, sizeof *ents);
    size_t cnt = 0, p = cdoff;
    for (unsigned i = 0; i < total && cnt < MAX_FILES * 4; i++) {
        if (p + 46 > n || rd32(z + p) != 0x02014b50) break;
        unsigned nl = rd16(z + p + 28), xl = rd16(z + p + 30), cl = rd16(z + p + 32);
        if (p + 46 + nl > n) break;
        Ent *en = &ents[cnt];
        en->flags = rd16(z + p + 8); en->method = rd16(z + p + 10);
        en->csize = rd32(z + p + 20); en->usize = rd32(z + p + 24); en->off = rd32(z + p + 42);
        size_t k = nl < sizeof en->name - 1 ? nl : sizeof en->name - 1;
        memcpy(en->name, z + p + 46, k); en->name[k] = 0;
        to_utf8(en->name, sizeof en->name);
        cnt++;
        p += 46 + nl + xl + cl;
    }
    // 共通の最上位フォルダを取り除く
    char top[256] = ""; int common = 1, anyFile = 0;
    for (size_t i = 0; i < cnt; i++) {
        const char *nm = ents[i].name;
        size_t l = strlen(nm);
        if (!l || nm[l - 1] == '/' || strstr(nm, "__MACOSX")) continue;
        anyFile = 1;
        const char *sl = strchr(nm, '/');
        if (!sl) { common = 0; break; }
        if (!top[0]) { snprintf(top, sizeof top, "%.*s", (int)(sl - nm), nm); }
        else if (strncmp(nm, top, (size_t)(sl - nm)) || strlen(top) != (size_t)(sl - nm)) { common = 0; break; }
    }
    size_t strip = (common && anyFile && top[0]) ? strlen(top) + 1 : 0;
    size_t sum = 0;
    int rc = 0;
    for (size_t i = 0; i < cnt; i++) {
        Ent *en = &ents[i];
        size_t l = strlen(en->name);
        if (!l || en->name[l - 1] == '/') continue;
        const char *rel = en->name;
        if (strip && strlen(rel) > strip && !strncmp(rel, top, strip - 1)) rel += strip;
        if (!safe_path(rel) || !ext_ok(rel, allow) || (en->flags & 1) || (en->method != 0 && en->method != 8)) { st->skipped++; continue; }
        if (st->files >= MAX_FILES || en->usize > MAX_ONE || sum + en->usize > MAX_TOTAL) { st->skipped++; continue; }
        if ((size_t)en->off + 30 > n || rd32(z + en->off) != 0x04034b50) { st->skipped++; continue; }
        size_t data = (size_t)en->off + 30 + rd16(z + en->off + 26) + rd16(z + en->off + 28);
        if (data > n || en->csize > n - data) { st->skipped++; continue; }
        unsigned char *out = malloc(en->usize ? en->usize : 1);
        if (!out) { st->skipped++; continue; }
        int good = 0;
        if (en->method == 0) { if (en->csize == en->usize) { memcpy(out, z + data, en->usize); good = 1; } }
        else {
            z_stream zs; memset(&zs, 0, sizeof zs);
            if (inflateInit2(&zs, -15) == Z_OK) {
                zs.next_in = (Bytef *)(z + data); zs.avail_in = en->csize;
                zs.next_out = out; zs.avail_out = en->usize;
                int r = inflate(&zs, Z_FINISH);
                good = (r == Z_STREAM_END && zs.total_out == en->usize);
                inflateEnd(&zs);
            }
        }
        if (good) {
            char path[2048];
            snprintf(path, sizeof path, "%s/%s", dest, rel);
            mkdirs(path);
            FILE *f = fopen(path, "wb");
            if (f) {
                if (fwrite(out, 1, en->usize, f) == en->usize) { st->files++; st->bytes += en->usize; sum += en->usize; }
                else st->skipped++;
                fclose(f);
            } else st->skipped++;
        } else st->skipped++;
        free(out);
    }
    free(ents);
    return rc;
}
