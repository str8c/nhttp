#include <string.h>
#include <stdio.h>

#define prefix(x, y) !strncmp(x, y, sizeof(y) - 1)
#define suffix(x, len, y) !(len < (sizeof(y) - 1) || strcmp(x + len - (sizeof(y) - 1), y))

char* getlibname(char *dest, char *path, char *host)
{
    if(prefix(host, "code.")) {
        strcpy(dest, "./code.so");
    } else if(prefix(path, "code/")) {
        path += sizeof("code/") - 1;
        strcpy(dest, "./code.so");
    } else if(prefix(host, "bbs.")) {
        strcpy(dest, "./nbbs.so");
    } else if(prefix(path, "talk/")) {
        path += sizeof("talk/") - 1;
        strcpy(dest, "./nbbs.so");
    } else {
        strcpy(dest, "./static.so");
    }

    printf("%s %s\n", path, dest);

    return path;
}
