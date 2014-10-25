#include "../nhttp.h"

static int filetype(const char *path)
{
    do {
        if(!strcmp(path, ".png")) {
            return IMAGE_PNG;
        }
    } while(*(++path));
    return TEXT_HTML;
}

int export(static, getpage)(PAGEINFO *p, const char *path, const char *post, int postlen)
{
    FILE *file;
    int len;
    char *str;

    /* prepend path with ROOT */
    len = strlen(path);
    char filepath[len + sizeof(ROOT)];
    strcpy(filepath, ROOT);
    str = filepath + sizeof(ROOT) - 1;
    memcpy(str, path, len + 1);

    if(!*str) {
        strcpy(str, "index.html");
    }

    if(!(file = fopen(filepath, "rb"))) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if(len > sizeof(p->buf)) {
        p->data = malloc(len);
        if(!p->data) {
            return -1;
        }
        len = fread(p->data, 1, len, file);
    } else {
        len = fread(p->buf, 1, len, file);
    }

    fclose(file);

    p->type = filetype(str);
    return len;
}
