#include <stdio.h>

int getpage(char *dest, char *path, char *post)
{
    FILE *file;
    int len;

    if(path[0] != '/') {
        return -1;
    }

    if(!(file = fopen(path + 1, "rb"))) {
        return -1;
    }

    len = fread(dest, 1, 65535, file);
    fclose(file);

    return len;
}
