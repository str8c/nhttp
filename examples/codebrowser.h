#include "../nhttp.h"

#define eq(x, y) !strcmp(x, y)
#define st(x) dest += sprintf(dest, (x))
#define escapehtml(ch) \
    if(ch == '&') {st("&amp;"); continue;} \
    if(ch == '>') {st("&gt;"); continue; } \
    if(ch == '<') {st("&lt;"); continue; }

#define find(word, list) _find(word, list, sizeof(list)/sizeof(*list) - 1)
int _find(const char *word, const char **list, int max);

#define write(x, n) memcpy(dest, x, (n)); dest += (n)
#define word() write(w, src - w)
#define wordtag(x) st("<" x ">"); word(); st("</" x ">")
