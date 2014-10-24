#include <string.h>

#define eq(x, y) !strcmp(x, y)
#define return(x) strcpy(dest, x); return path;

char* getlibname(char *dest, char *path, char *host)
{
    char *p;

    p = path;
    do {
        if(eq(p, ".asm")) {
            return("./asm.so");
        } else if(eq(p, ".c")) {
            return("./c.so");
        }
    } while(*(++p));

    return("./static.so");
}
