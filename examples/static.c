#include <stdio.h>

int getpage(char *dest, const char *path, const char *post)
{
    FILE *file;
    int len;
    const char *filepath;

    if(path[0] != '/') {
        return -1;
    }

    filepath = path + 1;
    if(!path[1]) {
        filepath = "index.html";
    }

    if(!(file = fopen(filepath, "rb"))) {
        return -1;
    }

    len = fread(dest, 1, 65535, file);
    fclose(file);

    return len;
}
