#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *data;
    int type;
    char buf[0];
} PAGEINFO;

int filetype(const char *path)
{
    do {
        if(!strcmp(path, ".png")) {
            return 1;
        }
    } while(*(++path));
    return 0;
}

int getpage(PAGEINFO *p, const char *path, const char *post)
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

    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if(len >= 32756) {
        p->data = malloc(len);
        if(!p->data) {
            return -1;
        }
        len = fread(p->data, 1, len, file);
    } else {
        len = fread(p->buf, 1, len, file);
    }

    fclose(file);

    p->type = filetype(filepath);
    return len;
}
