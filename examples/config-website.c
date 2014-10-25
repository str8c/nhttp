#include "../nhttp.h"

#define prefix(x, y) !strncmp(x, y, sizeof(y) - 1)
#define suffix(x, len, y) !(len < (sizeof(y) - 1) || strcmp(x + len - (sizeof(y) - 1), y))
#define return(x) *getpage = x##_getpage; return path

import(code);
import(nbbs);
import(static);

char* getconfig(GETPAGE **getpage, char *path, char *host)
{
    if(prefix(host, "code.")) {
        return(code);
    }

    if(prefix(path, "code/")) {
        path += sizeof("code/") - 1;
        return(code);
    }

    if(prefix(host, "bbs.")) {
        return(nbbs);
    }

    if(prefix(path, "talk/")) {
        path += sizeof("talk/") - 1;
        return(nbbs);
    }

    return(static);
}
