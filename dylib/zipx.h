// 最小の zip 展開(store / deflate)。プラグインの zip インポート用。パス検査・サイズ上限つき。
#ifndef ZIPX_H
#define ZIPX_H
#include <stddef.h>
typedef struct { int files, skipped; size_t bytes; char err[96]; } ZipxStats;
// dest 配下へ展開する。allow = 許可する拡張子("lua,png,..."、小文字)。共通の最上位フォルダは取り除く。0=成功
int zipx_extract(const unsigned char *z, size_t n, const char *dest, const char *allow, ZipxStats *st);
#endif
